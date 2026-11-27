// What LinuxHal can learn without starting a program, and when a service
// nothing needs is stopped (docs/PERFORMANCE.md, "Started when needed").
//
// Two jobs, both on LinuxHal's status thread:
//
//   Status. nmcli and bluetoothctl used to run every 2 s to fill the status
//   bar. net_fingerprint() is a few small reads of /sys instead; the programs
//   only run when it changes.
//
//   Services. bluetoothd runs while Bluetooth is on, PipeWire while something
//   plays, records or sets the volume (or Bluetooth is on: a headset or a
//   phone needs it there before it connects). Keeper decides when one that is
//   no longer needed has been idle long enough to stop.
//
// No LVGL and no LinuxHal here, and every path takes a root folder, so it is
// unit tested against a made-up /sys and /proc (shell/tests/demand_test.cpp).
#pragma once

#include <arpa/inet.h>
#include <dirent.h>
#ifndef __EMSCRIPTEN__
#include <ifaddrs.h>
#include <net/if.h>
#endif
#include <netinet/in.h>
#include <sys/socket.h>

#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <fstream>
#include <string>
#include <vector>

namespace zs::demand {

// First line of a file, "" if it can't be read.
inline std::string first_line(const std::string& path) {
    std::ifstream f(path);
    std::string s;
    if (f) std::getline(f, s);
    return s;
}

// Names in a folder, sorted (readdir's order isn't stable), without . files.
inline std::vector<std::string> names(const std::string& dir) {
    std::vector<std::string> v;
    if (DIR* d = opendir(dir.c_str())) {
        while (dirent* e = readdir(d))
            if (e->d_name[0] != '.') v.push_back(e->d_name);
        closedir(d);
    }
    std::sort(v.begin(), v.end());
    return v;
}

// --- Radios

// True if a radio of this type ("wlan", "bluetooth") isn't soft-blocked: what
// "rfkill list <type>" prints as "Soft blocked: no".
inline bool rfkill_on(const std::string& root, const std::string& type) {
    const std::string base = root + "/sys/class/rfkill/";
    for (auto& n : names(base))
        if (first_line(base + n + "/type") == type && first_line(base + n + "/soft") == "0") return true;
    return false;
}

// --- Addresses

struct Addr {
    std::string iface;
    std::string text;   // 192.168.1.20, fd00::1
    bool v4 = true;
    bool up = false;    // the interface is up
};

// Every address but loopback's and IPv6 link-local ones, in the kernel's order.
inline std::vector<Addr> addresses() {
    std::vector<Addr> v;
#ifndef __EMSCRIPTEN__   // the browser build has no network interfaces to list
    ifaddrs* all = nullptr;
    if (getifaddrs(&all) != 0) return v;
    for (ifaddrs* a = all; a; a = a->ifa_next) {
        if (!a->ifa_addr || !a->ifa_name || (a->ifa_flags & IFF_LOOPBACK)) continue;
        char buf[INET6_ADDRSTRLEN] = {0};
        Addr x;
        if (a->ifa_addr->sa_family == AF_INET) {
            inet_ntop(AF_INET, &reinterpret_cast<sockaddr_in*>(a->ifa_addr)->sin_addr, buf, sizeof buf);
        } else if (a->ifa_addr->sa_family == AF_INET6) {
            const in6_addr& six = reinterpret_cast<sockaddr_in6*>(a->ifa_addr)->sin6_addr;
            if (IN6_IS_ADDR_LINKLOCAL(&six) || IN6_IS_ADDR_MC_LINKLOCAL(&six)) continue;
            inet_ntop(AF_INET6, &six, buf, sizeof buf);
            x.v4 = false;
        } else {
            continue;
        }
        x.iface = a->ifa_name;
        x.text = buf;
        x.up = a->ifa_flags & IFF_UP;
        v.push_back(x);
    }
    freeifaddrs(all);
#endif
    return v;
}

// The address "hostname -I" prints first: the first one of an interface that is up.
inline std::string primary_address(const std::vector<Addr>& v) {
    for (auto& a : v)
        if (a.up) return a.text;
    return "";
}

// An interface's IPv4 address ("ip -4 addr show dev <iface>"), "" if it has none.
inline std::string ipv4_of(const std::vector<Addr>& v, const std::string& iface) {
    for (auto& a : v)
        if (a.v4 && a.iface == iface) return a.text;
    return "";
}

// --- Network

// One string that changes whenever something the status bar shows may have:
// a radio switched on or off, a network joined or left (carrier_changes
// counts every time, so a drop between two looks isn't missed), an address,
// a Bluetooth link (the kernel lists each one as hci0:<handle>). Comparing it
// costs a few dozen small reads of /sys; finding out what changed is nmcli's
// and bluetoothctl's job, and they then run once.
inline std::string net_fingerprint(const std::string& root, const std::vector<Addr>& addrs) {
    std::string fp;
    const std::string rf = root + "/sys/class/rfkill/";
    for (auto& n : names(rf))
        fp += n + ":" + first_line(rf + n + "/type") + ":" + first_line(rf + n + "/soft") +
              first_line(rf + n + "/hard") + ";";
    fp += "|";
    const std::string net = root + "/sys/class/net/";
    for (auto& n : names(net)) {
        if (n == "lo") continue;
        fp += n + ":" + first_line(net + n + "/operstate") + ":" + first_line(net + n + "/carrier_changes") + ";";
    }
    fp += "|";
    for (auto& a : addrs) fp += a.iface + "=" + a.text + (a.up ? "" : "(down)") + ";";
    fp += "|";
    for (auto& n : names(root + "/sys/class/bluetooth/")) fp += n + ";";
    return fp;
}

// --- Sound

// True while a sound device is open for playing or recording, by PipeWire for
// one of its clients or by a program directly. A device nobody uses says
// "closed" (PipeWire lets go of one a few seconds after the last sound).
inline bool pcm_open(const std::string& root) {
    const std::string base = root + "/proc/asound/";
    for (auto& card : names(base)) {
        if (card.rfind("card", 0) != 0) continue;
        for (auto& pcm : names(base + card)) {
            if (pcm.rfind("pcm", 0) != 0) continue;
            for (auto& sub : names(base + card + "/" + pcm)) {
                if (sub.rfind("sub", 0) != 0) continue;
                std::string st = first_line(base + card + "/" + pcm + "/" + sub + "/status");
                if (!st.empty() && st != "closed") return true;
            }
        }
    }
    return false;
}

// "Volume: 0.60 [MUTED]" from "wpctl get-volume". False if that isn't what
// the text is: no sound server, or one that has no speaker yet.
inline bool parse_wpctl_volume(const std::string& out, int& pct, bool& muted) {
    auto p = out.find("Volume:");
    if (p == std::string::npos) return false;
    const char* s = out.c_str() + p + 7;
    char* end = nullptr;
    double v = std::strtod(s, &end);
    if (end == s) return false;
    pct = int(v * 100 + 0.5);
    muted = out.find("MUTED") != std::string::npos;
    return true;
}

// "pw-cli ls Node": true if a program has a stream open on PipeWire, playing,
// recording or paused. PipeWire is not stopped under one: a paused game or
// player would find its sound gone when it carries on.
inline bool has_streams(const std::string& pw_nodes) {
    return pw_nodes.find("media.class = \"Stream/") != std::string::npos;
}

// --- Programs

// For each name, whether a process with that name (the kernel's "comm") is
// running. One pass over /proc however many names.
inline std::vector<bool> procs_running(const std::string& root, const std::vector<std::string>& comms) {
    std::vector<bool> found(comms.size(), false);
    const std::string base = root + "/proc/";
    if (DIR* d = opendir(base.c_str())) {
        while (dirent* e = readdir(d)) {
            if (!std::isdigit((unsigned char)e->d_name[0])) continue;
            std::string comm = first_line(base + e->d_name + "/comm");
            for (size_t i = 0; i < comms.size(); ++i)
                if (comm == comms[i]) found[i] = true;
        }
        closedir(d);
    }
    return found;
}
inline bool proc_running(const std::string& root, const std::string& comm) {
    return procs_running(root, {comm})[0];
}

// --- When to stop a service nothing needs

// One per service. step() is asked every couple of seconds with what is true
// now and answers what to do. Times are seconds from any steady clock.
//
// A service is stopped once it has been running, unneeded, for grace_s. If it
// comes back by itself soon after (something we can't see uses it: a script
// that asks BlueZ a question every few seconds starts bluetoothd again each
// time), the next wait is twice as long, up to kMaxGrace, so it is never
// stopped and started in a loop. A stop that stuck for kCalm resets the wait.
class Keeper {
public:
    enum class Act { None, Start, Stop };
    static constexpr long kMaxGrace = 1800, kCalm = 300, kStartEvery = 30;

    explicit Keeper(long grace_s) : base_(grace_s), grace_(grace_s) {}

    // running: the service's process is there. needed: something uses it now.
    // start: when needed and not running, start it (false for a service its
    // users start themselves, through its socket).
    Act step(long now, bool running, bool needed, bool start) {
        if (running) started_at_ = -1;       // the last start worked: the next needn't wait
        if (stopped_at_ >= 0 && running) {   // back, and not by us
            grace_ = now - stopped_at_ < kCalm ? std::min(grace_ * 2, kMaxGrace) : base_;
            stopped_at_ = -1;
        }
        if (needed) {
            idle_since_ = -1;
            if (running || !start) return Act::None;
            if (started_at_ >= 0 && now - started_at_ < kStartEvery) return Act::None;   // it didn't start: not every 2 s
            started_at_ = now;
            stopped_at_ = -1;   // back by us: not a reason to wait longer next time
            return Act::Start;
        }
        if (!running) {
            idle_since_ = -1;
            return Act::None;
        }
        if (idle_since_ < 0) idle_since_ = now;
        if (now - idle_since_ < grace_) return Act::None;
        idle_since_ = -1;
        stopped_at_ = now;
        return Act::Stop;
    }

    // The caller looked closer after a Stop and found a user: not stopped,
    // and the wait starts again.
    void keep() { stopped_at_ = -1; }

    long grace() const { return grace_; }

private:
    long base_, grace_;
    long idle_since_ = -1;   // running and unneeded since then
    long stopped_at_ = -1;   // we stopped it then, and haven't seen it since
    long started_at_ = -1;
};

}  // namespace zs::demand
