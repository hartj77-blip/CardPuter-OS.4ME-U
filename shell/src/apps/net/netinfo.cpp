// Netinfo: this device on the network. Two pages (Left/Right):
//   Links      every interface (Ethernet, Wi-Fi, USB) with its state and
//              address, the default gateway and the DNS servers in use
//   Listening  the TCP/UDP ports this device is accepting connections on
//
// Gathered in one shot with ip(8) and ss(8); R refreshes.
#include <string>
#include <vector>

#include "apps/common.h"
#include "apps/net/net_run.h"
#include "apps/net/net_sim.h"
#include "apps/net/net_ui.h"

namespace zs {
namespace {
using namespace theme;

std::vector<std::string> split_ws(const std::string& s) {
    std::vector<std::string> out;
    size_t i = 0;
    while (i < s.size()) {
        while (i < s.size() && s[i] == ' ') ++i;
        size_t j = i;
        while (j < s.size() && s[j] != ' ') ++j;
        if (j > i) out.push_back(s.substr(i, j - i));
        i = j;
    }
    return out;
}

class NetinfoApp : public App {
public:
    void build(lv_obj_t* root) override {
        root_ = root;
        tab_ = label(root_, "", &zs_font_12, kMuted);
        lv_obj_set_pos(tab_, 8, 2);
        hint_ = label(root_, "R refresh", &zs_font_12, kMuted);
        lv_obj_align(hint_, LV_ALIGN_TOP_RIGHT, -8, 2);
        out_.build(root_, 18);
        gather();
        render();
    }

    ~NetinfoApp() override { job_.cancel(); }

    void tick() override {
        if (!gathering_) return;
        std::vector<std::string> lines;
        if (job_.drain(lines)) for (auto& l : lines) raw_.push_back(l);
        if (!job_.running()) {
            gathering_ = false;
            parse();
            render();
        }
    }

    bool on_key(uint32_t key) override {
        if (key == LV_KEY_LEFT || key == LV_KEY_RIGHT || key == 9) {
            page_ = 1 - page_;
            render();
            return true;
        }
        if (key == 'r' || key == 'R') { gather(); render(); return true; }
        return false;
    }

private:
    void gather() {
        job_.cancel();
        job_.reset();
        raw_.clear();
        ifaces_.clear();
        gateways_.clear();
        dns_.clear();
        listens_.clear();
        gathering_ = true;
        // One shell run, sections split by "---".
        static const char* kScript =
            "ip -br addr; echo ---; ip route; echo ---; ss -tulnH; echo ---; cat /etc/resolv.conf";
        std::vector<std::string> simv;
        auto addsec = [&](const std::vector<std::string>& v) {
            for (auto& l : v) simv.push_back(l);
            simv.push_back("---");
        };
        addsec(net::sim::ip_addr());
        addsec(net::sim::ip_route());
        addsec(net::sim::ss_listen());
        for (auto& l : net::sim::resolv()) simv.push_back(l);
        job_.start({"sh", "-c", kScript}, /*root=*/false, simv, 0);
    }

    void parse() {
        int sec = 0;
        for (auto& line : raw_) {
            if (line == "---") { ++sec; continue; }
            auto f = split_ws(line);
            if (f.empty()) continue;
            switch (sec) {
            case 0:   // ip -br addr: name  STATE  addr...
                if (f.size() >= 2) {
                    std::string addr = f.size() >= 3 ? f[2] : "";
                    ifaces_.push_back(f[0] + "\t" + f[1] + "\t" + addr);
                }
                break;
            case 1:   // ip route: "default via GW dev IF ..."
                if (f.size() >= 5 && f[0] == "default" && f[1] == "via")
                    gateways_.push_back(f[2] + " (" + f[4] + ")");
                break;
            case 2:   // ss -tulnH: proto state recvq sendq LOCAL PEER
                if (f.size() >= 5) listens_.push_back(f[0] + "\t" + f[4]);
                break;
            case 3:   // resolv.conf: nameserver X
                if (f.size() >= 2 && f[0] == "nameserver") dns_.push_back(f[1]);
                break;
            }
        }
    }

    void render() {
        lv_label_set_text(tab_, page_ == 0 ? "Links   " LV_SYMBOL_RIGHT " Listening"
                                           : LV_SYMBOL_LEFT " Links   Listening");
        out_.clear();
        if (gathering_) { out_.set_placeholder("Reading..."); return; }
        if (page_ == 0) {
            NetStatus n = hal().net();
            if (n.wifi_connected) out_.append("Wi-Fi: " + n.wifi_ssid + fmt("  %d%%", n.wifi_signal));
            if (n.ethernet) out_.append("Ethernet: link up");
            if (!n.tether.empty()) out_.append("Tether: " + n.tether);
            out_.append("");
            out_.append("Interfaces");
            for (auto& i : ifaces_) {
                auto f = split_ws_tab(i);
                out_.append("  " + f[0] + "  " + f[1] + (f[2].empty() ? "" : "  " + f[2]));
            }
            if (!gateways_.empty()) {
                out_.append("");
                for (auto& g : gateways_) out_.append("Gateway: " + g);
            }
            if (!dns_.empty()) {
                std::string line = "DNS:";
                for (auto& d : dns_) line += " " + d;
                out_.append(line);
            }
        } else {
            if (listens_.empty()) { out_.set_placeholder("Nothing is listening."); return; }
            out_.append("Proto  Address");
            for (auto& l : listens_) {
                auto f = split_ws_tab(l);
                out_.append("  " + pad(f[0], 5) + "  " + f[1]);
            }
        }
    }

    static std::vector<std::string> split_ws_tab(const std::string& s) {
        std::vector<std::string> out;
        size_t start = 0, t;
        while ((t = s.find('\t', start)) != std::string::npos) {
            out.push_back(s.substr(start, t - start));
            start = t + 1;
        }
        out.push_back(s.substr(start));
        while (out.size() < 3) out.push_back("");
        return out;
    }
    static std::string pad(std::string s, size_t n) {
        while (s.size() < n) s += ' ';
        return s;
    }

    lv_obj_t* root_ = nullptr;
    lv_obj_t* tab_ = nullptr;
    lv_obj_t* hint_ = nullptr;
    net::OutputView out_;
    net::Job job_;
    std::vector<std::string> raw_, ifaces_, gateways_, dns_, listens_;
    int page_ = 0;
    bool gathering_ = false;
};
}  // namespace

std::unique_ptr<App> make_netinfo_app() { return std::make_unique<NetinfoApp>(); }

}  // namespace zs
