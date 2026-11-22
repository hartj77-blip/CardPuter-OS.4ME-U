// zeroos-grove-ledd: lights a NeoPixel (WS2812 / SK6812) strip on the Grove
// port (docs/GROVE.md). Runs as root, started by its systemd socket when the
// Grove app (or a script) connects to /run/zeroos-grove-ledd.sock.
//
// Talk to it one line at a time; each line gets one reply, "ok ..." or
// "error <why>":
//   set effect=rainbow color=ff0000 brightness=25 speed=5 count=15
//   set effect=pixels pixels=ff0000,00ff00,0000ff
//   off
//   status      -> ok <settings> hardware=on|off|error [error=<why>]
//
// The strip keeps its effect after the client goes. With the strip off, the
// Grove pins go back to the UART (for a Grove GPS) within two seconds, and
// the service exits after a minute with no one connected.
#include <fcntl.h>
#include <glob.h>
#include <poll.h>
#include <signal.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/un.h>
#include <unistd.h>
#include <grp.h>

#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <memory>
#include <string>
#include <vector>

#include "grove/leds.h"
#include "smi_ws2812.h"

namespace {
using namespace zs;
using Clock = std::chrono::steady_clock;

volatile sig_atomic_t g_stop = 0;
void on_signal(int) { g_stop = 1; }

struct Options {
    std::string socket = "/run/zeroos-grove-ledd.sock";
    ledd::SmiOptions smi;
    int max_ma = 500;
    bool dry_run = false;   // no hardware: print frames (tests, the simulator)
};

// The strip: the SMI driver, or for --dry-run a log on stdout.
class Strip {
public:
    explicit Strip(const Options& o) : o_(o) {}
    bool ready() const { return o_.dry_run ? dry_open_ : smi_.is_open(); }
    bool open() {
        if (o_.dry_run) {
            dry_open_ = true;
            std::printf("strip: open\n");
            std::fflush(stdout);
            return true;
        }
        ext_5v_on();
        return smi_.open(o_.smi);
    }
    bool show(const std::vector<leds::Rgb>& px) {
        if (o_.dry_run) {
            std::string l;
            for (auto& p : px) l += leds::hex(p) + " ";
            if (l != last_) std::printf("strip: %s\n", l.c_str());
            last_ = l;
            std::fflush(stdout);
            return true;
        }
        grb_.clear();
        for (auto& p : px) {   // WS2812 and SK6812 take green, red, blue
            grb_.push_back(p.g);
            grb_.push_back(p.r);
            grb_.push_back(p.b);
        }
        return smi_.show(grb_);
    }
    void close() {
        if (o_.dry_run) {
            if (dry_open_) std::printf("strip: closed\n");
            std::fflush(stdout);
            dry_open_ = false;
            last_.clear();
            return;
        }
        smi_.close();
    }
    std::string error() const { return o_.dry_run ? "" : smi_.error(); }

private:
    // The expansion port's 5 V (an LED-class switch, ext_5v_out on M5's
    // image): on whenever the strip is lit. Left on afterwards, as the GPS
    // or the mesh may be using it; ZeroShell turns it off.
    static void ext_5v_on() {
        glob_t g{};
        if (glob("/sys/class/leds/*5v*", 0, nullptr, &g) == 0) {
            for (size_t i = 0; i < g.gl_pathc; ++i) {
                std::string d = g.gl_pathv[i];
                std::string max = "1";
                std::ifstream(d + "/max_brightness") >> max;
                std::ofstream(d + "/brightness") << max;
            }
        }
        globfree(&g);
    }

    const Options& o_;
    ledd::SmiStrip smi_;
    std::vector<uint8_t> grb_;
    bool dry_open_ = false;
    std::string last_;
};

struct Client {
    int fd;
    std::string in;
};

bool send_all(int fd, const std::string& s) {
    size_t off = 0;
    while (off < s.size()) {
        ssize_t n = send(fd, s.data() + off, s.size() - off, MSG_NOSIGNAL);
        if (n <= 0) return false;
        off += size_t(n);
    }
    return true;
}

int listen_socket(const Options& o, bool* activated) {
    // systemd socket activation: our listening socket is fd 3.
    const char* pid = std::getenv("LISTEN_PID");
    const char* fds = std::getenv("LISTEN_FDS");
    if (pid && fds && std::atoi(pid) == getpid() && std::atoi(fds) >= 1) {
        *activated = true;
        fcntl(3, F_SETFD, FD_CLOEXEC);
        return 3;
    }
    *activated = false;
    int fd = socket(AF_UNIX, SOCK_STREAM | SOCK_CLOEXEC, 0);
    sockaddr_un a{};
    a.sun_family = AF_UNIX;
    if (fd < 0 || o.socket.size() >= sizeof a.sun_path) return -1;
    std::strcpy(a.sun_path, o.socket.c_str());
    unlink(o.socket.c_str());
    if (bind(fd, reinterpret_cast<sockaddr*>(&a), sizeof a) < 0 || listen(fd, 4) < 0) return -1;
    // Started by hand as root: let the gpio group (ZeroShell's user) in.
    if (group* g = getgrnam("gpio"); g && geteuid() == 0) {
        if (chown(o.socket.c_str(), 0, g->gr_gid) == 0) chmod(o.socket.c_str(), 0660);
    }
    return fd;
}

void usage() {
    std::fprintf(stderr,
                 "usage: zeroos-grove-ledd [--socket PATH] [--pins 14,15] [--dma N] [--max-ma N] [--dry-run]\n"
                 "Lights a NeoPixel strip on the Cardputer Zero's Grove port (docs/GROVE.md).\n");
}

}  // namespace

int main(int argc, char** argv) {
    Options o;
    for (int i = 1; i < argc; ++i) {
        std::string a = argv[i];
        auto next = [&]() -> std::string {
            if (i + 1 >= argc) {
                usage();
                std::exit(2);
            }
            return argv[++i];
        };
        if (a == "--socket") o.socket = next();
        else if (a == "--dma") o.smi.dma_channel = std::atoi(next().c_str());
        else if (a == "--max-ma") o.max_ma = std::atoi(next().c_str());
        else if (a == "--dry-run") o.dry_run = true;
        else if (a == "--pins") {
            o.smi.pins.clear();
            std::string v = next();
            for (size_t p = 0; p < v.size();) {
                size_t q = v.find(',', p);
                if (q == std::string::npos) q = v.size();
                o.smi.pins.push_back(std::atoi(v.substr(p, q - p).c_str()));
                p = q + 1;
            }
        } else {
            usage();
            return a == "--help" || a == "-h" ? 0 : 2;
        }
    }
    o.smi.max_leds = leds::kMaxLeds;
    signal(SIGTERM, on_signal);
    signal(SIGINT, on_signal);
    signal(SIGPIPE, SIG_IGN);

    bool activated = false;
    int lfd = listen_socket(o, &activated);
    if (lfd < 0) {
        std::fprintf(stderr, "zeroos-grove-ledd: can't listen on %s: %s\n", o.socket.c_str(), std::strerror(errno));
        return 1;
    }

    Strip strip(o);
    leds::Settings s;
    std::vector<leds::Rgb> px;
    std::vector<Client> clients;
    std::string hw_error;
    auto start = Clock::now();
    auto last_show = Clock::time_point{}, off_since = Clock::now(), alone_since = Clock::now(),
         retry_at = Clock::time_point{};
    bool dirty = true;

    auto ms_since = [](Clock::time_point t) {
        return std::chrono::duration_cast<std::chrono::milliseconds>(Clock::now() - t).count();
    };
    auto status = [&]() {
        std::string l = "ok " + leds::to_line(s) + " hardware=" +
                        (strip.ready() ? "on" : hw_error.empty() ? "off" : "error");
        if (!hw_error.empty()) l += " error=" + hw_error;
        return l;
    };
    auto handle = [&](const std::string& line) -> std::string {
        std::string cmd = line.substr(0, line.find(' '));
        std::string rest = line.size() > cmd.size() ? line.substr(cmd.size() + 1) : "";
        if (cmd == "set") {
            std::string e = leds::apply_line(rest, &s);
            if (!e.empty()) return "error " + e;
            dirty = true;
            if (s.effect != leds::Effect::Off) retry_at = {};   // try the hardware again now
            return status();
        }
        if (cmd == "off") {
            s.effect = leds::Effect::Off;
            dirty = true;
            return status();
        }
        if (cmd == "status") return status();
        if (cmd == "help") return "ok commands: set key=value..., off, status";
        return "error unknown command '" + cmd + "' (set, off, status)";
    };

    while (!g_stop) {
        bool lit = s.effect != leds::Effect::Off;
        if (lit) off_since = Clock::now();

        // Draw: animations at ~50 fps, still pictures again every 2 s (a
        // strip plugged in late, or a glitch, puts itself right).
        if (lit && !strip.ready() && Clock::now() >= retry_at) {
            if (strip.open()) {
                hw_error.clear();
            } else {
                hw_error = strip.error();
                std::fprintf(stderr, "zeroos-grove-ledd: %s\n", hw_error.c_str());
                retry_at = Clock::now() + std::chrono::seconds(5);
            }
            dirty = true;
        }
        if (strip.ready()) {
            if (dirty || leds::animated(s.effect) || ms_since(last_show) > 2000) {
                leds::render(s, uint32_t(ms_since(start)), &px, o.max_ma);
                if (!lit) px.assign(px.size(), leds::Rgb{});
                if (!strip.show(px)) {
                    hw_error = strip.error();
                    std::fprintf(stderr, "zeroos-grove-ledd: %s\n", hw_error.c_str());
                }
                last_show = Clock::now();
                dirty = false;
            }
            // Off for 2 s: dark strip, pins back to the UART.
            if (!lit && ms_since(off_since) > 2000) strip.close();
        }
        if (!lit && clients.empty() && activated && ms_since(alone_since) > 60000 && ms_since(off_since) > 60000)
            break;   // systemd starts us again on the next connection

        int timeout = strip.ready() && leds::animated(s.effect) ? 20 : strip.ready() || lit ? 500 : 5000;
        std::vector<pollfd> pf{{lfd, POLLIN, 0}};
        for (auto& c : clients) pf.push_back({c.fd, POLLIN, 0});
        if (poll(pf.data(), pf.size(), timeout) < 0) continue;

        if (pf[0].revents & POLLIN) {
            int c = accept4(lfd, nullptr, nullptr, SOCK_CLOEXEC);
            if (c >= 0) {
                if (clients.size() >= 8) close(c);
                else clients.push_back({c, ""});
            }
        }
        for (size_t i = pf.size() - 1; i >= 1; --i) {
            if (!pf[i].revents) continue;
            Client& c = clients[i - 1];
            char buf[2048];
            ssize_t n = recv(c.fd, buf, sizeof buf, 0);
            bool gone = n <= 0;
            if (n > 0) c.in.append(buf, size_t(n));
            size_t nl;
            while (!gone && (nl = c.in.find('\n')) != std::string::npos) {
                std::string line = c.in.substr(0, nl);
                c.in.erase(0, nl + 1);
                if (!line.empty() && line.back() == '\r') line.pop_back();
                if (line.empty()) continue;
                gone = !send_all(c.fd, handle(line) + "\n");
            }
            if (c.in.size() > 8192) gone = true;   // no line that long is ours
            if (gone) {
                close(c.fd);
                clients.erase(clients.begin() + long(i - 1));
                if (clients.empty()) alone_since = Clock::now();
            }
        }
    }

    // Stopping: dark strip, pins back.
    if (strip.ready()) {
        leds::Settings dark = s;
        dark.effect = leds::Effect::Off;
        leds::render(dark, 0, &px);
        strip.show(px);
    }
    strip.close();
    for (auto& c : clients) close(c.fd);
    if (!activated) unlink(o.socket.c_str());
    return 0;
}
