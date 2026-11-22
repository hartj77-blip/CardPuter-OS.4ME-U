// Capture: watch the traffic on the wire and save it. Runs tcpdump, showing a
// live one-line-per-packet summary and, at the same time, writing a .pcap to
// ~/Recordings that you can open in Wireshark on a PC (Sync copies it off).
//
//   Space  start / stop        F  interface (auto / eth0 / wlan0)
//   R      clear the view
#include <sys/stat.h>

#include <ctime>
#include <string>
#include <vector>

#include "apps/common.h"
#include "apps/net/net_run.h"
#include "apps/net/net_sim.h"
#include "apps/net/net_ui.h"

namespace zs {
namespace {
using namespace theme;

class CaptureApp : public App {
public:
    void build(lv_obj_t* root) override {
        root_ = root;
        header_ = label(root_, "", &zs_font_14, kText);
        lv_obj_set_pos(header_, 4, 1);
        hint_ = label(root_, "Space start  F iface  R clear", &zs_font_12, kMuted);
        lv_obj_align(hint_, LV_ALIGN_TOP_RIGHT, -4, 3);
        out_.build(root_, 18);
        out_.set_placeholder("Press Space to start capturing.\nPackets are saved to ~/Recordings.");
        update_header();
    }

    ~CaptureApp() override { job_.cancel(); }

    void tick() override {
        if (!capturing_) return;
        std::vector<std::string> lines;
        if (job_.drain(lines)) {
            for (auto& l : lines) { out_.append(l); ++packets_; }
            update_header();
        }
        if (!job_.running()) {   // tcpdump died (missing, or no permission)
            using net::Done;
            Done d = job_.status();
            if (d == Done::Missing) out_.append("tcpdump is not installed.");
            else if (packets_ == 0) out_.append("Could not start capture (needs root / an interface).");
            stop();
        }
    }

    bool on_key(uint32_t key) override {
        if (key == ' ') { capturing_ ? stop() : start(); return true; }
        if (key == 'r' || key == 'R') { out_.clear(); packets_ = 0; update_header(); return true; }
        if ((key == 'f' || key == 'F') && !capturing_) {
            iface_ = (iface_ + 1) % 3;
            update_header();
            return true;
        }
        return false;
    }

private:
    const char* iface_name() const { return iface_ == 1 ? "eth0" : iface_ == 2 ? "wlan0" : "auto"; }

    std::string new_pcap_path() {
        std::string dir = home_dir() + "/Recordings";
        ::mkdir(dir.c_str(), 0755);
        std::time_t now = std::time(nullptr);
        std::tm tm{};
        localtime_r(&now, &tm);
        char stamp[32];
        std::strftime(stamp, sizeof stamp, "%Y%m%d-%H%M%S", &tm);
        return dir + "/capture-" + stamp + ".pcap";
    }

    void start() {
        out_.clear();
        packets_ = 0;
        pcap_ = net::simulated() ? "~/Recordings/capture-sim.pcap" : new_pcap_path();
        job_.reset();
        capturing_ = true;

        // One tcpdump both prints a summary (--print) and writes the pcap (-w),
        // flushing each packet (-U) so a stop leaves a complete file.
        std::vector<std::string> argv = {"tcpdump", "-n", "-U", "-l", "--print", "-w", pcap_};
        if (iface_ != 0) { argv.push_back("-i"); argv.push_back(iface_name()); }
        job_.start(argv, /*root=*/true, net::sim::tcpdump());
        update_header();
    }

    void stop() {
        job_.cancel();
        capturing_ = false;
        update_header();
        if (packets_ > 0) shell_toast(fmt("Saved %d packets", packets_));
    }

    void update_header() {
        if (capturing_)
            lv_label_set_text(header_, fmt(LV_SYMBOL_PLAY " %d  %s", packets_, iface_name()).c_str());
        else if (packets_ > 0)
            lv_label_set_text(header_, fmt("%d packets  saved", packets_).c_str());
        else
            lv_label_set_text(header_, fmt("Capture  (%s)", iface_name()).c_str());
    }

    lv_obj_t* root_ = nullptr;
    lv_obj_t* header_ = nullptr;
    lv_obj_t* hint_ = nullptr;
    net::OutputView out_;
    net::Job job_;
    std::string pcap_;
    int packets_ = 0;
    int iface_ = 0;
    bool capturing_ = false;
};
}  // namespace

std::unique_ptr<App> make_capture_app() { return std::make_unique<CaptureApp>(); }

}  // namespace zs
