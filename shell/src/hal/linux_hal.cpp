// Real hardware on the Cardputer Zero, through standard Linux interfaces.
//
// Sources (see docs/HARDWARE.md):
//   battery    /sys/class/power_supply/bq27220-0/       (bq27xxx driver)
//   IMU        /sys/bus/iio/devices/iio:deviceN/        (bmi270, bmm150)
//   backlight  /sys/class/backlight/*/                  (pwm_bl_m5stack)
//   key LEDs   /sys/class/leds/*                        (names matched by substring)
//   Wi-Fi      nmcli (NetworkManager), run only when /sys says something changed
//   Bluetooth  bluetoothctl / rfkill; bluetoothd runs only while it's on or used
//   audio      wpctl (PipeWire, which runs only while there is sound; hal/demand.h)
//   IR         ir-ctl (rc-core, gpio-ir-tx / gpio-ir-recv); codes encoded in ir/ir.cpp
//   QR codes   rpicam-vid (preview frames on a pipe)
//   tethering  /usr/bin/zeroos-tether (bluetoothctl + nmcli), run in the background
//   media      sox: `play` for mp3/flac/ogg/wav, `rec` for voice notes, `soxi` for
//              durations. One small process per stream; pause = SIGSTOP/SIGCONT.
//
// Everything degrades gracefully: a missing file means "unknown", never a crash.
// None of this is verified on hardware yet (Phase 1 bring-up).
#include <dirent.h>
#include <fcntl.h>
#ifndef __EMSCRIPTEN__
#include <linux/input.h>   // media keys from a Bluetooth car or headset
#include <sys/ioctl.h>
#include <sys/resource.h>
#endif
#include <signal.h>
#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/statvfs.h>
#include <sys/sysinfo.h>
#include <sys/utsname.h>
#include <sys/wait.h>
#include <unistd.h>

#include <algorithm>
#include <atomic>
#include <cctype>
#include <cerrno>
#include <cmath>
#include <ctime>
#include <cstdio>
#include <climits>
#include <cstdlib>
#include <fstream>
#include <functional>
#include <iomanip>
#include <map>
#include <sstream>
#include <chrono>
#include <condition_variable>
#include <mutex>
#include <thread>

#include "hal/bt_player.h"
#include "hal/demand.h"
#include "hal/hal.h"
#include "hal/lcd_drm.h"
#include "hal/lcd_fb.h"
#include "hal/meshtastic.h"
#include "hal/play_clock.h"
#include "hal/proc_stats.h"
#include "hal/tailscale_up.h"
#include "hal/wifi_adapters.h"
#include "platform/shell_cmd.h"
#include "platform/watchdog.h"
#include "ui/prefs.h"

namespace zs {
namespace {

std::string read_str(const std::string& path) {
    std::ifstream f(path);
    std::string s;
    if (f) std::getline(f, s);
    return s;
}

long read_long(const std::string& path, long dflt = 0) {
    std::string s = read_str(path);
    if (s.empty()) return dflt;
    return std::strtol(s.c_str(), nullptr, 10);
}

double read_double(const std::string& path, double dflt = 0) {
    std::string s = read_str(path);
    if (s.empty()) return dflt;
    return std::strtod(s.c_str(), nullptr);
}

bool write_str(const std::string& path, const std::string& v) {
    std::ofstream f(path);
    if (!f) return false;
    f << v;
    return bool(f);
}

#if ZS_BACKEND_DEVICE
// platform/m5_drm_shim.c as built for this machine (cmake/embed.cmake).
extern "C" {
extern const unsigned char zs_m5_drm_shim[];
extern const unsigned long zs_m5_drm_shim_len;
}
#endif

// The preload library that swaps two DRM cards for an M5Stack app
// (hal/lcd_drm.h), written out of this binary to ~/.cache/zeroos; "" if that
// didn't work, and the app then runs without it, as it did before.
[[maybe_unused]] std::string m5_drm_shim_file() {
#if ZS_BACKEND_DEVICE
    const char* home = std::getenv("HOME");
    std::string cache = std::string(home && *home ? home : "/home/pi") + "/.cache";
    std::string path = cache + "/zeroos/m5-drm-shim.so";
    std::string want(reinterpret_cast<const char*>(zs_m5_drm_shim), zs_m5_drm_shim_len);
    if (lcd_fb_read(path) == want) return path;   // already there, from this build
    ::mkdir(cache.c_str(), 0755);
    ::mkdir((cache + "/zeroos").c_str(), 0755);
    std::string tmp = path + ".tmp";
    {
        std::ofstream f(tmp, std::ios::binary | std::ios::trunc);
        f.write(want.data(), std::streamsize(want.size()));
        f.flush();
        if (!f) return "";
    }
    // Renamed into place: an app that is starting never loads half a file.
    if (::rename(tmp.c_str(), path.c_str()) != 0) return "";
    return path;
#else
    return "";
#endif
}

// The command may take a while (bluetoothctl pair: up to 25 s); run_command
// gives the watchdog room for it.
std::string run(const std::string& cmd) { return run_command(cmd); }

// "ZeroOS 1.2.3 (built 2026-11-20)" -> "1.2.3"; "" if there is no release file.
std::string os_release_version() {
    std::istringstream line(read_str("/etc/zeroos-release"));
    std::string name, version;
    line >> name >> version;
    return version;
}

// A private (0600) temporary file holding data, for a tool to read a secret
// from instead of its command line, where every process can see it. "" on
// failure. The caller removes it.
std::string private_tmp(const std::string& data) {
    char tmp[] = "/tmp/zeroos-secret-XXXXXX";
    int fd = mkstemp(tmp);   // created 0600
    if (fd < 0) return "";
    bool ok = write(fd, data.data(), data.size()) == ssize_t(data.size());
    close(fd);
    if (!ok) {
        unlink(tmp);
        return "";
    }
    return tmp;
}

std::vector<std::string> list_dir(const std::string& path) {
    std::vector<std::string> v;
    if (DIR* d = opendir(path.c_str())) {
        while (dirent* e = readdir(d)) {
            if (e->d_name[0] != '.') v.push_back(e->d_name);
        }
        closedir(d);
    }
    return v;
}

// Find an iio device directory whose "name" contains needle.
std::string find_iio(const std::string& needle) {
    const std::string base = "/sys/bus/iio/devices/";
    for (auto& n : list_dir(base)) {
        std::string dir = base + n + "/";
        if (read_str(dir + "name").find(needle) != std::string::npos) return dir;
    }
    return "";
}

// Start argv as a child in its own process group; returns pid or -1.
pid_t spawn(const std::vector<std::string>& argv) {
#ifdef __EMSCRIPTEN__
    return -1;
#else
    // argv is built before fork: with video playing, libmpv's threads run in
    // this process, and the child may only make async-signal-safe calls.
    std::vector<char*> a;
    for (auto& s : argv) a.push_back(const_cast<char*>(s.c_str()));
    a.push_back(nullptr);
    pid_t pid = fork();
    if (pid == 0) {
        setpgid(0, 0);
        int devnull = open("/dev/null", O_RDWR);
        if (devnull >= 0) { dup2(devnull, 0); dup2(devnull, 1); dup2(devnull, 2); }
        execvp(a[0], a.data());
        _exit(127);
    }
    return pid;
#endif
}

// The same, with the child's output on a pipe that we read without blocking.
pid_t spawn_pipe(const std::vector<std::string>& argv, int& fd) {
    fd = -1;
#ifdef __EMSCRIPTEN__
    return -1;
#else
    int p[2];
    if (pipe(p) < 0) return -1;
    // argv is built before fork: with video playing, libmpv's threads run in
    // this process, and the child may only make async-signal-safe calls.
    std::vector<char*> a;
    for (auto& s : argv) a.push_back(const_cast<char*>(s.c_str()));
    a.push_back(nullptr);
    pid_t pid = fork();
    if (pid == 0) {
        setpgid(0, 0);
        int devnull = open("/dev/null", O_RDWR);
        if (devnull >= 0) { dup2(devnull, 0); dup2(devnull, 2); }
        dup2(p[1], 1);
        close(p[0]);
        close(p[1]);
        execvp(a[0], a.data());
        _exit(127);
    }
    close(p[1]);
    if (pid < 0) {
        close(p[0]);
        return -1;
    }
    fcntl(p[0], F_SETFL, O_NONBLOCK);
    fcntl(p[0], F_SETFD, FD_CLOEXEC);
    fd = p[0];
    return pid;
#endif
}

// One line of "nmcli -t" output split on ':', with its "\:" and "\\"
// escapes undone.
std::vector<std::string> nmcli_fields(const std::string& line) {
    std::vector<std::string> f;
    std::string cur;
    for (size_t i = 0; i < line.size(); ++i) {
        if (line[i] == '\\' && i + 1 < line.size()) { cur += line[++i]; continue; }
        if (line[i] == ':') { f.push_back(cur); cur.clear(); continue; }
        cur += line[i];
    }
    f.push_back(cur);
    return f;
}

// True if the child has exited (and reaps it).
bool exited(pid_t pid) {
#ifdef __EMSCRIPTEN__
    return true;
#else
    if (pid <= 0) return true;
    int st;
    return waitpid(pid, &st, WNOHANG) == pid;
#endif
}

// Stops a child from spawn(): the signal goes to its whole process group
// (spawn() calls setpgid), so "sh -c" wrappers don't leave their own
// children running. Waits at most ~1 s, then SIGKILLs, so a child that
// ignores the signal can't freeze the UI.
void kill_child(pid_t pid, int sig) {
#ifndef __EMSCRIPTEN__
    if (pid <= 0) return;
    auto signal_all = [pid](int s) {
        if (kill(-pid, s) < 0) kill(pid, s);   // no group (setpgid failed): just the child
    };
    signal_all(SIGCONT);   // a stopped process can't act on SIGTERM
    signal_all(sig);
    int st;
    for (int i = 0; i < 100; ++i) {   // 100 x 10 ms
        pid_t r = waitpid(pid, &st, WNOHANG);
        if (r == pid || (r < 0 && errno == ECHILD)) {
            if (sig != SIGKILL) kill(-pid, SIGTERM);   // group members the child left behind
            return;
        }
        usleep(10000);
    }
    signal_all(SIGKILL);
    waitpid(pid, &st, 0);   // SIGKILL can't be ignored: this returns at once
#endif
}

bool has_ext(const std::string& n, std::initializer_list<const char*> exts) {
    std::string lower;
    for (char c : n) lower += char(std::tolower((unsigned char)c));
    for (auto e : exts) {
        std::string x = e;
        if (lower.size() > x.size() && lower.compare(lower.size() - x.size(), x.size(), x) == 0) return true;
    }
    return false;
}

std::string stem(const std::string& name) {
    auto dot = name.rfind('.');
    return dot == std::string::npos ? name : name.substr(0, dot);
}

std::string find_power_supply() {
    const std::string base = "/sys/class/power_supply/";
    for (auto& n : list_dir(base)) {
        if (n.find("bq27") != std::string::npos) return base + n + "/";
    }
    return base + "bq27220-0/";
}

class LinuxHal : public Hal {
public:
    ~LinuxHal() override {
        net_thread_stop();
        scan_stop();
        tether_end();
        gps_stop();
        stop();
        record_stop();
    }

    LinuxHal() {
        bat_ = find_power_supply();
        accel_ = find_iio("bmi270");
        magn_ = find_iio("bmm150");
        for (auto& n : list_dir("/sys/class/backlight/")) {
            backlight_ = "/sys/class/backlight/" + n + "/";
            break;
        }
        // The mesh may still be running from before ZeroShell restarted.
        {
            std::string st = run("systemctl is-active meshtasticd.service 2>/dev/null");
            mesh_wanted_ = st.rfind("active", 0) == 0 || st.rfind("activating", 0) == 0;
            if (mesh_wanted_) mesh_log_checks_ = 0;   // look for "no radio" as if it just started
        }
        find_ext5v();
        demand_init();
#ifndef __EMSCRIPTEN__
        net_thread_ = std::thread([this] { net_thread_main(); });
#endif
    }

    // Services started when needed (hal/demand.h, docs/PERFORMANCE.md). Only
    // on ZeroOS: the same program run on a PC must never stop that PC's sound
    // or Bluetooth. Either can be turned off in settings.conf
    // (bluetooth.on_demand=0, audio.on_demand=0): the service then stays as
    // the system started it. ZS_SYSROOT, ZS_DEMAND and ZS_DEMAND_GRACE are for
    // the tests (shell/tests/device/demand_test.py).
    void demand_init() {
        if (const char* r = std::getenv("ZS_SYSROOT")) sys_root_ = r;
        const char* force = std::getenv("ZS_DEMAND");
        bool zeroos = force ? std::atoi(force) != 0 : !os_release_version().empty();
        demand_bt_ = zeroos && prefs::get_int("bluetooth.on_demand", 1) != 0;
        demand_audio_ = zeroos && prefs::get_int("audio.on_demand", 1) != 0;
        if (const char* g = std::getenv("ZS_DEMAND_GRACE")) demand_grace_s_ = std::max(1, std::atoi(g));
        bt_keeper_ = demand::Keeper(demand_grace_s_);
        audio_keeper_ = demand::Keeper(demand_grace_s_);
        // What the volume was when PipeWire last ran: shown, and changed,
        // without starting it. 60%: what a new image's speaker is set to.
        vol_saved_ = std::clamp(prefs::get_int("audio.volume", 60), 0, 100);
        mute_saved_ = prefs::get_int("audio.muted", 0) != 0;
        vol_cache_ = vol_saved_;
        mute_cache_ = mute_saved_;
    }
    static long steady_s() {
        return long(std::chrono::duration_cast<std::chrono::seconds>(
                        std::chrono::steady_clock::now().time_since_epoch()).count()) + 100000;   // never near 0
    }

    // Expansion 5 V: LED-class supplies (ext_5v_out on M5's image; the
    // Grove port's, if the kernel exposes it the same way).
    void find_ext5v() {
        ext5v_.clear();
        for (auto& n : list_dir("/sys/class/leds/")) {
            std::string l = n;
            std::transform(l.begin(), l.end(), l.begin(), ::tolower);
            if (l.find("5v") != std::string::npos) ext5v_.push_back("/sys/class/leds/" + n + "/");
        }
    }

    void poll() override {
        // Nobody has asked for the position for 5 s: let gpsd go idle.
        if (gps_fd_ >= 0 && time(nullptr) - gps_want_ > 5) gps_stop();
        mesh_pump();
        if (visible_pid_ > 0 && exited(visible_pid_)) visible_pid_ = -1;   // reap the pairing window
        if (pair_pid_ > 0 && exited(pair_pid_)) pair_pid_ = -1;
        // Nobody has looked through the camera for 3 s: turn it off.
        if (scan_pid_ > 0 && time(nullptr) - scan_want_ > 3) scan_stop();
        tether_pump();
        // Battery and IMU are cheap sysfs reads; cache them per poll.
        bat_cache_ = read_battery();
        imu_cache_ = read_imu();
        {
            std::lock_guard<std::mutex> lock(net_mu_);
            if (net_new_) net_cache_ = net_shared_;
            net_new_ = false;
        }
        // For the status thread: sound of ours that PipeWire must stay up for
        // (music, also paused; a recording; the desktop on a monitor).
        audio_hold_ = play_pid_ > 0 || rec_pid_ > 0 || dock_cache_;
        audio_save();
    }

    // The volume as last seen goes to settings.conf, so it is known after a
    // restart with PipeWire stopped. Once it has stood for 3 s: a held volume
    // key is one write to the card, not twenty.
    void audio_save() {
        int v = vol_cache_;
        bool m = mute_cache_;
        if (v == vol_saved_ && m == mute_saved_) {
            vol_changed_at_ = 0;
            return;
        }
        time_t now = time(nullptr);
        if (!vol_changed_at_ || v != vol_seen_ || m != mute_seen_) {
            vol_changed_at_ = now;
            vol_seen_ = v;
            mute_seen_ = m;
            return;
        }
        if (now - vol_changed_at_ < 3) return;
        if (v != vol_saved_) prefs::set_int("audio.volume", v);
        if (m != mute_saved_) prefs::set_int("audio.muted", m);
        vol_saved_ = v;
        mute_saved_ = m;
        vol_changed_at_ = 0;
    }

    BatteryInfo battery() override { return bat_cache_; }
    ImuSample imu() override { return imu_cache_; }
    NetStatus net() override { return net_cache_; }

    // The status thread. read_net() runs nmcli and bluetoothctl, up to a
    // second together on this CPU: on the UI thread that froze the screen and
    // the keyboard, so it has its own thread and poll() picks up the result.
    //
    // It used to run them every 2 s, screen on or off, which kept the CPU
    // from ever resting. Now a fingerprint of /sys is taken every 2 s (a few
    // dozen small reads, no program started), and read_net() runs when that
    // changed, when the UI asks (it just changed something), twice more after
    // either (a network takes a moment to settle), and every 30 s while the
    // screen is on, for the signal strength, which changes with no event.
    void net_thread_main() {
        using clock = std::chrono::steady_clock;
        std::string last_fp;
        clock::time_point last_read{};
        int settle = 0;
        bool asked = true;   // the first pass reads
        for (;;) {
            // Services first: Bluetooth just switched on starts bluetoothd
            // here, and the status read below already finds it.
            bool acted = demand_step();
            std::vector<demand::Addr> addrs = demand::addresses();
            std::string fp = demand::net_fingerprint(sys_root_, addrs);
            bool changed = asked || acted || fp != last_fp;
            if (changed) settle = 2;
            bool refresh = screen_on_ && clock::now() - last_read >= std::chrono::seconds(30);
            if (changed || settle > 0 || refresh) {
                if (!changed && settle > 0) --settle;
                last_fp = fp;
                NetStatus n = read_net(sys_root_, addrs);
                last_read = clock::now();
                std::lock_guard<std::mutex> lock(net_mu_);
                net_shared_ = n;
                net_new_ = true;
            }
            std::unique_lock<std::mutex> lock(net_mu_);
            net_cv_.wait_for(lock, std::chrono::seconds(2), [this] { return net_stop_ || net_now_; });
            if (net_stop_) return;
            asked = net_now_;
            net_now_ = false;
        }
    }
    // A pause on the status thread that ends at once when ZeroShell stops.
    // False if it is stopping.
    bool net_thread_pause(int ms) {
        std::unique_lock<std::mutex> lock(net_mu_);
        net_cv_.wait_for(lock, std::chrono::milliseconds(ms), [this] { return net_stop_; });
        return !net_stop_;
    }

    // --- Services that run only while needed (hal/demand.h). On the status
    // thread, every 2 s. True if bluetoothd was started or stopped (the
    // status then shows other connections).
    bool demand_step() {
        if (!demand_bt_ && !demand_audio_) return false;
        using Act = demand::Keeper::Act;
        long now = steady_s();
        bool acted = false;
        bool bt_on = demand::rfkill_on(sys_root_, "bluetooth");
        std::vector<bool> up = demand::procs_running(sys_root_, {"bluetoothd", "pipewire"});   // one look at /proc
        if (demand_bt_) {
            // bluetoothd: while the radio is on, and for a while after
            // ZeroShell last asked it something (the paired list, with
            // Bluetooth off).
            bool running = up[0];
            bool needed = bt_on || now - bt_used_ < demand_grace_s_;
            Act a = bt_keeper_.step(now, running, needed, bt_on);
            if (a == Act::Start) run("systemctl start bluetooth.service");
            if (a == Act::Stop) {
                std::fprintf(stderr, "zeroshell: Bluetooth is off: stopping bluetoothd\n");
                run("systemctl stop bluetooth.service");
            }
            acted = a != Act::None;
        }
        if (demand_audio_) {
            // PipeWire: while a sound device is open, while ZeroShell plays or
            // records, for a while after the volume was set, and while
            // Bluetooth is on (a headset or a phone finds no audio on this
            // side unless WirePlumber is there before it connects). Anything
            // that makes a sound starts it through its socket; only for
            // Bluetooth and for a volume change do we start it ourselves.
            bool running = up[1];
            bool apply = audio_apply_;
            bool needed = bt_on || apply || audio_hold_ || now - audio_used_ < demand_grace_s_ ||
                          demand::pcm_open(sys_root_);
            Act a = audio_keeper_.step(now, running, needed, bt_on || apply);
            if (a == Act::Start && !apply) run("systemctl --user start pipewire.service");
            if (apply) {
                audio_apply();
                running = true;
            } else if (running && !audio_learned_) {
                audio_learn(audio_seq_);   // running since before ZeroShell started: at what volume?
            }
            audio_learned_ = true;
            if (a == Act::Stop) {
                if (audio_stop()) running = false;
                else audio_keeper_.keep();
            }
            audio_up_ = running;
        }
        return acted;
    }
    // The volume as PipeWire has it, into the cache, unless it was set here
    // since `seq` (that one is newer). False if it was.
    bool audio_learn(unsigned seq) {
        int pct = 0;
        bool muted = false;
        bool known = demand::parse_wpctl_volume(run("wpctl get-volume @DEFAULT_AUDIO_SINK@"), pct, muted);
        std::lock_guard<std::mutex> lock(audio_mu_);
        if (audio_seq_ != seq) return false;
        if (known) {
            vol_cache_ = pct;
            mute_cache_ = muted;
        }
        return true;
    }
    // The volume or mute was set while PipeWire was stopped: start it, wait
    // until WirePlumber has found the speaker (a few seconds on this CPU),
    // and set what was asked. The newest values are used, so presses made
    // while this waited aren't lost.
    void audio_apply() {
        run("systemctl --user start pipewire.service");
        int pct = 0;
        bool muted = false;
        for (int i = 0; i < 40; ++i) {
            if (demand::parse_wpctl_volume(run("wpctl get-volume @DEFAULT_AUDIO_SINK@"), pct, muted)) break;
            if (!net_thread_pause(300)) return;
        }
        for (int i = 0; i < 5 && audio_apply_.exchange(false); ++i) {
            run("wpctl set-volume @DEFAULT_AUDIO_SINK@ " + std::to_string(int(vol_cache_)) + "%");
            run(std::string("wpctl set-mute @DEFAULT_AUDIO_SINK@ ") + (mute_cache_ ? "1" : "0"));
        }
        audio_used_ = steady_s();
    }
    // Nothing has used PipeWire for a while. Three last looks, then stop it;
    // its sockets stay, and the next program that makes a sound starts it
    // again. False if it is left running.
    bool audio_stop() {
        unsigned seq = audio_seq_;
        // Its sockets must be systemd's, or nothing could start it again.
        if (audio_no_socket_) return false;
        if (run("systemctl --user is-active pipewire.socket").rfind("active", 0) != 0) {
            std::fprintf(stderr, "zeroshell: pipewire.socket isn't active: PipeWire is left running\n");
            audio_no_socket_ = true;
            return false;
        }
        // A program with a stream open, even a silent one (a paused game),
        // would lose its sound for good.
        std::string nodes = run("pw-cli ls Node");
        if (nodes.empty() || demand::has_streams(nodes)) return false;
        // The volume it had, for volume() while it is stopped. Set just now:
        // in use after all.
        if (!audio_learn(seq)) return false;
        std::fprintf(stderr, "zeroshell: no sound for a while: stopping PipeWire\n");
        run("systemctl --user stop pipewire-pulse.service wireplumber.service pipewire.service");
        audio_up_ = false;
        // The volume was set while it stopped: that went to a PipeWire that
        // was on its way out. Set it again (next pass, which starts it).
        if (audio_seq_ != seq) {
            audio_apply_ = true;
            net_refresh_soon();
        }
        return true;
    }

    void net_refresh_soon() {
        std::lock_guard<std::mutex> lock(net_mu_);
        net_now_ = true;
        net_cv_.notify_one();
    }
    void net_thread_stop() {
        {
            std::lock_guard<std::mutex> lock(net_mu_);
            net_stop_ = true;
            net_cv_.notify_one();
        }
        if (net_thread_.joinable()) net_thread_.join();
    }

    SystemInfo system() override {
        SystemInfo s;
        char host[64] = {0};
        gethostname(host, sizeof host - 1);
        s.hostname = host;
        utsname u{};
        if (uname(&u) == 0) s.kernel = u.release;
        s.model = accel_.empty() ? "CardputerZero Lite" : "CardputerZero";
        // The image and every update write the release file; without one
        // (ZeroShell copied onto another image) say the shell's own version.
        std::string rel = os_release_version();
        s.os_version = "ZeroOS " + (rel.empty() ? std::string(ZS_VERSION) : rel);
        s.cpu_temp_c = int(read_long("/sys/class/thermal/thermal_zone0/temp") / 1000);
#ifndef __EMSCRIPTEN__  // the web demo always uses MockHal
        struct sysinfo si{};
        if (sysinfo(&si) == 0) s.uptime_s = si.uptime;
#endif
        read_mem_cpu(s, cpu_prev_);
        // Scanning every process costs a little; refresh the list every few calls.
        if (top_.empty() || ++top_calls_ % 3 == 0) top_ = top_processes(5);
        s.top_procs = top_;
        struct statvfs vfs{};
        if (statvfs("/", &vfs) == 0) {
            double total = double(vfs.f_blocks) * vfs.f_frsize;
            double avail = double(vfs.f_bavail) * vfs.f_frsize;
            s.disk_total_gb10 = int(total / 1e8);
            s.disk_used_gb10 = int((total - avail) / 1e8);
        }
        return s;
    }

    std::vector<WifiAdapter> wifi_adapters() override { return wifi::find_adapters(sys_root_); }
    std::string wifi_adapter() override { return prefs::get_str("wifi_adapter", ""); }
    void set_wifi_adapter(const std::string& id) override { prefs::set_str("wifi_adapter", id); }

    // The adapter chosen in Settings > Wi-Fi, if it's plugged in. Not plugged
    // in (a USB adapter taken out): automatic, so Wi-Fi still works.
    bool chosen_adapter(WifiAdapter& out) {
        std::string id = wifi_adapter();
        if (id.empty()) return false;
        std::vector<WifiAdapter> v = wifi_adapters();
        const WifiAdapter* a = wifi::pick(v, id);
        if (!a) return false;
        out = *a;
        return true;
    }
    // " ifname wlan1" for nmcli when an adapter is chosen, "" for automatic.
    std::string ifname_arg() {
        WifiAdapter a;
        return chosen_adapter(a) ? " ifname " + shell_quote(a.iface) : "";
    }

    std::vector<WifiNetwork> wifi_scan() override {
        std::vector<WifiNetwork> v;
        std::istringstream in(run("nmcli -t -f IN-USE,SSID,SIGNAL,SECURITY dev wifi list --rescan auto" + ifname_arg()));
        std::string line;
        while (std::getline(in, line)) {
            // IN-USE:SSID:SIGNAL:SECURITY (SSID colons are escaped as \:)
            std::vector<std::string> f = nmcli_fields(line);
            if (f.size() < 4 || f[1].empty()) continue;
            WifiNetwork n;
            n.connected = f[0] == "*";
            n.ssid = f[1];
            n.signal = std::atoi(f[2].c_str());
            n.secure = !f[3].empty() && f[3] != "--";
            // One row per network: nmcli lists each access point (a mesh, or
            // 2.4 and 5 GHz), and joining goes by name anyway.
            auto same = std::find_if(v.begin(), v.end(), [&](const WifiNetwork& o) { return o.ssid == n.ssid; });
            if (same == v.end()) {
                v.push_back(n);
            } else {
                same->connected = same->connected || n.connected;
                same->signal = std::max(same->signal, n.signal);
            }
        }
        // nmcli sorts by signal, but merging can raise one: the connected one
        // first, then strongest.
        std::stable_sort(v.begin(), v.end(), [](const WifiNetwork& a, const WifiNetwork& b) {
            return a.connected != b.connected ? a.connected : a.signal > b.signal;
        });
        return v;
    }

    // The SECURITY column of the last scan for this network ("WPA2", "WPA3",
    // "WEP", "WPA2 802.1X"...); "" if it wasn't seen (a hidden network).
    std::string wifi_security(const std::string& ssid) {
        std::istringstream in(run("nmcli -t -f SSID,SECURITY dev wifi list --rescan no" + ifname_arg()));
        for (std::string line; std::getline(in, line);) {
            std::vector<std::string> f = nmcli_fields(line);
            if (f.size() >= 2 && f[0] == ssid && !f[1].empty() && f[1] != "--") return f[1];
        }
        return "";
    }
    bool wifi_connect(const std::string& ssid, const std::string& psk) override {
        // nmcli has no "--": a name starting with '-' could be read as one of
        // its options (-a is --ask). Such names are refused.
        if (ssid.empty() || ssid[0] == '-') return false;
        if (psk.find_first_of("\r\n") != std::string::npos) return false;
        std::string id = shell_quote(ssid);
        // An adapter chosen in Settings: the network is joined on it, and its
        // profile is tied to that adapter's address, so it reconnects there
        // (and not on the other one). Automatic: tied to none.
        WifiAdapter ad;
        bool chosen = chosen_adapter(ad);
        std::string ifname = chosen ? " ifname " + shell_quote(ad.iface) : "";
        std::string bind_mac = chosen ? shell_quote(ad.mac) : std::string("''");
        if (psk.empty()) {
            // A saved network (its own password), or an open one. A saved
            // profile tied to the other adapter is moved to this one first,
            // or NetworkManager wouldn't use it here.
            run("nmcli connection modify id " + id + " 802-11-wireless.mac-address " + bind_mac);
            // --wait bounds the attempt (default 90 s): this runs on the UI thread.
            if (run("nmcli --wait 20 dev wifi connect " + id + ifname + " && echo OK").find("OK") == std::string::npos)
                return false;
            net_refresh_soon();
            return true;
        }
        // With a password: never on nmcli's command line, where every process
        // can read it. It goes in a private file (passwd-file) for "connection
        // up", into a new profile under a temporary name, so a mistyped
        // password never touches a profile that works: on failure only the
        // new one is deleted and nothing of it stays saved (NetworkManager
        // would keep retrying it, M5 launcher fix #62). On success it takes
        // the network's name and replaces the older profile.
        std::string sec = wifi_security(ssid);
        if (sec.find("802.1X") != std::string::npos) return false;   // enterprise: needs more than a password
        bool wep = sec.find("WEP") != std::string::npos;
        bool sae_only = sec.find("WPA3") != std::string::npos && sec.find("WPA2") == std::string::npos &&
                        sec.find("WPA1") == std::string::npos;
        std::string tmp_name = shell_quote("zeroos-joining-" + std::to_string(getpid()));
        std::string add = "nmcli connection add type wifi con-name " + tmp_name + " ssid " + id +
                          " wifi-sec.key-mgmt " + (wep ? "none wifi-sec.wep-key-type key" : sae_only ? "sae" : "wpa-psk");
        if (sec.empty()) add += " 802-11-wireless.hidden yes";   // not in the scan: maybe hidden
        if (chosen && !ad.mac.empty()) add += " 802-11-wireless.mac-address " + bind_mac;
        run("nmcli connection delete id " + tmp_name);   // a leftover from an attempt cut short
        if (run(add + " >/dev/null && echo OK").find("OK") == std::string::npos) return false;
        std::string pf = private_tmp(std::string(wep ? "802-11-wireless-security.wep-key0:"
                                                     : "802-11-wireless-security.psk:") + psk + "\n");
        bool ok = !pf.empty() && run("nmcli --wait 20 connection up id " + tmp_name + ifname + " passwd-file " +
                                     shell_quote(pf) + " && echo OK").find("OK") != std::string::npos;
        if (!pf.empty()) unlink(pf.c_str());
        if (!ok) {
            run("nmcli connection delete id " + tmp_name);
            return false;
        }
        for (int i = 0; i < 8 && !trim_nl(run("nmcli -g connection.id connection show id " + id)).empty(); ++i)
            run("nmcli connection delete id " + id);
        run("nmcli connection modify id " + tmp_name + " connection.id " + id);
        net_refresh_soon();
        return true;
    }
    static std::string trim_nl(std::string s) {
        while (!s.empty() && (s.back() == '\n' || s.back() == '\r')) s.pop_back();
        return s;
    }
    void set_wifi_enabled(bool on) override {
        // A soft rfkill block (left by another tool or app) keeps the radio off
        // even when NetworkManager says Wi-Fi is on, and scans find nothing.
        if (on) run("rfkill unblock wifi");
        run(std::string("nmcli radio wifi ") + (on ? "on" : "off"));
        net_refresh_soon();
    }
    void set_bt_enabled(bool on) override {
        run(std::string("rfkill ") + (on ? "unblock" : "block") + " bluetooth");
        net_refresh_soon();   // bluetoothd and PipeWire start with it (demand_step)
    }

    int brightness() override {
        long max = read_long(backlight_ + "max_brightness", 0);
        if (max <= 0) return 100;
        return int(read_long(backlight_ + "brightness") * 100 / max);
    }
    void set_brightness(int pct) override {
        long max = read_long(backlight_ + "max_brightness", 0);
        if (max <= 0) return;
        if (pct < 5) pct = 5;  // never fully dark from the slider
        write_str(backlight_ + "brightness", std::to_string(max * pct / 100));
    }
    bool has_backlight() override { return !backlight_.empty(); }
    void set_screen_on(bool on) override {
        // bl_power: 0 = on, 4 = off. fb blank 4 = powerdown (panel sleep).
        if (!backlight_.empty()) write_str(backlight_ + "bl_power", on ? "0" : "4");
        write_str("/sys/class/graphics/" + lcd_fb_sysfs_name() + "/blank", on ? "0" : "4");
        // Screen off: the status thread stops refreshing what nobody sees
        // (the signal strength). Back on: fresh at once.
        screen_on_ = on;
        if (on) net_refresh_soon();
    }
    void set_cpu_mode(CpuMode mode) override {
        // All four cores share one policy. zeroshell.service makes these
        // files writable by the video group at start (see zeroos-power-perms).
        const std::string p = "/sys/devices/system/cpu/cpufreq/policy0/";
        std::string govs = read_str(p + "scaling_available_governors");
        auto has = [&](const char* g) { return (" " + govs + " ").find(std::string(" ") + g + " ") != std::string::npos; };
        const char* dynamic = has("schedutil") ? "schedutil" : has("ondemand") ? "ondemand" : "conservative";
        long lo = read_long(p + "cpuinfo_min_freq"), hi = read_long(p + "cpuinfo_max_freq");
        if (hi <= 0) return;
        std::string gov;
        long cap = hi;
        switch (mode) {
        case CpuMode::Saver: gov = dynamic; cap = pick_freq(p, lo + (hi - lo) * 7 / 10); break;
        case CpuMode::Balanced: gov = dynamic; break;
        case CpuMode::Performance: gov = "performance"; break;
        case CpuMode::ScreenOff: gov = has("powersave") ? "powersave" : dynamic; cap = lo; break;
        }
        // Lower the cap before switching to a faster governor, never above it.
        write_str(p + "scaling_max_freq", std::to_string(cap));
        write_str(p + "scaling_governor", gov);
    }

    // Volume and mute. With PipeWire stopped (it only runs while there is
    // sound: demand_step), asking would start it, so the answer is what it
    // was when it last ran, and a change is set by the status thread, which
    // starts PipeWire and waits for the speaker without holding up the UI.
    bool audio_live() { return !demand_audio_ || (audio_up_ && !audio_apply_); }
    // Asks PipeWire, at most every 5 s: the status bar wants to know about
    // mute every second, and each answer is a program started on the UI
    // thread. What is set here is known at once; only a change made
    // elsewhere (a headset's own buttons, the desktop) shows up to 5 s late.
    void audio_read() {
        auto now = std::chrono::steady_clock::now();
        if (now - vol_read_at_ < std::chrono::seconds(5)) return;
        vol_read_at_ = now;
        int pct = 0;
        bool muted = false;
        if (!demand::parse_wpctl_volume(run("wpctl get-volume @DEFAULT_AUDIO_SINK@"), pct, muted)) return;
        std::lock_guard<std::mutex> lock(audio_mu_);
        vol_cache_ = pct;   // "Volume: 0.60 [MUTED]"
        mute_cache_ = muted;
        vol_known_ = true;
    }
    // vol_cache_ or mute_cache_ was just given a new value: make it so.
    void audio_set(int pct, int mute, const std::string& now_cmd) {
        {
            std::lock_guard<std::mutex> lock(audio_mu_);
            if (pct >= 0) vol_cache_ = pct;
            if (mute >= 0) mute_cache_ = mute != 0;
            ++audio_seq_;
        }
        audio_used_ = steady_s();
        if (audio_live()) {
            run(now_cmd);
            vol_read_at_ = std::chrono::steady_clock::now();   // it is what was just set
            return;
        }
        audio_apply_ = true;
        net_refresh_soon();
    }
    int volume() override {
        if (audio_live()) audio_read();
        // No PipeWire and no wpctl (a PC): 50, as before.
        return demand_audio_ || vol_known_ ? int(vol_cache_) : 50;
    }
    void set_volume(int pct) override {
        pct = std::clamp(pct, 0, 100);
        audio_set(pct, -1, "wpctl set-volume @DEFAULT_AUDIO_SINK@ " + std::to_string(pct) + "%");
    }
    bool muted() override {
        if (audio_live()) audio_read();
        return (demand_audio_ || vol_known_) && mute_cache_;
    }
    void set_muted(bool m) override {
        audio_set(-1, m, std::string("wpctl set-mute @DEFAULT_AUDIO_SINK@ ") + (m ? "1" : "0"));
    }

    // --- media
    std::vector<Track> music_library() override {
        std::vector<Track> v;
        std::string root = home() + "/Music";
        // Artist/Album/Disc/track: look a few folders down. The artist is the
        // folder right under ~/Music.
        std::function<void(const std::string&, const std::string&, int)> add_dir =
            [&](const std::string& dir, const std::string& artist, int depth) {
                for (auto& n : list_dir(dir)) {
                    if (n[0] == '.') continue;
                    std::string full = dir + "/" + n;
                    if (has_ext(n, {".mp3", ".flac", ".ogg", ".wav", ".m4a", ".opus"})) {
                        v.push_back({full, stem(n), artist, cached_duration(full)});
                        continue;
                    }
                    struct stat st{};
                    if (depth < 4 && stat(full.c_str(), &st) == 0 && S_ISDIR(st.st_mode))
                        add_dir(full, depth == 0 ? n : artist, depth + 1);
                }
            };
        add_dir(root, "Music", 0);
        std::sort(v.begin(), v.end(), [](const Track& a, const Track& b) {
            return a.artist == b.artist ? a.title < b.title : a.artist < b.artist;
        });
        return v;
    }

    bool play(const std::string& path) override {
        stop();
        video_.close();
        play_pid_ = spawn({"play", "-q", path});
        if (play_pid_ <= 0) return false;
        pb_ = PlaybackState{};
        pb_.path = path;
        pb_.playing = true;
        int d = duration_of(path);
        pb_.dur_ms = d >= 0 ? d * 1000 : -1;
        clock_.start();
        return true;
    }
    bool play_stream(const std::string& url, const std::string& auth, int duration_s) override {
        stop();
        video_.close();
        if (url.rfind("http://", 0) != 0 && url.rfind("https://", 0) != 0) return false;
        // mpv, not sox: it streams over HTTP and plays whatever the server sends.
        std::vector<std::string> argv = {"mpv", "--no-video", "--no-terminal", "--really-quiet", "--no-config",
                                         "--cache=yes", "--demuxer-max-bytes=16MiB"};
        // -append takes one header as is. --http-header-fields is a list split
        // at commas, and Jellyfin's header is full of them: the token never
        // arrived and every stream got 401. It goes in a file only we can
        // read (--include works with --no-config), not on the command line.
        if (!auth.empty()) {
            std::string conf = stream_header_file(auth);
            if (conf.empty()) return false;
            argv.push_back("--include=" + conf);
        }
        argv.push_back("--");
        argv.push_back(url);
        play_pid_ = spawn(argv);
        if (play_pid_ <= 0) return false;
        pb_ = PlaybackState{};
        pb_.path = url;
        pb_.playing = true;
        pb_.dur_ms = duration_s >= 0 ? duration_s * 1000 : -1;
        clock_.start();
        return true;
    }
    bool jellyfin_login(const std::string& server, const std::string& user, const std::string& password,
                        const std::string& device_id, JfSession& out, std::string& error) override {
        return jf::login(server, user, password, device_id, out, error);
    }
    bool jellyfin_list(const JfSession& s, JfView view, const std::string& arg, std::vector<JfItem>& out,
                       std::string& error) override {
        return jf::list(s, view, arg, out, error);
    }
    void jellyfin_report(const JfSession& s, JfReport what, const std::string& item_id,
                         const std::string& play_session, int pos_s, bool paused) override {
        jf::report(s, what, item_id, play_session, pos_s, paused);
    }
    bool video_open(const std::string& url, const std::string& auth, int start_s, int w, int h,
                    std::string& error) override {
        stop();   // one thing on the speaker at a time
        if (url.rfind("http://", 0) != 0 && url.rfind("https://", 0) != 0) {
            error = "Not a video address";
            return false;
        }
        return video_.open(url, auth, start_s, w, h, error);
    }
    bool video_frame(uint16_t* rgb565) override { return video_.frame(rgb565); }
    VideoState video() override { return video_.state(); }
    void video_pause(bool paused) override { video_.set_paused(paused); }
    void video_seek(int seconds, bool absolute) override { video_.seek(seconds, absolute); }
    void video_close() override { video_.close(); }
    void set_paused(bool p) override {
        if (play_pid_ <= 0) return;
#ifndef __EMSCRIPTEN__
        kill(play_pid_, p ? SIGSTOP : SIGCONT);
#endif
        pb_.paused = p;
        pb_.playing = !p;
        if (p) clock_.pause(); else clock_.resume();
    }
    void stop() override {
        kill_child(play_pid_, SIGTERM);
        play_pid_ = -1;
        pb_ = PlaybackState{};
    }
    PlaybackState playback() override {
        if (play_pid_ > 0) {
            pb_.pos_ms = int(clock_.ms());
            if (!pb_.paused && exited(play_pid_)) {
                play_pid_ = -1;
                pb_ = PlaybackState{};
                pb_.finished = true;
            }
        }
        return pb_;
    }

    std::vector<Track> recordings() override {
        std::vector<Track> v;
        std::string dir = home() + "/Recordings";
        for (auto& n : list_dir(dir)) {
            if (!has_ext(n, {".wav"})) continue;
            std::string full = dir + "/" + n;
            struct stat st{};
            stat(full.c_str(), &st);
            char when[32] = "";
            tm lt{};
            localtime_r(&st.st_mtime, &lt);
            strftime(when, sizeof when, "%d %b %H:%M", &lt);
            // 16 kHz mono 16-bit: 32000 bytes per second after the 44-byte header.
            v.push_back({full, n, when, int(st.st_size > 44 ? (st.st_size - 44) / 32000 : 0)});
        }
        std::sort(v.begin(), v.end(), [](const Track& a, const Track& b) { return a.path > b.path; });
        return v;
    }
    bool record_start() override {
        if (rec_pid_ > 0) return false;
        stop();
        std::string dir = home() + "/Recordings";
        mkdir(dir.c_str(), 0755);
        char name[64];
        time_t now = time(nullptr);
        tm lt{};
        localtime_r(&now, &lt);
        strftime(name, sizeof name, "/note-%Y%m%d-%H%M%S.wav", &lt);
        rec_ = RecordState{};
        rec_.path = dir + name;
        rec_pid_ = spawn({"rec", "-q", "-c", "1", "-r", "16000", "-b", "16", rec_.path});
        if (rec_pid_ <= 0) return false;
        rec_.recording = true;
        rec_clock_.start();
        return true;
    }
    void record_stop() override {
        kill_child(rec_pid_, SIGINT);  // SIGINT lets sox finish the WAV header
        rec_pid_ = -1;
        rec_ = RecordState{};
    }
    RecordState record_state() override {
        if (rec_pid_ > 0 && exited(rec_pid_)) {  // rec died (no mic?)
            rec_pid_ = -1;
            rec_ = RecordState{};
        }
        if (rec_.recording) rec_.elapsed_ms = int(rec_clock_.ms());
        return rec_;
    }
    bool delete_recording(const std::string& path) override {
        // Only our own files: resolve "..", symlinks first.
        char real[PATH_MAX], dir[PATH_MAX];
        if (!realpath(path.c_str(), real) || !realpath((home() + "/Recordings").c_str(), dir)) return false;
        if (std::string(real).rfind(std::string(dir) + "/", 0) != 0) return false;
        if (pb_.path == path) stop();
        return std::remove(path.c_str()) == 0;
    }

    void set_led(Led led, bool on) override {
        static const char* keys[] = {"shift", "fn", "sym", "key_led1", "key_led2", "key_led3"};
        const char* key = keys[int(led)];
        for (auto& n : list_dir("/sys/class/leds/")) {
            std::string lower;
            for (char c : n) lower += char(std::tolower((unsigned char)c));
            if (lower.find(key) != std::string::npos) {
                write_str("/sys/class/leds/" + n + "/brightness", on ? "1" : "0");
            }
        }
    }

    // IR: rc-core gives one /dev/lircN per direction (gpio-ir-tx sends,
    // gpio-ir-recv receives); ask ir-ctl which is which.
    std::string ir_device(bool send) {
        std::string& cached = send ? ir_tx_ : ir_rx_;
        if (!cached.empty()) return cached;
        for (auto& n : list_dir("/dev/")) {
            if (n.rfind("lirc", 0) != 0) continue;
            std::string dev = "/dev/" + n;
            std::string f = run("ir-ctl -d " + dev + " --features");
            if (f.find(send ? "can send" : "can receive") != std::string::npos) return cached = dev;
        }
        return "";
    }

    bool ir_send(const ir::Wave& w) override {
        std::string dev = ir_device(true);
        if (dev.empty() || w.timings.empty() || w.timings.size() > 1023) return false;
        char path[] = "/tmp/zeroos-ir-XXXXXX";
        int fd = mkstemp(path);
        if (fd < 0) return false;
        std::string body;
        for (size_t i = 0; i < w.timings.size(); ++i)
            body += (i % 2 ? "space " : "pulse ") + std::to_string(w.timings[i]) + "\n";
        bool wrote = write(fd, body.data(), body.size()) == ssize_t(body.size());
        close(fd);
        std::string out;
        if (wrote)
            out = run("ir-ctl -d " + dev + " -c " + std::to_string(w.carrier) + " -s " + path + " && echo OK");
        unlink(path);
        return out.find("OK") != std::string::npos;
    }

    bool ir_learn(std::vector<uint32_t>& timings, int timeout_ms) override {
        std::string dev = ir_device(false);
        if (dev.empty()) return false;
        int secs = timeout_ms / 1000 > 0 ? timeout_ms / 1000 : 1;
        // One message (--one-shot where this ir-ctl has it). Output is either
        // "+9000 -4500 +560 ..." or "pulse 9000 / space 4500" lines.
        std::string o = run("o=; ir-ctl --help 2>&1 | grep -q -- --one-shot && o=-1; timeout " +
                            std::to_string(secs) + " ir-ctl -d " + dev + " -r $o");
        timings.clear();
        std::istringstream in(o);
        std::string tok;
        int want = -1;   // after "pulse"/"space": 1 mark, 0 space; -2: skip a value
        auto add = [&](bool mark, long v) {
            if (v <= 0) return;
            if (timings.empty() && !mark) return;
            bool last_mark = timings.size() % 2 == 1;
            if (!timings.empty() && last_mark == mark) timings.back() += uint32_t(v);
            else timings.push_back(uint32_t(v));
        };
        while (in >> tok) {
            if (want == -2) { want = -1; continue; }
            if (want >= 0) { add(want == 1, std::atol(tok.c_str())); want = -1; continue; }
            if (tok == "pulse") want = 1;
            else if (tok == "space") want = 0;
            else if (tok == "timeout" || tok == "carrier") want = -2;
            else if (tok[0] == '+') add(true, std::atol(tok.c_str() + 1));
            else if (tok[0] == '-') add(false, std::atol(tok.c_str() + 1));
        }
        if (timings.size() % 2 == 0 && !timings.empty()) timings.pop_back();
        return timings.size() >= 7;
    }

    void power_off() override { run("systemctl poweroff"); }
    void reboot() override { run("systemctl reboot"); }

    void beep(int freq_hz, int ms) override {
        // Fire-and-forget; uses sox's play if present.
        std::string cmd = "play -nq synth " + std::to_string(ms / 1000.0) + " sine " +
                          std::to_string(freq_hz) + " vol 0.2 >/dev/null 2>&1 &";
        if (std::system(cmd.c_str())) {}
    }

private:
    static std::string home() {
        const char* h = std::getenv("HOME");
        return h ? h : "/home/pi";
    }

    // ~/.config/zeroos/stream-header.conf (0600, in a 0700 folder): the
    // Authorization header for mpv, as an option it reads with --include.
    // "" if it can't be written or the header has a line break.
    static std::string stream_header_file(const std::string& auth) {
        std::string header = "Authorization: " + auth;
        if (header.find_first_of("\r\n") != std::string::npos) return "";
        std::string dir = home() + "/.config/zeroos";
        mkdir((home() + "/.config").c_str(), 0755);
        mkdir(dir.c_str(), 0700);
        std::string path = dir + "/stream-header.conf", tmp = path + ".tmp";
        // %N% takes N bytes as they are, commas and quotes included.
        std::string text = "http-header-fields-append=%" + std::to_string(header.size()) + "%" + header + "\n";
        int fd = open(tmp.c_str(), O_WRONLY | O_CREAT | O_TRUNC | O_CLOEXEC, 0600);
        if (fd < 0) return "";
        fchmod(fd, 0600);
        bool ok = write(fd, text.data(), text.size()) == ssize_t(text.size());
        close(fd);
        if (!ok || rename(tmp.c_str(), path.c_str()) != 0) {
            unlink(tmp.c_str());
            return "";
        }
        return path;
    }

    int duration_of(const std::string& path) {
        int d = cached_duration(path);
        if (d >= 0) return d;
        std::string out = run("soxi -D " + shell_quote(path));
        if (out.empty()) return -1;
        d = int(std::strtod(out.c_str(), nullptr) + 0.5);
        durations_.push_back({path, d});
        return d;
    }
    int cached_duration(const std::string& path) {
        for (auto& p : durations_)
            if (p.first == path) return p.second;
        return -1;
    }

    long long cpu_prev_[2] = {0, 0};
    std::vector<ProcMem> top_;
    ProcCpuPrev proc_prev_;   // processes(): CPU time at the previous list
    int top_calls_ = 0;
    pid_t play_pid_ = -1, rec_pid_ = -1, cam_pid_ = -1, visible_pid_ = -1, pair_pid_ = -1, ext_pid_ = -1;
    std::vector<int> media_fds_;
    time_t media_scan_ = 0;
    static constexpr const char* kCameraApp = "/usr/bin/camera_app";
    static constexpr const char* kM5AppRoot = "/usr/share/APPLaunch";
    PlaybackState pb_;
    MpvVideo video_;   // libmpv, loaded on first use
    RecordState rec_;
    PlayClock clock_, rec_clock_;
    std::vector<std::pair<std::string, int>> durations_;
    std::string ir_tx_, ir_rx_;   // /dev/lircN, found on first use

    DisplayStatus display() override {
        DisplayStatus d;
        // The vc4 card's HDMI connector, e.g. /sys/class/drm/card1-HDMI-A-1/.
        for (auto& n : list_dir("/sys/class/drm/")) {
            if (n.find("-HDMI-A-") == std::string::npos) continue;
            std::string base = "/sys/class/drm/" + n + "/";
            if (read_str(base + "status") == "connected") {
                d.hdmi_connected = true;
                std::istringstream modes(read_str(base + "modes"));
                std::getline(modes, d.hdmi_mode);
                break;
            }
        }
        // systemctl is a fork; ask every ~2 s at most.
        if (++dock_calls_ % 20 == 1) dock_cache_ = run("systemctl is-active zeroos-dock.service").rfind("active", 0) == 0;
        d.dock_running = dock_cache_;
        return d;
    }
    void dock_start() override { run("zeroos-dock start"); dock_cache_ = true; dock_calls_ = 0; }
    void dock_stop() override { run("zeroos-dock stop"); dock_cache_ = false; dock_calls_ = 0; }
    bool dock_open(const std::string& what, const std::string& path) override {
        if (!dock_cache_) return false;
        std::ostringstream cmd;
        cmd << "zeroos-dock open " << what;
        if (!path.empty()) cmd << ' ' << shell_quote(path);
        run(cmd.str());
        return true;
    }

    // --- Tailscale. Every call forks the tailscale CLI, so cache for 3 s.
    TsStatus tailscale() override {
        time_t now = time(nullptr);
        if (now - ts_at_ < 3 && ts_at_) return ts_cache_;
        ts_at_ = now;
        TsStatus t;
        t.installed = access("/usr/bin/tailscale", X_OK) == 0;
        if (!t.installed) return ts_cache_ = t;
        t.running = run("systemctl is-active tailscaled.service").rfind("active", 0) == 0;
        if (!t.running) {
            t.state = "Stopped";
            return ts_cache_ = t;
        }
        std::string js = run("tailscale status --json --peers=false 2>/dev/null");
        t.state = json_str(js, "BackendState");
        t.auth_url = json_str(js, "AuthURL");
        t.tailnet = json_str(js, "MagicDNSSuffix");
        if (t.state.empty()) t.state = "Starting";
        if (t.state == "Running") {
            // "100.x.y.z  name  user@  os  -|offline|active; direct ..." (first line is us)
            std::istringstream in(run("tailscale status 2>/dev/null"));
            bool first = true;
            for (std::string line; std::getline(in, line);) {
                std::istringstream f(line);
                std::string ip, name, user, os;
                if (!(f >> ip >> name >> user >> os) || ip.empty() || ip[0] == '#') continue;
                std::string rest;
                std::getline(f, rest);
                if (first) {
                    t.self_ip = ip;
                    t.self_name = name;
                    first = false;
                    continue;
                }
                t.peers.push_back({name, ip, os, rest.find("offline") == std::string::npos});
            }
            std::stable_sort(t.peers.begin(), t.peers.end(),
                             [](const TsPeer& a, const TsPeer& b) { return a.online > b.online; });
        }
        return ts_cache_ = t;
    }
    void tailscale_start() override {
        // tailscaled's ZeroOS drop-in makes pi the operator, so "up" needs no
        // root. It waits for sign-in; the link shows up in status (AuthURL).
        // The control server is Tailscale's or a Headscale server (Remote
        // sets tailscale.server); an auth key, if given, is used once.
        std::string server = tailscale_server_url(prefs::get_str("tailscale.server", ""));
        std::string key = prefs::get_str("tailscale.authkey", "");
        // The key goes to tailscale in a private file (0600), removed once
        // "up" has finished with it, never on its command line.
        std::string key_file = key.empty() ? "" : private_tmp(key + "\n");
        std::string flags = tailscale_up_flags(server, prefs::get_str("tailscale.server_used", ""), key_file);
        prefs::set_str("tailscale.server_used", server);
        if (!key.empty()) prefs::set_str("tailscale.authkey", "");
        run("systemctl start tailscaled.service");
        std::string cleanup = key_file.empty() ? "" : "; rm -f " + shell_quote(key_file);
        run("mkdir -p \"$HOME/.cache/zeroos\"; (setsid tailscale up --timeout=10m" + flags + cleanup +
            ") >\"$HOME/.cache/zeroos/tailscale-up.log\" 2>&1 </dev/null &");
        ts_at_ = 0;
    }
    void tailscale_stop() override {
        run("tailscale down; systemctl stop tailscaled.service");
        ts_at_ = 0;
    }
    void tailscale_logout() override {
        run("tailscale logout");
        ts_at_ = 0;
    }
    bool taildrop_send(const std::string& path, const std::string& peer) override {
        std::ostringstream cmd;
        cmd << "tailscale file cp " << shell_quote(path) << ' ' << shell_quote(peer + ":") << " && echo ok";
        return run(cmd.str()).find("ok") != std::string::npos;
    }

    // --- Sync: syncthing@pi.service, driven through /usr/bin/zeroos-sync
    // (Syncthing's REST API). The helper is Python, so cache for 4 s.
    SyncStatus sync() override {
        time_t now = time(nullptr);
        if (now - sync_at_ < 4 && sync_at_) return sync_cache_;
        sync_at_ = now;
        SyncStatus s;
        s.installed = access("/usr/bin/syncthing", X_OK) == 0;
        if (!s.installed) return sync_cache_ = s;
        s.running = sync_running();
        if (!s.running) return sync_cache_ = s;
        std::istringstream in(run("zeroos-sync status 2>/dev/null"));
        for (std::string line; std::getline(in, line);) {
            std::vector<std::string> f;
            std::istringstream ls(line);
            for (std::string x; std::getline(ls, x, '\t');) f.push_back(x);
            if (f.empty()) continue;
            auto at = [&](size_t i) { return i < f.size() ? f[i] : std::string(); };
            if (f[0] == "id") {
                s.device_id = at(1);
                s.ready = true;
            } else if (f[0] == "folder") {
                SyncFolder d;
                d.id = at(1), d.label = at(2), d.path = at(3), d.state = at(4);
                d.percent = std::atoi(at(5).c_str());
                d.need_files = std::atoi(at(6).c_str());
                d.error = at(7);
                s.folders.push_back(d);
            } else if (f[0] == "device") {
                s.devices.push_back({at(1), at(2), at(3) == "1", std::atoi(at(4).c_str())});
            } else if (f[0] == "request-device") {
                s.requests.push_back({false, at(1), at(2), "", ""});
            } else if (f[0] == "request-folder") {
                s.requests.push_back({true, at(3), at(4), at(1), at(2)});
            }
        }
        return sync_cache_ = s;
    }
    bool sync_running() override {
        time_t now = time(nullptr);
        if (now - sync_run_at_ >= 4 || !sync_run_at_) {
            sync_run_at_ = now;
            std::string st = run("systemctl is-active syncthing@${USER:-pi}.service");
            sync_running_ = st.rfind("active", 0) == 0 || st.rfind("activating", 0) == 0;
        }
        return sync_running_;
    }
    void sync_start() override {
        // The unit's ExecStartPost runs "zeroos-sync setup" (folders, options).
        run("systemctl --no-block start syncthing@${USER:-pi}.service");
        sync_at_ = sync_run_at_ = 0;
    }
    void sync_stop() override {
        run("systemctl --no-block stop syncthing@${USER:-pi}.service");
        sync_at_ = sync_run_at_ = 0;
    }
    bool sync_answer(const SyncRequest& r, bool accept) override {
        if (!sync_id_ok(r.device_id)) return false;
        std::ostringstream cmd;
        if (r.is_folder)
            cmd << "zeroos-sync " << (accept ? "accept-folder " : "ignore-folder ") << shell_quote(r.folder_id) << ' '
                << shell_quote(r.folder_label) << ' ' << shell_quote(r.device_id);
        else
            cmd << "zeroos-sync " << (accept ? "add-device " : "ignore-device ") << shell_quote(r.device_id) << ' '
                << shell_quote(r.device_name);
        return sync_run(cmd.str());
    }
    // A Syncthing device ID: letters, digits and dashes (AAAAAAA-BBBBBBB-...),
    // never starting with '-'. IDs also come from QR codes and typing.
    static bool sync_id_ok(const std::string& id) {
        if (id.empty() || id.size() > 80 || id[0] == '-') return false;
        for (unsigned char ch : id)
            if (!std::isalnum(ch) && ch != '-') return false;
        return true;
    }
    bool sync_add_device(const std::string& id, const std::string& name) override {
        if (!sync_id_ok(id)) return false;
        std::ostringstream cmd;
        cmd << "zeroos-sync add-device " << shell_quote(id) << ' ' << shell_quote(name);
        return sync_run(cmd.str());
    }
    bool sync_remove_device(const std::string& id) override {
        if (!sync_id_ok(id)) return false;
        std::ostringstream cmd;
        cmd << "zeroos-sync remove-device " << shell_quote(id);
        return sync_run(cmd.str());
    }
    void sync_rescan() override { sync_run("zeroos-sync rescan"); }
    bool sync_run(const std::string& cmd) {
        sync_at_ = 0;
        return run(cmd + " 2>/dev/null").find("ok") != std::string::npos;
    }

    // --- Expansion 5 V
    bool ext_power() override { return !ext5v_.empty() && read_long(ext5v_[0] + "brightness") > 0; }
    void set_ext_power(bool on) override {
        // ext_5v_out sits behind an I2C IO expander whose LEDs can appear
        // after ZeroShell starts: look again rather than skip the power.
        if (ext5v_.empty()) find_ext5v();
        for (auto& d : ext5v_) {
            long max = read_long(d + "max_brightness", 1);
            write_str(d + "brightness", on ? std::to_string(max > 0 ? max : 1) : "0");
        }
    }

    // --- GPS through gpsd (socket-activated: it starts when we connect and
    // stops reading the receiver when we stop watching).
    GpsFix gps() override {
        time_t now = time(nullptr);
        gps_want_ = now;
        if (gps_fd_ < 0 && now - gps_try_ >= 3) {
            gps_try_ = now;
            gps_fd_ = socket(AF_INET, SOCK_STREAM | SOCK_NONBLOCK | SOCK_CLOEXEC, 0);
            sockaddr_in a{};
            a.sin_family = AF_INET;
            a.sin_port = htons(2947);
            a.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
            if (gps_fd_ >= 0 && connect(gps_fd_, reinterpret_cast<sockaddr*>(&a), sizeof a) < 0 &&
                errno != EINPROGRESS) {
                close(gps_fd_);
                gps_fd_ = -1;
                gps_ = GpsFix{};   // gpsd unreachable: no fix, not the last one seen
            }
            gps_sent_ = false;
            gps_buf_.clear();
        }
        if (gps_fd_ < 0) return gps_;
        if (!gps_sent_) {
            static const char kWatch[] = "?WATCH={\"enable\":true,\"json\":true};\n";
            if (send(gps_fd_, kWatch, sizeof kWatch - 1, MSG_NOSIGNAL) > 0) gps_sent_ = true;
        }
        char buf[4096];
        for (;;) {
            ssize_t n = recv(gps_fd_, buf, sizeof buf, MSG_DONTWAIT);
            if (n > 0) {
                gps_buf_.append(buf, size_t(n));
                continue;
            }
            if (n == 0 || (errno != EAGAIN && errno != EWOULDBLOCK && errno != ENOTCONN)) {
                close(gps_fd_);   // gpsd went away; try again in 3 s
                gps_fd_ = -1;
                gps_ = GpsFix{};
            }
            break;
        }
        size_t nl;
        while ((nl = gps_buf_.find('\n')) != std::string::npos) {
            std::string line = gps_buf_.substr(0, nl);
            gps_buf_.erase(0, nl + 1);
            gps_line(line);
        }
        if (gps_buf_.size() > 65536) gps_buf_.clear();
        return gps_;
    }
    void gps_stop() override {
        if (gps_fd_ < 0) return;
        static const char kOff[] = "?WATCH={\"enable\":false};\n";
        if (send(gps_fd_, kOff, sizeof kOff - 1, MSG_NOSIGNAL | MSG_DONTWAIT) < 0) {}
        close(gps_fd_);
        gps_fd_ = -1;
        gps_ = GpsFix{};   // the next look starts from "searching", never a stale fix
    }
    void gps_line(const std::string& js) {
        std::string cls = json_str(js, "class");
        if (cls == "DEVICES" || cls == "DEVICE") {
            // /etc/default/gpsd names /dev/serial0 whether or not a GPS is
            // plugged in, so a path alone means nothing. gpsd adds "driver"
            // once a receiver actually talks (a second or two).
            gps_.device = js.find("\"driver\"") != std::string::npos;
        } else if (cls == "TPV") {
            gps_.device = true;
            gps_.mode = int(json_num(js, "mode", 0));
            if (gps_.mode >= 2) {
                gps_.lat = json_num(js, "lat", gps_.lat);
                gps_.lon = json_num(js, "lon", gps_.lon);
                gps_.speed_mps = json_num(js, "speed", 0);
                gps_.track_deg = json_num(js, "track", gps_.track_deg);
                gps_.error_m = json_num(js, "eph", 0);
            }
            if (gps_.mode >= 3) gps_.alt_m = json_num(js, "altMSL", json_num(js, "alt", gps_.alt_m));
            std::string t = json_str(js, "time");
            if (!t.empty()) gps_.time = t;
        } else if (cls == "SKY") {
            int seen = 0, used = 0;
            for (size_t i = js.find("\"PRN\""); i != std::string::npos; i = js.find("\"PRN\"", i + 1)) {
                ++seen;
                size_t end = js.find('}', i);
                size_t u = js.find("\"used\":true", i);
                if (u != std::string::npos && u < end) ++used;
            }
            gps_.sats_seen = seen ? seen : int(json_num(js, "nSat", gps_.sats_seen));
            gps_.sats_used = seen ? used : int(json_num(js, "uSat", gps_.sats_used));
        }
    }
    static double json_num(const std::string& js, const std::string& key, double def) {
        auto k = js.find("\"" + key + "\":");
        if (k == std::string::npos) return def;
        const char* p = js.c_str() + k + key.size() + 3;
        char* end = nullptr;
        double v = std::strtod(p, &end);
        return end == p ? def : v;
    }

    // --- QR scanning. The preview is rpicam-vid writing raw 640x480 YUV frames to
    // a pipe, ten a second; we keep the newest whole one. (640 is a multiple
    // of 64, so the frames have no padding between rows.) To confirm on
    // hardware: which way up the picture is.
    static constexpr int kScanW = 640, kScanH = 480;
    static constexpr size_t kScanBytes = size_t(kScanW) * kScanH * 3 / 2;
    void scan_start() override {
        if (scan_pid_ > 0) return;
        scan_pid_ = spawn_pipe({"rpicam-vid", "--nopreview", "--timeout", "0", "--codec", "yuv420", "--width",
                               std::to_string(kScanW), "--height", std::to_string(kScanH), "--framerate", "10",
                               "--output", "-"},
                              scan_fd_);
        scan_state_ = scan_pid_ > 0 ? CamState::Starting : CamState::NoCamera;
        if (scan_pid_ <= 0) return;
#ifdef F_SETPIPE_SZ
        if (fcntl(scan_fd_, F_SETPIPE_SZ, 1 << 20) < 0) {}   // room for two frames
#endif
        scan_back_.resize(kScanBytes);
        scan_front_.resize(kScanBytes);
        scan_fill_ = 0;
        scan_seq_ = 0;
        scan_want_ = time(nullptr);
    }
    void scan_stop() override {
        kill_child(scan_pid_, SIGTERM);
        scan_pid_ = -1;
        if (scan_fd_ >= 0) close(scan_fd_);
        scan_fd_ = -1;
        scan_state_ = CamState::Off;
    }
    CamState scan_state() override { return scan_state_; }
    bool scan_frame(CameraFrame& out) override {
        scan_want_ = time(nullptr);
        if (scan_pid_ <= 0 || scan_fd_ < 0) return false;
        for (;;) {
            ssize_t n = read(scan_fd_, scan_back_.data() + scan_fill_, kScanBytes - scan_fill_);
            if (n > 0) {
                scan_fill_ += size_t(n);
                if (scan_fill_ == kScanBytes) {
                    scan_front_.swap(scan_back_);
                    scan_fill_ = 0;
                    ++scan_seq_;
                    scan_state_ = CamState::Running;
                }
                continue;
            }
            if (n == 0 || (errno != EAGAIN && errno != EWOULDBLOCK && errno != EINTR)) {
                // rpicam-vid gave up: no camera (the Lite model), or it's in use.
                scan_stop();
                scan_state_ = CamState::NoCamera;
                return false;
            }
            break;
        }
        if (!scan_seq_) return false;
        out.w = kScanW;
        out.h = kScanH;
        out.y = scan_front_.data();
        out.u = out.y + kScanW * kScanH;
        out.v = out.u + kScanW * kScanH / 4;
        out.seq = scan_seq_;
        return true;
    }
    // --- Tethering. zeroos-tether prints tab-separated lines and always ends
    // with the current state ("bt", "usb", "phone" lines, then "end"). It can
    // take many seconds (a search, pairing), so it runs in the background
    // and we read what it says from poll().
    TetherStatus tether() override {
        tether_want_ = time(nullptr);
        tether_pump();
        return tether_;
    }
    void tether_search() override { tether_run("searching", {"search"}, ""); }
    void tether_pair(const std::string& mac) override { tether_run("pairing", {"pair", mac}, mac); }
    void tether_connect(const std::string& mac) override { tether_run("connecting", {"connect", mac}, mac); }
    void tether_disconnect(const std::string& mac) override { tether_run("", {"disconnect", mac}, mac); }
    void tether_forget(const std::string& mac) override { tether_run("", {"forget", mac}, mac); }

    void tether_run(const std::string& busy, std::vector<std::string> args, const std::string& mac) {
        tether_end();
        bt_daemon();   // zeroos-tether asks BlueZ whether Bluetooth is on
        args.insert(args.begin(), "zeroos-tether");
        tether_pid_ = spawn_pipe(args, tether_fd_);
        tether_status_job_ = args[1] == "status";
        if (tether_status_job_) return;
        tether_.busy = busy;
        tether_.busy_mac = mac;
        tether_.passkey.clear();
        tether_.error.clear();
        if (tether_pid_ <= 0) tether_.busy.clear();
    }
    void tether_end() {
        if (tether_pid_ > 0) kill_child(tether_pid_, SIGTERM);
        tether_pid_ = -1;
        if (tether_fd_ >= 0) close(tether_fd_);
        tether_fd_ = -1;
        tether_buf_.clear();
        tether_next_ = TetherStatus{};
        tether_found_seen_ = false;
    }
    void tether_pump() {
        time_t now = time(nullptr);
        if (tether_pid_ <= 0) {
            // Refresh every 3 s while someone is looking.
            if (now - tether_want_ <= 5 && now - tether_at_ >= 3) tether_run("", {"status"}, "");
            return;
        }
        char buf[1024];
        bool eof = false;
        for (;;) {
            ssize_t n = read(tether_fd_, buf, sizeof buf);
            if (n > 0) {
                tether_buf_.append(buf, size_t(n));
                continue;
            }
            eof = n == 0 || (errno != EAGAIN && errno != EWOULDBLOCK && errno != EINTR);
            break;
        }
        size_t nl;
        while ((nl = tether_buf_.find('\n')) != std::string::npos) {
            std::vector<std::string> f;
            std::istringstream ls(tether_buf_.substr(0, nl));
            tether_buf_.erase(0, nl + 1);
            for (std::string x; std::getline(ls, x, '\t');) f.push_back(x);
            if (!f.empty()) tether_line(f);
        }
        if (eof) {
            bool was_status = tether_status_job_;
            tether_end();
            tether_at_ = now;
            if (!was_status) {
                tether_.busy.clear();
                tether_.busy_mac.clear();
                tether_.passkey.clear();
                net_refresh_soon();   // the status bar's network icons: look again now
            }
        }
    }
    void tether_line(const std::vector<std::string>& f) {
        auto at = [&](size_t i) { return i < f.size() ? f[i] : std::string(); };
        TetherStatus& n = tether_next_;
        if (f[0] == "bt") n.bt_on = at(1) == "on";
        else if (f[0] == "usb") {
            n.usb = true;
            n.usb_name = at(2);
            n.usb_ip = at(3);
        } else if (f[0] == "phone") n.phones.push_back({at(1), at(2), at(3) == "1", at(4) == "1"});
        else if (f[0] == "found") {
            n.found.push_back({at(1), at(2), false, false});
            tether_found_seen_ = true;
        } else if (f[0] == "searched") tether_found_seen_ = true;   // even if it found nothing
        else if (f[0] == "passkey") tether_.passkey = at(1);
        else if (f[0] == "stage") tether_.busy = at(1);
        else if (f[0] == "error") tether_.error = at(1);
        else if (f[0] == "end") {
            // Keep what belongs to the action in progress; take the rest.
            n.busy = tether_.busy;
            n.busy_mac = tether_.busy_mac;
            n.passkey = tether_.passkey;
            n.error = tether_.error;
            if (!tether_found_seen_) n.found = tether_.found;
            for (auto& p : n.phones)   // a phone we just paired is no longer "found"
                n.found.erase(std::remove_if(n.found.begin(), n.found.end(),
                                             [&](const TetherPhone& x) { return x.mac == p.mac; }),
                              n.found.end());
            tether_ = n;
            tether_next_ = TetherStatus{};
            tether_found_seen_ = false;
        }
    }

    // --- LoRa mesh: meshtasticd.service (config in /etc/meshtasticd/config.d).
    MeshStatus mesh() override {
        time_t now = time(nullptr);
        if (now - mesh_at_ < 3 && mesh_at_) return mesh_cache_;
        mesh_at_ = now;
        MeshStatus m;
        m.installed = access("/usr/bin/meshtasticd", X_OK) == 0 || access("/usr/sbin/meshtasticd", X_OK) == 0;
        // ActiveState/SubState/NRestarts, so a daemon that keeps crashing
        // (Restart= puts it in "activating (auto-restart)") isn't shown as on.
        std::string active, sub;
        int restarts = 0;
        std::istringstream show(run("systemctl show meshtasticd.service -p ActiveState -p SubState -p NRestarts 2>/dev/null"));
        for (std::string line; std::getline(show, line);) {
            size_t eq = line.find('=');
            if (eq == std::string::npos) continue;
            std::string k = line.substr(0, eq), v = line.substr(eq + 1);
            if (k == "ActiveState") active = v;
            else if (k == "SubState") sub = v;
            else if (k == "NRestarts") restarts = std::atoi(v.c_str());
        }
        m.running = active == "active" || active == "activating" || active == "reloading";
        // NRestarts stays up after the daemon recovers, so only a new
        // restart counts, and only for 30 s: one slow start isn't an error.
        if (mesh_restarts_ < 0) mesh_restarts_ = restarts;   // baseline after a start
        if (restarts > mesh_restarts_) mesh_fail_until_ = now + 30;
        mesh_restarts_ = restarts;
        m.failing = mesh_wanted_ && (active == "failed" || sub == "auto-restart" || now < mesh_fail_until_);
        if (m.failing) {
            if (now - mesh_err_at_ >= 10) {   // the journal is slow: not every refresh
                mesh_err_at_ = now;
                mesh_err_ = mesh_last_error();
                // 2.7 stops (Aborted) instead of using a simulated radio when
                // no Cap is plugged in: that's "No radio", not a radio error.
                mesh_no_radio_ = mesh_saw_no_radio();
            }
            m.error = mesh_err_;
            m.no_radio = mesh_no_radio_;
        }
        if (m.running) {
            // Without a working radio the daemon falls back to a simulated
            // one and says so in its log (pi is in the adm group). Look three
            // times, 15 s apart, after it starts.
            if (mesh_log_checks_ < 3 && now - mesh_log_at_ >= 15) {
                mesh_log_at_ = now;
                ++mesh_log_checks_;
                mesh_no_radio_ = mesh_saw_no_radio();
            }
            m.no_radio = mesh_no_radio_;
            const NetStatus& n = net_cache_;   // poll() refreshes it; read_net() forks ~7 times
            std::string ip = !n.tailscale_ip.empty() && n.ip.empty() ? n.tailscale_ip : n.ip;
            if (!ip.empty()) {
                m.web_url = "https://" + ip + ":9443";
                m.api = ip;
            }
        }
        m.connected = mesh_connected_;
        m.my_name = mesh_me_ ? node_name(mesh_me_) : "";
        m.nodes = int(mesh_names_.size()) - (mesh_names_.count(mesh_me_) ? 1 : 0);
        return mesh_cache_ = m;
    }
    void mesh_start() override {
        mesh_wanted_ = true;
        mesh_no_radio_ = false;
        mesh_log_at_ = time(nullptr);   // give it 15 s before reading its log
        mesh_log_checks_ = 0;
        mesh_err_at_ = 0;
        mesh_err_.clear();
        mesh_started_at_ = time(nullptr);
        mesh_restarts_ = -1;
        mesh_fail_until_ = 0;
        set_ext_power(true);
        // Start clean: a unit left "failed" by an earlier try won't restart.
        run("systemctl reset-failed meshtasticd.service 2>/dev/null");
        run("systemctl --no-block start meshtasticd.service");
        mesh_at_ = 0;
    }
    void mesh_stop() override {
        mesh_wanted_ = false;   // stop polling systemd and the journal
        mesh_fail_until_ = 0;
        run("systemctl --no-block stop meshtasticd.service");
        mesh_at_ = 0;
        mesh_close();
    }

    // Chat: a client connection to meshtasticd's API while it runs.
    void mesh_pump() {
        time_t now = time(nullptr);
        if (!mesh_wanted_) return;   // never started this session: nothing to ask
        if (now - mesh_at_ >= 3) mesh();   // refresh "running" every few seconds
        if (!mesh_cache_.running) {
            mesh_close();
            return;
        }
        if (mesh_fd_ < 0) {
            if (now - mesh_try_ < 5) return;
            mesh_try_ = now;
            mesh_fd_ = socket(AF_INET, SOCK_STREAM | SOCK_NONBLOCK | SOCK_CLOEXEC, 0);
            sockaddr_in a{};
            a.sin_family = AF_INET;
            a.sin_port = htons(4403);
            a.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
            if (mesh_fd_ >= 0 && connect(mesh_fd_, reinterpret_cast<sockaddr*>(&a), sizeof a) < 0 &&
                errno != EINPROGRESS) {
                mesh_close();
                return;
            }
            mesh_hello_ = false;
            mesh_in_.clear();
        }
        if (!mesh_hello_ && mesh_write(mt::want_config(uint32_t(now)))) {
            mesh_hello_ = true;
            mesh_beat_ = now;
        }
        if (mesh_hello_ && now - mesh_beat_ > 240 && mesh_write(mt::heartbeat())) mesh_beat_ = now;
        mesh_flush();
        if (mesh_fd_ < 0) return;
        char buf[2048];
        for (;;) {
            ssize_t n = recv(mesh_fd_, buf, sizeof buf, MSG_DONTWAIT);
            if (n > 0) {
                mesh_in_.feed(buf, size_t(n));
                continue;
            }
            if (n == 0 || (errno != EAGAIN && errno != EWOULDBLOCK && errno != ENOTCONN)) {
                mesh_close();
                return;
            }
            break;
        }
        std::string payload;
        while (mesh_in_.next(payload)) {
            mt::Event e = mt::decode(payload);
            switch (e.kind) {
            case mt::Event::MyNode: mesh_me_ = e.num; break;
            case mt::Event::Node:
                mesh_names_[e.num] = e.short_name.empty() ? e.long_name : e.short_name;
                break;
            case mt::Event::ConfigDone: mesh_connected_ = true; break;
            case mt::Event::Text: {
                if (e.from == mesh_me_) break;
                MeshMessage m;
                m.from = node_name(e.from);
                m.text = e.text;
                m.direct = e.to != mt::kBroadcast;
                m.time = e.rx_time ? long(e.rx_time) : long(now);
                mesh_log_.push_back(m);
                if (mesh_log_.size() > 200) mesh_log_.erase(mesh_log_.begin());
                ++mesh_seq_;
                break;
            }
            default: break;
            }
        }
    }
    std::string node_name(uint32_t num) {
        auto it = mesh_names_.find(num);
        if (it != mesh_names_.end() && !it->second.empty()) return it->second;
        char b[16];
        std::snprintf(b, sizeof b, "!%08x", num);
        return b;
    }
    // The daemon's last error from this run (since mesh_start, else this
    // boot): ERROR/CRIT lines first, then anything that reads like one.
    // The daemon's log says it found no LoRa radio (pi is in the adm group).
    bool mesh_saw_no_radio() {
        return !run("journalctl -u meshtasticd -b --no-pager -n 300 2>/dev/null | "
                    // 2.7.x: "SX126x init result -2", "No sx1262 radio"
                    "grep -m1 -iE 'init result -[0-9]|No [A-Za-z0-9]+ radio|SimRadio|"
                    "radio init failed'").empty();
    }
    std::string mesh_last_error() {
        std::string since = mesh_started_at_ ? "--since @" + std::to_string(long(mesh_started_at_) - 1) : "-b";
        std::istringstream in(run("journalctl -u meshtasticd " + since + " --no-pager -n 200 -o cat 2>/dev/null"));
        std::string worst, any;
        for (std::string line; std::getline(in, line);) {
            if (line.find("ERROR") != std::string::npos || line.find("CRIT") != std::string::npos) worst = line;
            std::string l = line;
            std::transform(l.begin(), l.end(), l.begin(), ::tolower);
            for (const char* k : {"error", "fail", "could not", "cannot", "unable", "no such", "denied"})
                if (l.find(k) != std::string::npos) any = line;
        }
        std::string e = worst.empty() ? any : worst;
        while (!e.empty() && std::isspace(uint8_t(e.back()))) e.pop_back();
        if (e.size() > 160) e.resize(160);
        return e;
    }
    // Queue a frame and send what the socket takes now; mesh_pump sends the
    // rest. A partial send must not drop the tail of a frame: the daemon's
    // framing would be off until we reconnect.
    bool mesh_write(const std::string& frame) {
        if (mesh_fd_ < 0 || mesh_out_.size() + frame.size() > 16384) return false;
        mesh_out_ += frame;
        mesh_flush();
        return mesh_fd_ >= 0;
    }
    void mesh_flush() {
        while (mesh_fd_ >= 0 && !mesh_out_.empty()) {
            ssize_t n = send(mesh_fd_, mesh_out_.data(), mesh_out_.size(), MSG_NOSIGNAL | MSG_DONTWAIT);
            if (n > 0) {
                mesh_out_.erase(0, size_t(n));
            } else if (n < 0 && (errno == EAGAIN || errno == EWOULDBLOCK || errno == ENOTCONN || errno == EINTR)) {
                break;   // still connecting, or the buffer is full: next pump
            } else {
                mesh_close();
            }
        }
    }
    void mesh_close() {
        if (mesh_fd_ >= 0) close(mesh_fd_);
        mesh_fd_ = -1;
        mesh_out_.clear();
        mesh_hello_ = mesh_connected_ = false;
    }
    // At most max bytes, not ending inside a UTF-8 character.
    static std::string utf8_cut(const std::string& s, size_t max) {
        if (s.size() <= max) return s;
        size_t n = max;
        while (n > 0 && (uint8_t(s[n]) & 0xc0) == 0x80) --n;
        return s.substr(0, n);
    }
    std::vector<MeshMessage> mesh_messages() override { return mesh_log_; }
    bool mesh_send(const std::string& text) override {
        if (!mesh_connected_ || text.empty()) return false;
        std::string t = utf8_cut(text, 200);
        if (!mesh_write(mt::text_message(t, mt::kBroadcast, 0, ++mesh_packet_id_))) return false;
        MeshMessage m;
        m.text = t;
        m.mine = true;
        m.time = long(time(nullptr));
        mesh_log_.push_back(m);
        return true;
    }
    int mesh_seq() override { return mesh_seq_; }

    // --- Terminal: zeroos-terminal.service runs tmux on tty3 and switches to it.
    bool terminal_open(const std::string& command) override {
        std::string dir = std::string(std::getenv("HOME") ? std::getenv("HOME") : "/home/pi") + "/.cache/zeroos";
        run("mkdir -p '" + dir + "'");
        write_str(dir + "/terminal-cmd", command);
        // --no-block: starting waits for the console switch; don't freeze the UI.
        run("systemctl start --no-block zeroos-terminal.service");
        term_at_ = 0;
        return true;
    }
    bool terminal_active() override {
        time_t now = time(nullptr);
        if (now != term_at_) {
            term_at_ = now;
            std::string st = run("systemctl is-active zeroos-terminal.service");
            term_cache_ = st.rfind("active", 0) == 0 || st.rfind("activating", 0) == 0;
        }
        return term_cache_;
    }

    // --- Bluetooth audio out (docs/CAR.md). bluetoothctl does the pairing;
    // PipeWire's Bluetooth module sends the sound once a device connects.
    static bool is_address(const std::string& a) {
        if (a.size() != 17) return false;
        for (size_t i = 0; i < 17; ++i) {
            if (i % 3 == 2 ? a[i] != ':' : !std::isxdigit((unsigned char)a[i])) return false;
        }
        return true;
    }
    // "Device AA:BB:CC:DD:EE:FF Name with spaces" lines.
    static void parse_bt(const std::string& out, std::vector<BtDevice>& v, bool paired, bool connected) {
        std::istringstream in(out);
        for (std::string l; std::getline(in, l);) {
            if (l.rfind("Device ", 0) != 0 || l.size() < 24) continue;
            std::string addr = l.substr(7, 17);
            if (!is_address(addr)) continue;
            std::string name = l.size() > 25 ? l.substr(25) : addr;
            BtDevice* d = nullptr;
            for (auto& x : v)
                if (x.address == addr) d = &x;
            if (!d) {
                // Devices that only announce an address are not speakers or cars.
                std::string dashed = addr;
                for (auto& ch : dashed) if (ch == ':') ch = '-';
                if (!paired && (name == addr || name == dashed)) continue;
                v.push_back({addr, name, false, false});
                d = &v.back();
            }
            d->paired = d->paired || paired;
            d->connected = d->connected || connected;
        }
    }
    // bluetoothd only runs while Bluetooth is on, or for a while after it
    // was last asked something (demand_step). bluetoothctl waits for ever for
    // a bluetoothd that isn't there, so every use starts it first. False if
    // it can't be started: then there is nothing to ask.
    bool bt_daemon() {
        bt_used_ = steady_s();
        if (!demand_bt_ || demand::proc_running(sys_root_, "bluetoothd")) return true;
        run("systemctl start bluetooth.service");   // back once BlueZ is on the bus
        return demand::proc_running(sys_root_, "bluetoothd");
    }
    // Radio on, and bluetoothd there to make it so.
    void bt_power_on() {
        run("rfkill unblock bluetooth");
        if (bt_daemon()) run("bluetoothctl power on");
        net_refresh_soon();
    }
    std::vector<BtDevice> bt_devices() override {
        std::vector<BtDevice> v;
        if (!bt_daemon()) return v;
        parse_bt(run("bluetoothctl devices Connected"), v, true, true);
        parse_bt(run("bluetoothctl devices Paired"), v, true, false);
        parse_bt(run("bluetoothctl devices"), v, false, false);
        std::stable_sort(v.begin(), v.end(), [](const BtDevice& a, const BtDevice& b) {
            return a.connected != b.connected ? a.connected : a.paired != b.paired ? a.paired : false;
        });
        return v;
    }
    // Addresses of the paired devices.
    std::vector<std::string> bt_paired() {
        std::vector<std::string> v;
        if (!bt_daemon()) return v;
        std::istringstream in(run("bluetoothctl devices Paired"));
        for (std::string l; std::getline(in, l);)
            if (l.rfind("Device ", 0) == 0 && l.size() >= 24 && is_address(l.substr(7, 17))) v.push_back(l.substr(7, 17));
        return v;
    }
    // Unpairs whatever paired since `before`, except `keep`.
    void bt_drop_strangers(const std::vector<std::string>& before, const std::string& keep) {
        for (auto& a : bt_paired())
            if (a != keep && std::find(before.begin(), before.end(), a) == before.end())
                run("bluetoothctl remove " + a);
    }
    // A bluetoothctl that is BlueZ's default agent and accepts pairing
    // requests (its built-in "agent auto"), with the adapter pairable, for
    // `seconds`. Gamepads (Xbox) ask "Accept pairing?" the moment they
    // connect; with no default agent BlueZ refuses ("No agent available") and
    // they connect unpaired, then drop. The agent can't tell who is asking,
    // so the window is only open while someone is pairing, and once a second
    // it unpairs any device that paired in it other than the one allowed:
    //   allow = an address: only that device (the one picked to pair).
    //   allow = "", visible: the first device that pairs (Car's "Be found",
    //     where the car starts the pairing); the window then closes at once,
    //     with that one trusted so it can reconnect by itself.
    // `before` is the paired list when it opened. bluetoothctl registers its
    // own agent first; dropping it is async, hence the pauses.
    static std::string pairing_window_cmd(int seconds, const std::vector<std::string>& before,
                                          const std::string& allow, bool visible) {
        std::string known;
        for (auto& a : before) known += a + " ";
        std::string cmd =
            "known='" + known + "'; allow='" + allow + "'; "
            "guard() { for a in $(bluetoothctl devices Paired | while read -r _ x _; do echo \"$x\"; done); do "
            "case \" $known $allow \" in *\" $a \"*) ;; *) ";
        if (visible)
            cmd += "if [ -z \"$allow\" ]; then allow=$a; bluetoothctl trust \"$a\" >/dev/null 2>&1; "
                   "else bluetoothctl remove \"$a\" >/dev/null 2>&1; fi;; ";
        else
            cmd += "bluetoothctl remove \"$a\" >/dev/null 2>&1;; ";
        cmd += "esac; done; }; "
               "{ sleep 1; echo 'agent off'; sleep 0.5; echo 'agent auto'; sleep 0.5; echo default-agent; "
               "echo 'pairable on'; ";
        if (visible) cmd += "echo 'discoverable on'; ";
        cmd += "t=0; while [ $t -lt " + std::to_string(seconds) + " ]; do sleep 1; t=$((t+1)); guard; ";
        if (visible) cmd += "[ -n \"$allow\" ] && break; ";
        cmd += "done; ";
        if (visible) cmd += "echo 'discoverable off'; ";
        cmd += "echo 'pairable off'; sleep 0.5; guard; echo quit; } | bluetoothctl >/dev/null 2>&1";
        return cmd;
    }
    void bt_close_pairing_window() {
#ifndef __EMSCRIPTEN__
        if (pair_pid_ <= 0) return;
        kill_child(pair_pid_, SIGTERM);
        pair_pid_ = -1;
        if (bt_daemon()) run("bluetoothctl pairable off");
#endif
    }
    void bt_scan() override {
        bt_power_on();
        // No pairing window here: nothing pairs until a device is picked
        // (bt_connect), and then only that one.
        if (bt_daemon()) run("bluetoothctl --timeout 8 scan on");
    }
    bool bt_connect(const std::string& address, std::string& error) override {
        if (!is_address(address)) {
            error = "Not a Bluetooth address";
            return false;
        }
        bt_power_on();
        if (!bt_daemon()) {
            error = "Bluetooth didn't start";
            return false;
        }
        std::string info = run("bluetoothctl info " + address);
        std::vector<std::string> before;
        bool windowed = false;
        if (info.find("Paired: yes") == std::string::npos) {
            // "Just works" pairing: nothing to type here. The window's agent
            // answers what this device asks, before or after our pair, until
            // it has connected; anything else that pairs is dropped.
            before = bt_paired();
            windowed = true;
#ifndef __EMSCRIPTEN__
            if (pair_pid_ > 0) kill_child(pair_pid_, SIGTERM);
            pair_pid_ = spawn({"sh", "-c", pairing_window_cmd(60, before, address, false)});
            usleep(2500 * 1000);   // registered and pairable before anything asks
#endif
            std::string out = run("bluetoothctl --timeout 25 pair " + address);
            if (out.find("Pairing successful") == std::string::npos &&
                run("bluetoothctl info " + address).find("Paired: yes") == std::string::npos) {
                bt_close_pairing_window();
                bt_drop_strangers(before, address);
                error = "Pairing failed. Put it in pairing mode (lights flash fast), then try again";
                return false;
            }
        }
        run("bluetoothctl trust " + address);   // lets it reconnect by itself later
        std::string out = run("bluetoothctl --timeout 15 connect " + address);
        bool ok = out.find("Connection successful") != std::string::npos ||
                  run("bluetoothctl info " + address).find("Connected: yes") != std::string::npos;
        if (windowed) {
            // Paired and connected (or given up): close the window now.
            bt_close_pairing_window();
            bt_drop_strangers(before, address);
        }
        if (!ok) {
            error = "Paired, but it didn't connect";
            return false;
        }
        return true;
    }
    void bt_disconnect(const std::string& address) override {
        if (is_address(address) && bt_daemon()) run("bluetoothctl --timeout 8 disconnect " + address);
    }
    void bt_forget(const std::string& address) override {
        if (is_address(address) && bt_daemon()) run("bluetoothctl remove " + address);
    }
    void bt_set_visible(int seconds) override {
#ifndef __EMSCRIPTEN__
        if (visible_pid_ > 0) {
            // Ends the agent and the timer (the whole group), then does what
            // the window would have: hide, and stop taking pairings. What
            // paired in it was trusted (or dropped) as it happened.
            kill_child(visible_pid_, SIGTERM);
            visible_pid_ = -1;
            if (bt_daemon()) {
                run("bluetoothctl discoverable off");
                run("bluetoothctl pairable off");
            }
        }
        if (seconds <= 0) return;
        bt_power_on();
        if (!bt_daemon()) return;
        // Visible and taking pairings for `seconds`, or until the first
        // device (the car) has paired: one device per "Be found".
        visible_pid_ = spawn({"sh", "-c", pairing_window_cmd(seconds, bt_paired(), "", true)});
#endif
    }

    // Play/skip buttons: BlueZ presents a connected car or headset's buttons
    // as an input device (through uinput), like a USB multimedia keyboard.
    MediaKey media_key() override {
#ifdef __EMSCRIPTEN__
        return MediaKey::None;
#else
        time_t now = time(nullptr);
        if (now - media_scan_ >= 3) {   // devices come and go with the connection
            media_scan_ = now;
            for (int fd : media_fds_) close(fd);
            media_fds_.clear();
            for (auto& n : list_dir("/dev/input")) {
                if (n.rfind("event", 0) != 0) continue;
                int fd = open(("/dev/input/" + n).c_str(), O_RDONLY | O_NONBLOCK | O_CLOEXEC);
                if (fd < 0) continue;
                unsigned long keys[KEY_MAX / (8 * sizeof(long)) + 1] = {0};
                ioctl(fd, EVIOCGBIT(EV_KEY, sizeof keys), keys);
                auto has = [&](int k) { return (keys[k / (8 * sizeof(long))] >> (k % (8 * sizeof(long)))) & 1; };
                // Media buttons but no letters: not the built-in keyboard.
                if ((has(KEY_NEXTSONG) || has(KEY_PLAYCD) || has(KEY_PLAYPAUSE)) && !has(KEY_A)) media_fds_.push_back(fd);
                else close(fd);
            }
        }
        for (int fd : media_fds_) {
            input_event ev;
            while (read(fd, &ev, sizeof ev) == ssize_t(sizeof ev)) {
                if (ev.type != EV_KEY || ev.value != 1) continue;
                switch (ev.code) {
                    case KEY_PLAYPAUSE: return MediaKey::PlayPause;
                    case KEY_PLAYCD: case KEY_PLAY: return MediaKey::Play;
                    case KEY_PAUSECD: case KEY_PAUSE: return MediaKey::Pause;
                    case KEY_STOPCD: case KEY_STOP: return MediaKey::Stop;
                    case KEY_NEXTSONG: case KEY_FASTFORWARD: return MediaKey::Next;
                    case KEY_PREVIOUSSONG: case KEY_REWIND: return MediaKey::Previous;
                }
            }
        }
        return MediaKey::None;
#endif
    }

    // A play/skip key for a phone whose music plays here (docs/CAR.md): its
    // AVRCP player, which BlueZ shows while the phone is connected. Only a
    // device that sends music counts: a car's or speaker's own player isn't
    // what the keys are for.
    bool phone_media_key(MediaKey k) override {
#ifdef __EMSCRIPTEN__
        (void)k;
        return false;
#else
        if (k == MediaKey::None) return false;
        btplayer::Info info;
        std::string path = phone_player(info);
        if (!path.empty()) {
            btplayer::Key key = k == MediaKey::PlayPause ? btplayer::Key::PlayPause
                                : k == MediaKey::Play    ? btplayer::Key::Play
                                : k == MediaKey::Pause   ? btplayer::Key::Pause
                                : k == MediaKey::Stop    ? btplayer::Key::Stop
                                : k == MediaKey::Next    ? btplayer::Key::Next
                                                         : btplayer::Key::Previous;
            // player_paths() only passes letters, digits, '_' and '/'.
            run("busctl call org.bluez " + path + " org.bluez.MediaPlayer1 " + btplayer::method_for(key, info.playing()));
            return true;
        }
        return false;
#endif
    }
    bool phone_playing() override {
#ifdef __EMSCRIPTEN__
        return false;
#else
        btplayer::Info info;
        return !phone_player(info).empty() && info.playing();
#endif
    }
#ifndef __EMSCRIPTEN__
    // The first connected phone's player and what it says it's doing, or "".
    std::string phone_player(btplayer::Info& info) {
        // Bluetooth off: no phone. (Asking BlueZ over D-Bus would start
        // bluetoothd just to hear that.)
        if (!demand::rfkill_on(sys_root_, "bluetooth")) return "";
        bt_used_ = steady_s();
        for (const std::string& path : btplayer::player_paths(run("busctl --list tree org.bluez"))) {
            std::string addr = btplayer::address_of(path);
            if (!is_address(addr) || !btplayer::is_music_source(run("bluetoothctl info " + addr))) continue;
            info = btplayer::parse_info(run("busctl get-property org.bluez " + path + " org.bluez.MediaPlayer1 Status"));
            return path;
        }
        return "";
    }
#endif

    // --- USB stick mode: zeroos-usb-stick.service does the work as root and
    // reports through a state file.
    UsbStickStatus usb_stick() override {
        UsbStickStatus st;
        std::string s = read_str("/run/zeroos/usb-stick.state");
        if (s.rfind("building", 0) == 0) {
            st.state = UsbStickStatus::Building;
            st.percent = std::atoi(s.c_str() + 8);
        } else if (s.rfind("error:", 0) == 0) {
            st.state = UsbStickStatus::Error;
            st.error = s.substr(s.find_first_not_of(' ', 6) == std::string::npos ? 6 : s.find_first_not_of(' ', 6));
        } else if (s.rfind("ready", 0) == 0) {
            st.state = UsbStickStatus::Ready;
            // "configured" once the car (or a PC) has taken it up.
            for (auto& n : list_dir("/sys/class/udc"))
                if (read_str("/sys/class/udc/" + n + "/state") == "configured") st.state = UsbStickStatus::Attached;
        }
        return st;
    }
    void usb_stick_start() override { run("systemctl start --no-block zeroos-usb-stick.service"); }
    void usb_stick_stop() override { run("systemctl stop --no-block zeroos-usb-stick.service"); }

    // --- Software update: zeroos-update runs as root from two services and
    // reports through a state file of key=value lines.
    UpdateStatus update() override {
        UpdateStatus st;
        st.current = os_release_version();
        std::ifstream f("/run/zeroos/update.status");
        std::string line, state;
        while (std::getline(f, line)) {
            size_t eq = line.find('=');
            if (eq == std::string::npos) continue;
            std::string k = line.substr(0, eq), v = line.substr(eq + 1);
            if (k == "state") state = v;
            else if (k == "latest") st.latest = v;
            else if (k == "percent") st.percent = std::atoi(v.c_str());
            else if (k == "message") st.message = v;
        }
        static const std::map<std::string, UpdateStatus::State> states = {
            {"checking", UpdateStatus::Checking},     {"up-to-date", UpdateStatus::UpToDate},
            {"available", UpdateStatus::Available},   {"offline", UpdateStatus::Offline},
            {"downloading", UpdateStatus::Downloading}, {"installing", UpdateStatus::Installing},
            {"restarting", UpdateStatus::Restarting}, {"done", UpdateStatus::Done},
            {"rolled-back", UpdateStatus::RolledBack}, {"failed", UpdateStatus::Failed}};
        auto it = states.find(state);
        if (it != states.end()) st.state = it->second;
        return st;
    }
    void update_check() override { run("systemctl start --no-block zeroos-update-check.service"); }
    void update_install() override { run("systemctl start --no-block zeroos-update.service"); }

    // --- Camera: M5Stack's camera_app draws on the LCD itself. It reads the
    // keyboard without grabbing it, so ZeroShell still sees key presses and
    // keeps the screen awake (it ignores what they mean while handed off).
    CameraStatus camera() override {
        CameraStatus st;
        st.installed = access(kCameraApp, X_OK) == 0;
        // A sensor that answered at boot is bound to its driver.
        for (const char* drv : {"imx219", "imx477", "imx708", "imx296", "ov5647", "ov9281"}) {
            for (auto& n : list_dir(std::string("/sys/bus/i2c/drivers/") + drv))
                if (n.find('-') != std::string::npos && std::isdigit((unsigned char)n[0])) st.present = true;
        }
        return st;
    }
    bool camera_open() override {
#ifdef __EMSCRIPTEN__
        return false;
#else
        if (camera_active()) return true;
        scan_stop();   // the QR reader must let go of the sensor first
        if (access(kCameraApp, X_OK) != 0) return false;
        cam_pid_ = spawn({"env", "CAMERA_APP_KEYBOARD_GRAB=0",
                          "CAMERA_APP_FB_DEVICE=" + lcd_framebuffer(), kCameraApp});
        return cam_pid_ > 0;
#endif
    }
    bool camera_active() override {
        if (cam_pid_ <= 0) return false;
        if (exited(cam_pid_)) cam_pid_ = -1;
        return cam_pid_ > 0;
    }
    void camera_close() override {
        if (cam_pid_ <= 0) return;
        kill_child(cam_pid_, SIGTERM);   // it saves and stops the sensor on SIGTERM
        cam_pid_ = -1;
    }

    // --- Apps made for M5Stack's launcher (APPLaunch). Started the way it
    // starts them: sh -c <Exec> in /usr/share/APPLaunch, as our user, in a
    // process group of its own, the keyboard shared (not grabbed), drawing
    // on the LCD's framebuffer, which M5's LVGL apps find in these variables.
    // One that opens the LCD's DRM card by name instead finds it at
    // /dev/dri/card1, its number on M5's system, whatever number it has here
    // (hal/lcd_drm.h).
    bool external_open(const std::string& command) override {
#ifdef __EMSCRIPTEN__
        (void)command;
        return false;
#else
        if (external_active()) return true;
        if (command.empty()) return false;
        scan_stop();   // a QR reader left running would hold the camera
        std::string fb = lcd_framebuffer();
        std::string swap;
        if (m5_opens_card1(command, kM5AppRoot)) swap = m5_drm_swap(lcd_drm_card(sys_root_ + "/sys", lcd_fb_sysfs_name()));
        std::string shim = swap.empty() ? "" : m5_drm_shim_file();
        // Everything is built before fork (see spawn()).
        std::vector<std::string> env_s;
        std::string preload;   // what is preloaded already (the tests' fake devices)
        for (char** e = environ; e && *e; ++e) {
            std::string v = *e;
            if (v.rfind("LV_LINUX_FBDEV_DEVICE=", 0) == 0 || v.rfind("APPLAUNCH_LINUX_FBDEV_DEVICE=", 0) == 0 ||
                v.rfind("SDL_FBDEV=", 0) == 0 || v.rfind("ZEROOS_DRM_SWAP=", 0) == 0)
                continue;
            if (!shim.empty() && v.rfind("LD_PRELOAD=", 0) == 0) {
                preload = v.substr(11);
                continue;
            }
            env_s.push_back(v);
        }
        env_s.push_back("LV_LINUX_FBDEV_DEVICE=" + fb);
        env_s.push_back("APPLAUNCH_LINUX_FBDEV_DEVICE=" + fb);
        env_s.push_back("SDL_FBDEV=" + fb);
        if (!shim.empty()) {
            env_s.push_back("ZEROOS_DRM_SWAP=" + swap);
            env_s.push_back("LD_PRELOAD=" + (preload.empty() ? shim : shim + ":" + preload));
        }
        std::vector<char*> envp;
        for (auto& v : env_s) envp.push_back(const_cast<char*>(v.c_str()));
        envp.push_back(nullptr);
        std::string cmd = command;
        const char* argv[] = {"sh", "-c", cmd.c_str(), nullptr};
        pid_t pid = fork();
        if (pid == 0) {
            setpgid(0, 0);
            // Ours are -5 and -500 (zeroshell.service): an app gets the usual.
            setpriority(PRIO_PROCESS, 0, 0);
            int oom = open("/proc/self/oom_score_adj", O_WRONLY);
            if (oom >= 0) {
                (void)!write(oom, "0", 1);
                close(oom);
            }
            if (chdir(kM5AppRoot) != 0) (void)!chdir("/");
            int devnull = open("/dev/null", O_RDWR);
            if (devnull >= 0) dup2(devnull, 0);   // output stays: it lands in our journal
            for (int fd = 3; fd < 1024; ++fd) close(fd);   // not our screen, keyboard or sockets
            execve("/bin/sh", const_cast<char* const*>(argv), envp.data());
            _exit(127);
        }
        ext_pid_ = pid;
        if (pid > 0) std::fprintf(stderr, "zeroshell: M5Stack app started (%d): %s\n", int(pid), command.c_str());
        if (pid > 0 && !shim.empty()) std::fprintf(stderr, "zeroshell: DRM cards swapped for it: %s\n", swap.c_str());
        return pid > 0;
#endif
    }
    bool external_active() override {
        if (ext_pid_ <= 0) return false;
        if (exited(ext_pid_)) ext_pid_ = -1;
        return ext_pid_ > 0;
    }
    void external_close() override {
        if (ext_pid_ <= 0) return;
        kill_child(ext_pid_, SIGTERM);   // the whole group, SIGKILL after ~1 s
        ext_pid_ = -1;
    }

    // --- Running programs (Settings). Only our own user's: we run as that
    // user, so the kernel wouldn't let us stop anything else anyway.
    std::vector<ProcInfo> processes() override {
#ifdef __EMSCRIPTEN__
        return {};
#else
        return user_processes(getuid(), int(getpid()), proc_prev_);
#endif
    }

    bool process_stop(int pid) override {
#ifdef __EMSCRIPTEN__
        (void)pid;
        return false;
#else
        return stop_user_process(getuid(), int(getpid()), pid);
#endif
    }

    // Date and time: systemd's timedated does the work (timedatectl), which
    // also sets the hardware clock. The polkit rule lets the UI user ask
    // (image/stage-zeroos/files/90-zeroos.rules).
    TimeSettings time_settings() override {
        // The zone may have been changed elsewhere (timedatectl over SSH):
        // with TZ unset, tzset() reads /etc/localtime again if it changed.
        tzset();
        TimeSettings t;
        std::istringstream in(run("timedatectl show -p Timezone -p CanNTP -p NTP -p NTPSynchronized"));
        for (std::string l; std::getline(in, l);) {
            size_t eq = l.find('=');
            if (eq == std::string::npos) continue;
            std::string k = l.substr(0, eq), v = l.substr(eq + 1);
            if (k == "Timezone") t.zone = v;
            else if (k == "CanNTP") t.can_automatic = v == "yes";
            else if (k == "NTP") t.automatic = v == "yes";
            else if (k == "NTPSynchronized") t.synced = v == "yes";
        }
        return t;
    }
    static bool timedatectl(const std::string& args) {
        return run("timedatectl --no-ask-password " + args + " && echo OK").find("OK") != std::string::npos;
    }
    bool set_time_automatic(bool on) override { return timedatectl(on ? "set-ntp true" : "set-ntp false"); }
    bool set_time(int year, int month, int day, int hour, int minute, int second) override {
        char when[32];
        std::snprintf(when, sizeof when, "%04d-%02d-%02d %02d:%02d:%02d", year, month, day, hour, minute, second);
        return timedatectl("set-time " + shell_quote(when));
    }
    std::vector<std::string> time_zones() override {
        std::vector<std::string> v;
        std::istringstream in(run("timedatectl list-timezones"));
        for (std::string l; std::getline(in, l);)
            if (!l.empty()) v.push_back(l);
        return v;
    }
    bool set_time_zone(const std::string& zone) override {
        bool ok = timedatectl("set-timezone " + shell_quote(zone));
        tzset();   // our own clock (the status bar) follows now
        return ok;
    }

    static std::string json_str(const std::string& js, const std::string& key) {
        // Enough for tailscale's flat top-level string fields: "Key": "value"
        auto k = js.find("\"" + key + "\"");
        if (k == std::string::npos) return "";
        auto q1 = js.find('"', js.find(':', k) + 1);
        if (q1 == std::string::npos) return "";
        auto q2 = js.find('"', q1 + 1);
        if (q2 == std::string::npos || js.find('\n', k) < q1) return "";
        return js.substr(q1 + 1, q2 - q1 - 1);
    }

    // The highest listed frequency at or below target (or the lowest one).
    static long pick_freq(const std::string& policy, long target) {
        std::istringstream in(read_str(policy + "scaling_available_frequencies"));
        long f, best = 0, lowest = 0;
        while (in >> f) {
            if (!lowest || f < lowest) lowest = f;
            if (f <= target && f > best) best = f;
        }
        return best ? best : lowest ? lowest : target;
    }

    BatteryInfo read_battery() {
        BatteryInfo b;
        b.percent = int(read_long(bat_ + "capacity", -1));
        std::string st = read_str(bat_ + "status");
        b.charging = st == "Charging";
        b.external_power = st == "Charging" || st == "Full" || st == "Not charging";
        b.voltage_mv = int(read_long(bat_ + "voltage_now") / 1000);
        b.current_ma = int(read_long(bat_ + "current_now") / 1000);
        b.temp_c10 = int(read_long(bat_ + "temp"));
        long secs = read_long(bat_ + (b.charging ? "time_to_full_now" : "time_to_empty_now"), -60);
        b.minutes_left = secs >= 0 ? int(secs / 60) : -1;
        b.cycle_count = int(read_long(bat_ + "cycle_count", -1));
        long full = read_long(bat_ + "charge_full", 0), design = read_long(bat_ + "charge_full_design", 0);
        b.health_percent = design > 0 ? int(full * 100 / design) : -1;
        long uv = read_long(bat_ + "voltage_now"), ua = read_long(bat_ + "current_now");
        b.power_mw = int(std::llabs((long long)uv * ua / 1000000000LL));
        long now = read_long(bat_ + "charge_now", -1000);
        b.charge_now_mah = now >= 0 ? int(now / 1000) : -1;
        b.charge_full_mah = full > 0 ? int(full / 1000) : -1;
        return b;
    }

    ImuSample read_imu() {
        ImuSample s;
        if (accel_.empty()) return s;
        s.available = true;
        double as = read_double(accel_ + "in_accel_scale", 1.0);
        s.ax = float(read_long(accel_ + "in_accel_x_raw") * as);
        s.ay = float(read_long(accel_ + "in_accel_y_raw") * as);
        s.az = float(read_long(accel_ + "in_accel_z_raw") * as);
        double gs = read_double(accel_ + "in_anglvel_scale", 1.0) * 57.2958;
        s.gx = float(read_long(accel_ + "in_anglvel_x_raw") * gs);
        s.gy = float(read_long(accel_ + "in_anglvel_y_raw") * gs);
        s.gz = float(read_long(accel_ + "in_anglvel_z_raw") * gs);
        s.pitch_deg = float(std::atan2(-s.ax, std::sqrt(s.ay * s.ay + s.az * s.az)) * 57.2958);
        s.roll_deg = float(std::atan2(s.ay, s.az) * 57.2958);
        if (!magn_.empty()) {
            // Tilt-compensated heading. Axis orientation to be confirmed on hardware.
            double mx = read_long(magn_ + "in_magn_x_raw"), my = read_long(magn_ + "in_magn_y_raw"),
                   mz = read_long(magn_ + "in_magn_z_raw");
            double p = s.pitch_deg / 57.2958, r = s.roll_deg / 57.2958;
            double xh = mx * std::cos(p) + mz * std::sin(p);
            double yh = mx * std::sin(r) * std::sin(p) + my * std::cos(r) - mz * std::sin(r) * std::cos(p);
            double h = std::atan2(yh, xh) * 57.2958;
            s.heading_deg = float(h < 0 ? h + 360 : h);
        }
        return s;
    }

    // Runs on the status thread: must not touch any member. Three programs
    // at most (it was seven): addresses and radio switches are read directly.
    static NetStatus read_net(const std::string& root, const std::vector<demand::Addr>& addrs) {
        NetStatus n;
        n.wifi_enabled = run("nmcli radio wifi").find("enabled") != std::string::npos;
        // --rescan no: the list as NetworkManager has it. Without it every
        // look older than 30 s started a scan, which costs power and briefly
        // stalls the connection; the Wi-Fi page scans when it is opened.
        // Every adapter's networks (a USB Wi-Fi adapter's too): DEVICE says
        // which one is connected.
        std::istringstream in(run("nmcli -t -f ACTIVE,SSID,SIGNAL,DEVICE dev wifi list --rescan no"));
        std::string line;
        while (std::getline(in, line)) {
            std::vector<std::string> f = nmcli_fields(line);   // unescapes "\:" in the SSID
            if (f.size() >= 3 && f[0] == "yes" && !n.wifi_connected) {
                n.wifi_connected = true;
                n.wifi_ssid = f[1];
                n.wifi_signal = std::atoi(f[2].c_str());
                if (f.size() >= 4) n.wifi_iface = f[3];
            }
        }
        n.ip = demand::primary_address(addrs);
        n.bt_enabled = demand::rfkill_on(root, "bluetooth");
        // Not while bluetoothd is still starting: bluetoothctl would wait for it.
        if (n.bt_enabled && demand::proc_running(root, "bluetoothd")) {
            std::istringstream bt(run("bluetoothctl devices Connected 2>/dev/null"));
            for (std::string l; std::getline(bt, l);)
                if (l.rfind("Device ", 0) == 0) ++n.bt_connected;
        }
        n.ethernet = read_str("/sys/class/net/eth0/operstate") == "up";
        // Online through a phone: a Bluetooth network link (bnep0), or a USB
        // network interface from a phone's driver (Android: rndis_host or
        // cdc_ncm; iPhone: ipheth).
        std::string usb_phone;
        for (auto& i : list_dir("/sys/class/net/")) {
            std::string base = "/sys/class/net/" + i;
            if (i.rfind("bnep", 0) == 0) {
                if (n.tether.empty()) n.tether = "bluetooth";
                continue;
            }
            char link[256];
            ssize_t len = readlink((base + "/device/driver").c_str(), link, sizeof link - 1);
            if (len <= 0) continue;
            std::string driver(link, size_t(len));
            driver = driver.substr(driver.rfind('/') + 1);
            if (driver != "rndis_host" && driver != "ipheth" && driver != "cdc_ncm") continue;
            usb_phone = i;
            if (read_str(base + "/carrier") == "1") n.tether = "usb";
        }
        n.usb_gadget = usb_phone != "usb0" && read_str("/sys/class/net/usb0/operstate") == "up";
        n.tailscale_ip = demand::ipv4_of(addrs, "tailscale0");
        return n;
    }

    pid_t scan_pid_ = -1;
    int scan_fd_ = -1;
    CamState scan_state_ = CamState::Off;
    std::vector<uint8_t> scan_front_, scan_back_;
    size_t scan_fill_ = 0;
    uint32_t scan_seq_ = 0;
    time_t scan_want_ = 0;
    pid_t tether_pid_ = -1;
    int tether_fd_ = -1;
    bool tether_status_job_ = false, tether_found_seen_ = false;
    std::string tether_buf_;
    TetherStatus tether_, tether_next_;
    time_t tether_want_ = 0, tether_at_ = 0;
    bool dock_cache_ = false;
    TsStatus ts_cache_;
    SyncStatus sync_cache_;
    std::vector<std::string> ext5v_;
    int gps_fd_ = -1;
    bool gps_sent_ = false;
    time_t gps_want_ = 0, gps_try_ = 0, mesh_at_ = 0;
    std::string gps_buf_;
    GpsFix gps_;
    MeshStatus mesh_cache_;
    int mesh_fd_ = -1, mesh_seq_ = 0;
    bool mesh_wanted_ = false, mesh_no_radio_ = false;
    time_t mesh_log_at_ = 0, mesh_err_at_ = 0, mesh_fail_until_ = 0;
    int mesh_restarts_ = -1;
    std::string mesh_err_;
    int mesh_log_checks_ = 3;   // none until mesh_start
    bool mesh_hello_ = false, mesh_connected_ = false;
    time_t mesh_try_ = 0, mesh_beat_ = 0;
    uint32_t mesh_me_ = 0, mesh_packet_id_ = uint32_t(time(nullptr)) & 0xfffff;
    mt::Stream mesh_in_;
    std::string mesh_out_;   // frames not yet taken by the socket
    time_t mesh_started_at_ = 0;
    std::map<uint32_t, std::string> mesh_names_;
    std::vector<MeshMessage> mesh_log_;
    time_t sync_at_ = 0, sync_run_at_ = 0;
    bool sync_running_ = false;
    time_t ts_at_ = 0, term_at_ = 0;
    bool term_cache_ = false;
    long dock_calls_ = 0;
    std::string bat_, accel_, magn_, backlight_;
    BatteryInfo bat_cache_;
    ImuSample imu_cache_;
    NetStatus net_cache_;          // UI thread's copy
    NetStatus net_shared_;         // from the network thread, under net_mu_
    bool net_new_ = false, net_stop_ = false, net_now_ = false;
    std::mutex net_mu_;
    std::condition_variable net_cv_;
    std::thread net_thread_;
    std::atomic<bool> screen_on_{true};
    // Services started when needed (demand_step). Set once, in the constructor:
    std::string sys_root_;   // "" on the device; the tests' made-up /sys and /proc
    bool demand_bt_ = false, demand_audio_ = false;
    long demand_grace_s_ = 60;   // unneeded this long: stopped
    // The status thread's own:
    demand::Keeper bt_keeper_{60}, audio_keeper_{60};
    bool audio_learned_ = false, audio_no_socket_ = false;
    // Shared between the UI and the status thread:
    std::atomic<long> bt_used_{0}, audio_used_{0};   // steady_s() of the last use
    std::atomic<bool> audio_hold_{false};            // ZeroShell is playing or recording
    std::atomic<bool> audio_up_{false};              // PipeWire is running
    std::atomic<bool> audio_apply_{false};           // the cached volume is to be set
    std::mutex audio_mu_;                            // writes of the three below
    std::atomic<int> vol_cache_{60};
    std::atomic<bool> mute_cache_{false};
    std::atomic<unsigned> audio_seq_{0};             // counts set_volume and set_muted
    // The UI thread's own:
    bool vol_known_ = false;                         // wpctl answered at least once
    int vol_saved_ = 60, vol_seen_ = 60;             // in settings.conf; last looked at
    bool mute_saved_ = false, mute_seen_ = false;
    time_t vol_changed_at_ = 0;
    std::chrono::steady_clock::time_point vol_read_at_{};   // PipeWire was last asked then
};

}  // namespace

std::unique_ptr<Hal> make_linux_hal() { return std::make_unique<LinuxHal>(); }

static std::unique_ptr<Hal> g_hal;
Hal& hal() { return *g_hal; }
void set_hal(std::unique_ptr<Hal> h) { g_hal = std::move(h); }

}  // namespace zs
