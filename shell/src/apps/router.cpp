// Router: a laptop plugged into the Cardputer goes online through your
// Tailscale exit node (docs/ROUTER.md).
//
//   Ethernet   the laptop on the Ethernet port (switch on Host)
//   USB-C      the laptop on the right-hand USB-C port, which becomes a USB
//              network adapter (switch on Slave): one cable for power and
//              network
//   Exit node  where the traffic comes out: one of your machines, or a
//              Mullvad location
//
// zeroos-router@<mode>.service does the work as root (image/stage-zeroos/
// files/router/zeroos-router) and writes /run/zeroos/router.state. Anything
// that needs a command runs on a worker thread, so the screen never waits.
// The simulator gets a pretend router.
#include <atomic>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <memory>
#include <mutex>
#include <sstream>
#include <thread>
#include <vector>

#include "apps/common.h"
#include "ui/prefs.h"
#include "ui/remote.h"

namespace zs {
namespace {
using namespace theme;

constexpr int kListW = 168;
const char* kHelper = "/usr/lib/zeroos/router";

struct ExitNode {
    std::string ip, name, place;
    bool online = false, selected = false;
};

struct RouterState {
    std::string mode;     // "" (off), "ethernet" or "usb"
    bool error = false;
    std::string error_text, lan, address;
};

struct Lease {
    std::string ip, name;
};

std::string read_file(const std::string& path) {
    std::ifstream f(path);
    std::stringstream s;
    s << f.rdbuf();
    std::string t = s.str();
    while (!t.empty() && (t.back() == '\n' || t.back() == ' ')) t.pop_back();
    return t;
}

long long read_num(const std::string& path) { return std::atoll(read_file(path).c_str()); }

std::vector<std::string> split(const std::string& line, char sep) {
    std::vector<std::string> v;
    std::string cur;
    for (char ch : line) {
        if (ch == sep) {
            v.push_back(cur);
            cur.clear();
        } else {
            cur += ch;
        }
    }
    v.push_back(cur);
    return v;
}

std::string run_cmd(const std::string& cmd) {
    std::string outp;
    if (FILE* p = popen(cmd.c_str(), "r")) {
        char buf[512];
        size_t n;
        while ((n = fread(buf, 1, sizeof buf, p)) > 0) outp.append(buf, n);
        if (pclose(p) != 0 && outp.empty()) outp = "\x01";   // failed with nothing to say
    }
    return outp;
}

// A command on a worker thread. The result waits in shared state, so an app
// that closed meanwhile is never touched.
struct Job {
    std::mutex m;
    bool done = false;
    std::string out;
};

std::shared_ptr<Job> spawn(const std::string& cmd) {
    auto job = std::make_shared<Job>();
    auto work = [job, cmd] {
        std::string o = run_cmd(cmd);
        std::lock_guard<std::mutex> l(job->m);
        job->out = std::move(o);
        job->done = true;
    };
#ifdef __EMSCRIPTEN__
    work();
#else
    std::thread(work).detach();
#endif
    return job;
}

bool take(std::shared_ptr<Job>& job, std::string& out) {
    if (!job) return false;
    std::lock_guard<std::mutex> l(job->m);
    if (!job->done) return false;
    out = std::move(job->out);
    job.reset();
    return true;
}

// "4.9" or "12": Mbit/s, short enough for both directions on one line.
std::string mbit(double bytes_per_s) {
    double m = bytes_per_s * 8 / 1e6;
    return m >= 10 ? fmt("%.0f", m) : fmt("%.1f", m < 0 ? 0.0 : m);
}

const char* mode_name(const std::string& m) { return m == "usb" ? "USB-C" : "Ethernet"; }

class RouterApp : public App {
public:
    void build(lv_obj_t* root) override {
        root_ = root;
        sim_ = hal().system().simulated;
        panel_ = lv_obj_create(root);
        style_panel(panel_);
        lv_obj_set_pos(panel_, kListW + 8, 4);
        lv_obj_set_size(panel_, kScreenW - kListW - 14, kContentH - 8);
        lv_obj_remove_flag(panel_, LV_OBJ_FLAG_SCROLLABLE);
        head_ = label(panel_, "", &zs_font_14, kText);
        lv_obj_set_width(head_, kScreenW - kListW - 26);
        lv_label_set_long_mode(head_, LV_LABEL_LONG_WRAP);
        body_ = label(panel_, "", &zs_font_12, kMuted);
        lv_obj_set_width(body_, kScreenW - kListW - 26);
        lv_label_set_long_mode(body_, LV_LABEL_LONG_WRAP);
        // Tailscale may be stopped ("when needed", unused for 15 minutes):
        // without it there are no exit nodes to pick. Start it, as Remote does.
        if (!sim_) {
            remote_note_use();
            TsStatus t = hal().tailscale();
            if (t.installed && !t.running) hal().tailscale_start();
        }
        read_state();
        refresh_nodes();
        rebuild(0);
    }

    void tick() override {
        ++n_;
        remote_note_use();   // keep Tailscale up while the app is open
        std::string out;
        if (take(nodes_job_, out)) {
            parse_nodes(out);
            dirty_ = true;
        }
        if (take(action_job_, out)) {
            if (out == "\x01") shell_toast("That didn't work: see docs/ROUTER.md");
            refresh_nodes();
            n_ = 0;
        }
        if (n_ % 10 == 0) {   // once a second
            std::string before = signature();
            read_state();
            measure();
            if (signature() != before) dirty_ = true;
            else show_panel(focused());
        }
        // Exit nodes come and go; while Tailscale starts, look more often.
        if (n_ % (ts_state_ == "Running" || ts_state_ == "Missing" ? 150 : 20) == 0) refresh_nodes();
        if (dirty_) {
            dirty_ = false;
            rebuild(focused());
        }
    }

    bool on_back() override {
        if (page_ == Page::Main) return false;
        page_ = Page::Main;
        rebuild(2);
        return true;
    }

private:
    enum class Page { Main, Nodes };
    enum class Row { Ethernet, Usb, ExitNode, Node, NoNode };

    // ---- What the router is doing ---------------------------------------

    void read_state() {
        RouterState s;
        if (sim_) {
            s = sim_state_;
            if (!sim_mode_.empty() && sim_clock_.ms() > 1500) s.mode = sim_mode_;
            s.lan = s.mode == "usb" ? "usb0" : "eth0";
            s.address = s.mode == "usb" ? "10.43.0.1" : "10.42.0.1";
        } else {
            std::istringstream in(read_file("/run/zeroos/router.state"));
            for (std::string l; std::getline(in, l);) {
                auto eq = l.find('=');
                if (eq == std::string::npos) continue;
                std::string k = l.substr(0, eq), v = l.substr(eq + 1);
                if (k == "mode") s.mode = v;
                else if (k == "state") s.error = v == "error";
                else if (k == "error") s.error_text = v;
                else if (k == "lan") s.lan = v;
                else if (k == "address") s.address = v;
            }
        }
        st_ = s;
        if (!pending_.empty() && (st_.mode == pending_ || pending_clock_.ms() > 120000)) pending_.clear();
        if (pending_ == "off" && st_.mode.empty()) pending_.clear();
    }

    // The laptop's link, its address and the traffic, once a second.
    void measure() {
        cable_ = false;
        leases_.clear();
        if (st_.mode.empty() || st_.error) {
            up_ = down_ = 0;
            return;
        }
        long long rx, tx;
        if (sim_) {
            long t = sim_clock_.ms();
            cable_ = t > 3500;
            if (cable_) leases_.push_back({st_.mode == "usb" ? "10.43.0.12" : "10.42.0.37", "laptop"});
            rx = cable_ ? t * 40 : 0;
            tx = cable_ ? t * 610 : 0;
        } else {
            std::string base = "/sys/class/net/" + st_.lan;
            cable_ = read_file(base + "/carrier") == "1";
            rx = read_num(base + "/statistics/rx_bytes");
            tx = read_num(base + "/statistics/tx_bytes");
            std::istringstream in(read_file("/var/lib/NetworkManager/dnsmasq-" + st_.lan + ".leases"));
            for (std::string l; std::getline(in, l);) {
                auto f = split(l, ' ');   // expiry mac ip name client-id
                if (f.size() >= 4) leases_.push_back({f[2], f[3] == "*" ? "" : f[3]});
            }
        }
        long now = clock_.ms();
        if (last_ms_ > 0 && now > last_ms_) {
            double s = (now - last_ms_) / 1000.0;
            up_ = (rx - last_rx_) / s;   // the laptop sends: we receive
            down_ = (tx - last_tx_) / s;
        }
        last_ms_ = now;
        last_rx_ = rx;
        last_tx_ = tx;
    }

    void refresh_nodes() {
        if (sim_) {
            ts_state_ = "Running";
            if (nodes_.empty()) {
                nodes_ = {{"100.101.4.2", "homelab", "", true, true},
                          {"100.88.12.9", "parents-pi", "", true, false},
                          {"100.64.3.30", "office-mac", "", false, false},
                          {"100.120.0.7", "nl-ams-wg-001", "Amsterdam, Netherlands", true, false},
                          {"100.120.0.9", "us-nyc-wg-301", "New York, USA", true, false}};
            }
            return;
        }
        if (!nodes_job_) nodes_job_ = spawn(std::string(kHelper) + " nodes 2>/dev/null");
    }

    void parse_nodes(const std::string& out) {
        nodes_.clear();
        ts_state_ = out.empty() || out == "\x01" ? "Missing" : "";
        std::istringstream in(out);
        for (std::string l; std::getline(in, l);) {
            auto f = split(l, '\t');
            if (f[0] == "state" && f.size() >= 2) ts_state_ = f[1];
            if (f[0] == "node" && f.size() >= 6)
                nodes_.push_back({f[1], f[2], f[5], f[3] == "1", f[4] == "1"});
        }
    }

    const ExitNode* chosen() const {
        for (auto& n : nodes_)
            if (n.selected) return &n;
        return nullptr;
    }

    std::string signature() const {
        return st_.mode + "|" + std::to_string(st_.error) + st_.error_text + "|" + pending_ + "|" +
               std::to_string(cable_) + "|" + std::to_string(leases_.size()) + "|" + ts_state_;
    }

    // ---- Actions ----------------------------------------------------------

    void turn_on(const std::string& mode) {
        if (nodes_job_ && ts_state_.empty()) {
            shell_toast("Checking Tailscale, one moment...");
            return;
        }
        if (ts_state_ == "Missing") {
            shell_toast("Router needs Remote (Tailscale): get it from Apps");
            return;
        }
        if (ts_state_ == "NeedsLogin") {
            shell_toast("Sign in to Tailscale in Remote first");
            return;
        }
        if (!chosen()) {
            shell_toast("Pick an exit node first");
            page_ = Page::Nodes;
            dirty_ = true;
            return;
        }
        keep_tailscale_on(true);
        pending_ = mode;
        pending_clock_.reset();
        if (sim_) {
            sim_mode_ = mode;
            sim_state_ = {};
            sim_clock_.reset();
            last_ms_ = 0;
        } else {
            std::string off = mode == "usb" ? "ethernet" : "usb";
            action_job_ = spawn("systemctl stop zeroos-router@" + off + ".service; systemctl start --no-block zeroos-router@" +
                                mode + ".service");
        }
        shell_toast(std::string("Router on over ") + mode_name(mode) + "...");
        dirty_ = true;
    }

    void turn_off() {
        pending_ = "off";
        pending_clock_.reset();
        if (sim_) {
            sim_mode_.clear();
            sim_state_ = {};
        } else {
            action_job_ = spawn("systemctl stop zeroos-router@ethernet.service zeroos-router@usb.service");
        }
        keep_tailscale_on(false);
        shell_toast("Router off");
        dirty_ = true;
    }

    // Tailscale normally stops 15 minutes after Remote was last used
    // (Settings > Tailscale: when needed). The router needs it all along, and
    // after a restart too, so it is "always on" while the router is; the
    // user's own choice comes back when the router is turned off.
    void keep_tailscale_on(bool on) {
        if (on) {
            std::string m = tailscale_mode();
            if (m != "always") {
                prefs::set_str("router.tailscale_mode", m);
                set_tailscale_mode("always");
            }
        } else {
            std::string before = prefs::get_str("router.tailscale_mode", "");
            if (!before.empty()) {
                set_tailscale_mode(before);
                prefs::set_str("router.tailscale_mode", "");
            }
        }
    }

    void pick_node(int i) {
        std::string ip = i >= 0 && i < int(nodes_.size()) ? nodes_[size_t(i)].ip : "";
        if (!st_.mode.empty() && ip.empty()) {
            shell_toast("Turn the router off first: it needs an exit node");
            return;
        }
        if (sim_) {
            for (size_t k = 0; k < nodes_.size(); ++k) nodes_[k].selected = int(k) == i;
        } else {
            action_job_ = spawn("tailscale set --exit-node=" + ip + " 2>/dev/null");
            for (size_t k = 0; k < nodes_.size(); ++k) nodes_[k].selected = int(k) == i;   // until it's confirmed
        }
        shell_toast(ip.empty() ? "No exit node" : "Exit node: " + nodes_[size_t(i)].name);
        page_ = Page::Main;
        dirty_ = true;
    }

    // ---- Screen -----------------------------------------------------------

    struct Item {
        Row row;
        int node = -1;
    };

    std::string row_text(Row r) const {
        std::string mode = r == Row::Ethernet ? "ethernet" : "usb";
        std::string name = mode_name(mode);
        if (pending_ == mode) return name + ": starting...";
        if (st_.mode == mode) return name + (st_.error ? ": failed" : cable_ ? ": in use" : ": on");
        return name + ": off";
    }

    void rebuild(int focus) {
        if (list_) {
            if (group) lv_group_remove_all_objs(group);
            lv_obj_delete(list_);
        }
        items_.clear();
        buttons_.clear();
        list_ = make_list(root_, 4, 4, kListW, kContentH - 8);
        if (page_ == Page::Main) {
            add(LV_SYMBOL_SHUFFLE, row_text(Row::Ethernet), {Row::Ethernet});
            add(LV_SYMBOL_USB, row_text(Row::Usb), {Row::Usb});
            const ExitNode* n = chosen();
            add(ZS_ICON_GLOBE, "Exit node: " + (n ? n->name : std::string("none")), {Row::ExitNode});
        } else {
            for (size_t i = 0; i < nodes_.size(); ++i) {
                const ExitNode& n = nodes_[i];
                lv_obj_t* b = add(n.selected ? LV_SYMBOL_OK : n.place.empty() ? ZS_ICON_DESKTOP : ZS_ICON_GLOBE,
                                  n.name, {Row::Node, int(i)});
                if (!n.online) lv_obj_set_style_text_color(b, c(kMuted), 0);
            }
            add(LV_SYMBOL_CLOSE, "None", {Row::NoNode});
        }
        finish_list(list_);
        if (focus < 0) focus = 0;
        if (focus >= int(buttons_.size())) focus = int(buttons_.size()) - 1;
        if (focus >= 0) lv_group_focus_obj(buttons_[size_t(focus)]);
        show_panel(focus);
    }

    lv_obj_t* add(const char* symbol, const std::string& text, Item item) {
        lv_obj_t* b = list_button(list_, symbol, text.c_str(), on_click, this);
        lv_obj_set_user_data(b, (void*)(intptr_t)items_.size());
        lv_obj_add_event_cb(b, on_focus, LV_EVENT_FOCUSED, this);
        if (lv_obj_t* lbl = lv_obj_get_child(b, 1)) lv_label_set_long_mode(lbl, LV_LABEL_LONG_DOT);
        items_.push_back(item);
        buttons_.push_back(b);
        return b;
    }

    int focused() {
        lv_obj_t* f = group ? lv_group_get_focused(group) : nullptr;
        if (!f || !list_ || lv_obj_get_parent(f) != list_) return -1;
        return int((intptr_t)lv_obj_get_user_data(f));
    }

    // Where the laptop's traffic goes, in a few lines.
    std::string status_lines() const {
        std::string s;
        if (cable_) {
            if (leases_.empty()) s += "Laptop plugged in, getting an address...\n";
            for (auto& l : leases_) s += (l.name.empty() ? "Laptop" : l.name) + ": " + l.ip + "\n";
            s += LV_SYMBOL_DOWN " " + mbit(down_) + "  " LV_SYMBOL_UP " " + mbit(up_) + " Mbit/s\n";
        } else {
            s += st_.mode == "usb" ? "Plug the laptop into the right-hand USB-C port.\n"
                                   : "Plug the laptop into the Ethernet port.\n";
        }
        const ExitNode* n = chosen();
        if (ts_state_ != "Running") s += "Tailscale: " + (ts_state_.empty() ? std::string("starting") : ts_state_) + "\n";
        else if (n && !n->online) s += n->name + " is offline: no internet until it's back.\n";
        else if (n) s += "Out through " + n->name + ".\n";
        return s;
    }

    void show_panel(int i) {
        if (!head_ || i < 0 || i >= int(items_.size())) return;
        const Item& it = items_[size_t(i)];
        std::string head, body;
        uint32_t color = kText;
        switch (it.row) {
            case Row::Ethernet:
            case Row::Usb: {
                std::string mode = it.row == Row::Ethernet ? "ethernet" : "usb";
                if (pending_ == mode) {
                    head = "Starting...";
                    color = kWarn;
                    body = "Setting up the network and the kill switch.";
                } else if (st_.mode == mode && st_.error) {
                    head = "Failed";
                    color = kBad;
                    body = st_.error_text + "\nEnter turns it off.";
                } else if (st_.mode == mode) {
                    head = cable_ ? "Sharing" : "On";
                    color = kGood;
                    body = status_lines() + "Enter turns it off.";
                } else if (mode == "usb") {
                    head = "USB-C";
                    body = "One cable: the laptop powers the Cardputer and gets its network.\nSwitch on the left: "
                           "Slave. Online here over Wi-Fi.";
                } else {
                    head = "Ethernet";
                    body = "The laptop on the Ethernet port goes online through your exit node.\nSwitch on the "
                           "left: Host.";
                }
                break;
            }
            case Row::ExitNode: {
                const ExitNode* n = chosen();
                head = n ? n->name : "No exit node";
                color = n ? (n->online ? kGood : kWarn) : kText;
                if (ts_state_ == "Missing") body = "Router needs Remote (Tailscale). Get it from Apps.";
                else if (ts_state_ == "NeedsLogin") body = "Sign in to Tailscale in Remote first.";
                else if (ts_state_ != "Running") body = "Starting Tailscale...";
                else if (n) body = (n->place.empty() ? std::string("") : n->place + "\n") +
                                   (n->online ? "Online." : "Offline.") + "\nEnter picks another.";
                else body = "Where the laptop's traffic comes out. Enter picks one.";
                break;
            }
            case Row::Node: {
                const ExitNode& n = nodes_[size_t(it.node)];
                head = n.name;
                color = n.online ? kText : kMuted;
                body = (n.place.empty() ? "One of your machines" : n.place) + std::string("\n") + n.ip + "\n" +
                       (n.online ? "Online" : "Offline") + (n.selected ? ", in use" : "");
                break;
            }
            case Row::NoNode:
                head = "None";
                body = nodes_.empty() && ts_state_ != "Running" ? "Starting Tailscale..."
                       : nodes_.empty() ? "No exit nodes yet. Make a machine one with: tailscale set --advertise-exit-node"
                                      : "Stop using an exit node (the router needs one).";
                break;
        }
        lv_label_set_text(head_, head.c_str());
        lv_obj_set_style_text_color(head_, c(color), 0);
        lv_label_set_text(body_, body.c_str());
        lv_obj_update_layout(head_);
        lv_obj_set_pos(body_, 0, lv_obj_get_height(head_) + 4);
    }

    void activate(int i) {
        if (i < 0 || i >= int(items_.size())) return;
        Item it = items_[size_t(i)];
        switch (it.row) {
            case Row::Ethernet:
            case Row::Usb: {
                std::string mode = it.row == Row::Ethernet ? "ethernet" : "usb";
                if (st_.mode == mode || pending_ == mode) turn_off();
                else turn_on(mode);
                break;
            }
            case Row::ExitNode:
                if (ts_state_ == "Missing") {
                    shell_toast("Router needs Remote (Tailscale): get it from Apps");
                    return;
                }
                page_ = Page::Nodes;
                refresh_nodes();
                rebuild(0);
                return;
            case Row::Node: pick_node(it.node); return;
            case Row::NoNode: pick_node(-1); return;
        }
        rebuild(i);
    }

    static void on_click(lv_event_t* e) {
        auto* self = static_cast<RouterApp*>(lv_event_get_user_data(e));
        self->pending_click_ = int((intptr_t)lv_obj_get_user_data(lv_event_get_target_obj(e)));
        // Deferred: activating rebuilds the list, which deletes the clicked button.
        lv_async_call([](void* p) {
            auto* s = static_cast<RouterApp*>(p);
            s->activate(s->pending_click_);
        }, self);
    }
    static void on_focus(lv_event_t* e) {
        auto* self = static_cast<RouterApp*>(lv_event_get_user_data(e));
        self->show_panel(int((intptr_t)lv_obj_get_user_data(lv_event_get_target_obj(e))));
    }

    struct Clock {
        uint32_t start = lv_tick_get();
        long ms() const { return long(lv_tick_elaps(start)); }
        void reset() { start = lv_tick_get(); }
    };

    lv_obj_t* root_ = nullptr;
    lv_obj_t* list_ = nullptr;
    lv_obj_t* panel_ = nullptr;
    lv_obj_t* head_ = nullptr;
    lv_obj_t* body_ = nullptr;
    std::vector<Item> items_;
    std::vector<lv_obj_t*> buttons_;
    Page page_ = Page::Main;

    RouterState st_;
    std::vector<ExitNode> nodes_;
    std::string ts_state_;            // Tailscale's BackendState, or "Missing"
    std::vector<Lease> leases_;
    bool cable_ = false;
    double up_ = 0, down_ = 0;        // bytes per second
    long long last_rx_ = 0, last_tx_ = 0;
    long last_ms_ = 0;
    Clock clock_;

    std::string pending_;             // a mode being turned on, or "off"
    Clock pending_clock_;
    std::shared_ptr<Job> nodes_job_, action_job_;

    bool sim_ = false;                // the simulator: a pretend router
    std::string sim_mode_;
    RouterState sim_state_;
    Clock sim_clock_;

    bool dirty_ = false;
    int n_ = 0;
    int pending_click_ = -1;
};
}  // namespace

std::unique_ptr<App> make_router_app() { return std::make_unique<RouterApp>(); }

}  // namespace zs
