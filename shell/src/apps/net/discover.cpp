// Discover: who is on your network. Sweeps the local subnet with arp-scan
// (works the same on Ethernet or Wi-Fi) and lists every device with its IP,
// hardware vendor and MAC. Enter runs a port scan of the selected host.
//
//   Up/Down  move            Enter  scan the host's ports
//   R        scan again      F      interface (auto / eth0 / wlan0)
#include <algorithm>
#include <string>
#include <vector>

#include "apps/common.h"
#include "apps/net/net_run.h"
#include "apps/net/net_sim.h"

namespace zs {
namespace {
using namespace theme;

struct Host {
    std::string ip, mac, vendor;
    lv_obj_t* row = nullptr;
};

// 192.168.1.42 -> a sortable key (one byte per octet).
// Pad an address so the vendor column lines up (15 = "255.255.255.255").
std::string pad_ip(std::string ip) {
    while (ip.size() < 15) ip += ' ';
    return ip;
}

uint32_t ip_key(const std::string& ip) {
    uint32_t v = 0;
    int oct = 0;
    for (char ch : ip) {
        if (ch == '.') { v = (v << 8) | uint32_t(oct & 0xff); oct = 0; }
        else if (ch >= '0' && ch <= '9') oct = oct * 10 + (ch - '0');
    }
    return (v << 8) | uint32_t(oct & 0xff);
}

class DiscoverApp : public App {
public:
    void build(lv_obj_t* root) override {
        root_ = root;
        title_ = label(root_, "", &zs_font_14, kAccent);
        lv_obj_set_pos(title_, 4, 1);
        hint_ = label(root_, "R rescan  F iface", &zs_font_12, kMuted);
        lv_obj_align(hint_, LV_ALIGN_TOP_RIGHT, -4, 3);

        list_ = make_list(root_, 0, 18, kScreenW, kContentH - 18);
        finish_list(list_);

        msg_ = label(root_, "", &zs_font_14, kMuted);
        lv_obj_set_style_text_align(msg_, LV_TEXT_ALIGN_CENTER, 0);
        lv_obj_align(msg_, LV_ALIGN_CENTER, 0, 6);

        scan();
    }

    void tick() override {
        if (!job_.started()) return;
        std::vector<std::string> lines;
        if (job_.drain(lines)) for (auto& l : lines) add(l);
        update_title();
        if (!job_.running() && !done_) {
            done_ = true;
            if (hosts_.empty()) {
                using net::Done;
                net::Done d = job_.status();
                set_msg(d == Done::Missing   ? "arp-scan is not installed."
                        : d == Done::Failed  ? "Scan failed.\nNeed a network connection?"
                                             : "No devices answered.\nTry F to pick an interface.");
            }
        }
    }

    bool on_key(uint32_t key) override {
        if (key == 'r' || key == 'R') { scan(); return true; }
        if (key == 'f' || key == 'F') {
            iface_ = (iface_ + 1) % 3;   // auto, eth0, wlan0
            scan();
            return true;
        }
        if (key == LV_KEY_ENTER) {
            int i = focused();
            if (i >= 0) shell_open_app("ports", hosts_[size_t(i)].ip);
            return true;
        }
        return false;
    }

private:
    const char* iface_name() const { return iface_ == 1 ? "eth0" : iface_ == 2 ? "wlan0" : ""; }

    void scan() {
        job_.cancel();
        job_.reset();
        hosts_.clear();
        done_ = false;
        lv_obj_clean(list_);
        if (group) lv_group_remove_all_objs(group);
        set_msg("Scanning the network...");
        update_title();

        std::vector<std::string> argv = {"arp-scan", "--quiet", "--ignoredups", "--retry=2"};
        if (iface_ == 0) argv.push_back("--localnet");
        else { argv.push_back("--localnet"); argv.push_back(std::string("--interface=") + iface_name()); }
        job_.start(argv, /*root=*/true, net::sim::arp_scan());
    }

    void add(const std::string& line) {
        // arp-scan: "IP<TAB>MAC<TAB>Vendor". Ignore its header/footer lines.
        size_t t1 = line.find('\t');
        if (t1 == std::string::npos) return;
        size_t t2 = line.find('\t', t1 + 1);
        Host h;
        h.ip = line.substr(0, t1);
        if (h.ip.find('.') == std::string::npos) return;   // not an address line
        h.mac = line.substr(t1 + 1, (t2 == std::string::npos ? line.size() : t2) - t1 - 1);
        h.vendor = t2 == std::string::npos ? "" : line.substr(t2 + 1);
        if (std::any_of(hosts_.begin(), hosts_.end(), [&](const Host& o) { return o.ip == h.ip; })) return;

        set_msg("");
        // "192.168.1.10   Raspberry Pi"  (MAC when the vendor is unknown).
        std::string right = h.vendor.empty() ? h.mac : h.vendor;
        if (right.size() > 22) right = right.substr(0, 21) + "~";
        std::string text = pad_ip(h.ip) + "  " + right;
        h.row = list_button(list_, LV_SYMBOL_WIFI, text.c_str(), nullptr, this);
        hosts_.push_back(h);
        // Keep the list in address order.
        std::sort(hosts_.begin(), hosts_.end(),
                  [](const Host& a, const Host& b) { return ip_key(a.ip) < ip_key(b.ip); });
        finish_list(list_);
    }

    int focused() {
        lv_obj_t* f = group ? lv_group_get_focused(group) : nullptr;
        for (size_t i = 0; i < hosts_.size(); ++i)
            if (hosts_[i].row == f) return int(i);
        return hosts_.empty() ? -1 : 0;
    }

    void update_title() {
        const char* on = iface_ == 0 ? "network" : iface_name();
        lv_label_set_text(title_, fmt("%d device%s on %s", int(hosts_.size()),
                                      hosts_.size() == 1 ? "" : "s", on).c_str());
    }

    void set_msg(const char* m) { lv_label_set_text(msg_, m); }

    lv_obj_t *root_ = nullptr, *title_ = nullptr, *hint_ = nullptr, *list_ = nullptr, *msg_ = nullptr;
    std::vector<Host> hosts_;
    net::Job job_;
    int iface_ = 0;
    bool done_ = false;
};
}  // namespace

std::unique_ptr<App> make_discover_app() { return std::make_unique<DiscoverApp>(); }

}  // namespace zs
