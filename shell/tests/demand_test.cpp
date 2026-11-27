// Checks what LinuxHal reads instead of starting programs, and when it stops
// a service nothing needs (hal/demand.h), against a made-up /sys and /proc.
// Build and run: shell/tests/run.sh
#include <sys/stat.h>
#include <unistd.h>

#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <string>

#include "hal/demand.h"

using namespace zs::demand;

namespace {
int g_failed = 0;

void check(bool ok, const char* what) {
    if (ok) return;
    std::printf("FAIL %s\n", what);
    ++g_failed;
}

std::string g_root;

void put(const std::string& path, const std::string& text) {
    std::string full = g_root + path;
    for (size_t i = g_root.size() + 1; (i = full.find('/', i)) != std::string::npos; ++i)
        mkdir(full.substr(0, i).c_str(), 0755);
    std::ofstream(full) << text << "\n";
}
void drop(const std::string& path) { unlink((g_root + path).c_str()); }

using Act = Keeper::Act;
}  // namespace

int main() {
    char tmpl[] = "/tmp/zs-demand-XXXXXX";
    g_root = mkdtemp(tmpl);

    // --- radios: "Soft blocked: no" of any radio of the type
    check(!rfkill_on(g_root, "bluetooth"), "no radios: off");
    put("/sys/class/rfkill/rfkill0/type", "bluetooth");
    put("/sys/class/rfkill/rfkill0/soft", "1");
    put("/sys/class/rfkill/rfkill0/hard", "0");
    put("/sys/class/rfkill/rfkill1/type", "wlan");
    put("/sys/class/rfkill/rfkill1/soft", "0");
    put("/sys/class/rfkill/rfkill1/hard", "0");
    check(!rfkill_on(g_root, "bluetooth"), "Bluetooth blocked: off");
    check(rfkill_on(g_root, "wlan"), "Wi-Fi not blocked: on");

    // --- the fingerprint changes with everything the status bar shows
    put("/sys/class/net/lo/operstate", "unknown");
    put("/sys/class/net/wlan0/operstate", "up");
    put("/sys/class/net/wlan0/carrier_changes", "2");
    std::vector<Addr> addrs = {{"wlan0", "192.168.1.20", true, true}};
    std::string fp = net_fingerprint(g_root, addrs);
    check(fp == net_fingerprint(g_root, addrs), "nothing changed: same fingerprint");

    put("/sys/class/rfkill/rfkill0/soft", "0");
    check(rfkill_on(g_root, "bluetooth"), "Bluetooth unblocked: on");
    std::string fp_bt = net_fingerprint(g_root, addrs);
    check(fp_bt != fp, "Bluetooth switched on: changed");

    put("/sys/class/net/wlan0/carrier_changes", "4");   // dropped and came back between two looks
    std::string fp_drop = net_fingerprint(g_root, addrs);
    check(fp_drop != fp_bt, "Wi-Fi dropped and rejoined: changed");

    std::vector<Addr> moved = {{"wlan0", "10.0.0.7", true, true}};
    check(net_fingerprint(g_root, moved) != fp_drop, "new address: changed");

    put("/sys/class/net/eth0/operstate", "up");
    std::string fp_eth = net_fingerprint(g_root, addrs);
    check(fp_eth != fp_drop, "cable plugged in: changed");

    put("/sys/class/bluetooth/hci0/x", "");
    std::string fp_hci = net_fingerprint(g_root, addrs);
    put("/sys/class/bluetooth/hci0:256/x", "");   // a headset connected
    check(net_fingerprint(g_root, addrs) != fp_hci, "Bluetooth device connected: changed");

    put("/sys/class/net/lo/operstate", "down");
    put("/sys/class/bluetooth/hci0/y", "");
    check(net_fingerprint(g_root, addrs).find("lo:") == std::string::npos, "loopback isn't in it");

    // --- addresses: what "hostname -I" and "ip -4 addr show dev" answered
    std::vector<Addr> a = {{"eth0", "192.168.1.5", true, false},
                           {"wlan0", "192.168.1.20", true, true},
                           {"tailscale0", "fd7a::1", false, true},
                           {"tailscale0", "100.64.0.9", true, true}};
    check(primary_address(a) == "192.168.1.20", "first address of an interface that is up");
    check(primary_address({}).empty(), "no address");
    check(ipv4_of(a, "tailscale0") == "100.64.0.9", "Tailscale's IPv4 address, not its IPv6 one");
    check(ipv4_of(a, "usb0").empty(), "an interface without one");
    for (auto& x : addresses()) check(x.iface != "lo" && !x.text.empty(), "real addresses: no loopback, none empty");

    // --- sound devices
    check(!pcm_open(g_root), "no sound card: nothing open");
    put("/proc/asound/card0/pcm0p/sub0/status", "closed");
    put("/proc/asound/card0/pcm0c/sub0/status", "closed");
    put("/proc/asound/card0/id", "ES8389");
    check(!pcm_open(g_root), "all closed");
    put("/proc/asound/card0/pcm0p/sub0/status", "state: RUNNING\nowner_pid   : 812");
    check(pcm_open(g_root), "playing");
    put("/proc/asound/card0/pcm0p/sub0/status", "closed");
    put("/proc/asound/card0/pcm0c/sub0/status", "state: PREPARED");
    check(pcm_open(g_root), "open for recording");

    // --- wpctl and pw-cli
    int pct = -1;
    bool muted = true;
    check(parse_wpctl_volume("Volume: 0.60\n", pct, muted) && pct == 60 && !muted, "Volume: 0.60");
    check(parse_wpctl_volume("Volume: 0.35 [MUTED]\n", pct, muted) && pct == 35 && muted, "Volume: 0.35 [MUTED]");
    check(parse_wpctl_volume("Volume: 1.00\n", pct, muted) && pct == 100, "Volume: 1.00");
    check(!parse_wpctl_volume("", pct, muted), "no answer");
    check(!parse_wpctl_volume("Translate ID error: '@DEFAULT_AUDIO_SINK@' is not a valid ID\n", pct, muted),
          "no speaker yet");
    check(!parse_wpctl_volume("Volume: \n", pct, muted), "no number");
    const char* idle = "\tid 52, type PipeWire:Interface:Node/3\n"
                       " \t\tnode.name = \"alsa_output.platform-soc_sound.stereo-fallback\"\n"
                       " \t\tmedia.class = \"Audio/Sink\"\n";
    check(!has_streams(idle), "only the speaker: no streams");
    check(has_streams(std::string(idle) + "\tid 71, type PipeWire:Interface:Node/3\n"
                                          " \t\tnode.name = \"aplay\"\n"
                                          " \t\tmedia.class = \"Stream/Output/Audio\"\n"),
          "a program's stream");

    // --- programs
    check(!proc_running(g_root, "bluetoothd"), "not running");
    put("/proc/1/comm", "systemd");
    put("/proc/433/comm", "bluetoothd");
    put("/proc/901/comm", "pipewire-pulse");
    check(proc_running(g_root, "bluetoothd"), "running");
    check(!proc_running(g_root, "pipewire"), "pipewire-pulse isn't pipewire");
    {
        std::vector<bool> up = procs_running(g_root, {"pipewire", "bluetoothd", "systemd"});
        check(!up[0] && up[1] && up[2], "several names in one pass");
    }
    drop("/proc/433/comm");
    check(!proc_running(g_root, "bluetoothd"), "gone");

    // --- Keeper: stopped after the wait, not before
    {
        Keeper k(60);
        check(k.step(1000, true, true, false) == Act::None, "needed: left alone");
        check(k.step(1002, true, false, false) == Act::None, "just became unneeded");
        check(k.step(1060, true, false, false) == Act::None, "58 s: not yet");
        check(k.step(1062, true, false, false) == Act::Stop, "60 s unneeded: stop");
        check(k.step(1064, false, false, false) == Act::None, "stopped: nothing to do");
        check(k.step(5000, false, false, false) == Act::None, "still stopped");
    }
    // Needed again in the middle: the wait starts over.
    {
        Keeper k(60);
        k.step(1000, true, false, false);
        check(k.step(1050, true, true, false) == Act::None, "needed again");
        check(k.step(1052, true, false, false) == Act::None, "unneeded again");
        check(k.step(1100, true, false, false) == Act::None, "48 s since: not yet");
        check(k.step(1112, true, false, false) == Act::Stop, "60 s since: stop");
    }
    // Started when needed, if it is ours to start, and not every 2 s when it won't.
    {
        Keeper k(60);
        check(k.step(1000, false, true, false) == Act::None, "its users start it: not us");
        check(k.step(1002, false, true, true) == Act::Start, "ours to start");
        check(k.step(1004, false, true, true) == Act::None, "it didn't start: wait");
        check(k.step(1030, false, true, true) == Act::None, "28 s: still waiting");
        check(k.step(1032, false, true, true) == Act::Start, "30 s: try again");
        check(k.step(1034, true, true, true) == Act::None, "running");
    }
    // Started, stopped when unneeded, needed again soon after: started at
    // once (the wait before another try is for a start that failed), and
    // our own start doesn't count as "it came back by itself".
    {
        Keeper k(60);
        check(k.step(1000, false, true, true) == Act::Start, "needed: start");
        check(k.step(1002, true, false, true) == Act::None, "running, no longer needed");
        check(k.step(1062, true, false, true) == Act::Stop, "stopped a minute later");
        check(k.step(1064, false, true, true) == Act::Start, "needed again 2 s later: started at once");
        check(k.step(1066, true, false, true) == Act::None, "running");
        check(k.grace() == 60, "the wait is unchanged");
    }
    // Something we can't see starts it again each time: the wait doubles,
    // up to half an hour, so it isn't stopped and started in a loop.
    {
        Keeper k(60);
        long t = 1000;
        k.step(t, true, false, false);
        check(k.step(t += 60, true, false, false) == Act::Stop, "first stop");
        check(k.step(t += 4, true, false, false) == Act::None, "back after 4 s");
        check(k.grace() == 120, "wait doubled");
        check(k.step(t += 100, true, false, false) == Act::None, "100 s: not yet");
        check(k.step(t += 20, true, false, false) == Act::Stop, "120 s: stop");
        for (int i = 0; i < 10; ++i) {
            k.step(t += 4, true, false, false);          // back again
            while (k.step(t += 10, true, false, false) != Act::Stop) {}
        }
        check(k.grace() == Keeper::kMaxGrace, "never more than half an hour");
        // It stayed stopped for a long time, then was used and left: back to the short wait.
        k.step(t += 4000, true, true, false);
        check(k.grace() == 60, "a stop that stuck: short wait again");
    }
    // The closer look found a user: not a stop, and nothing doubles.
    {
        Keeper k(60);
        k.step(1000, true, false, false);
        check(k.step(1060, true, false, false) == Act::Stop, "would stop");
        k.keep();
        check(k.step(1062, true, false, false) == Act::None, "kept");
        check(k.grace() == 60, "the wait is unchanged");
        check(k.step(1122, true, false, false) == Act::Stop, "asked again a minute later");
    }

    if (std::system(("rm -rf '" + g_root + "'").c_str())) {}
    if (g_failed) return 1;
    std::printf("demand_test: all passed\n");
    return 0;
}
