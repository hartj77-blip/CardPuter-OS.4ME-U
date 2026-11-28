// The Wi-Fi adapters in this Cardputer: the built-in chip and any USB Wi-Fi
// adapter (dongle) plugged into the USB-A or a USB-C port in host mode.
// Settings > Wi-Fi offers a choice between them once there is more than one
// (docs/HARDWARE.md, "USB Wi-Fi adapters").
//
// Read from /sys only, no program started. Every path takes a root folder, so
// it is unit tested against a made-up /sys (shell/tests/wifi_adapters_test.cpp).
#pragma once

#include <dirent.h>
#include <sys/stat.h>
#include <unistd.h>

#include <algorithm>
#include <fstream>
#include <string>
#include <vector>

#include "hal/hal.h"

namespace zs::wifi {

namespace detail {
inline std::string first_line(const std::string& path) {
    std::ifstream f(path);
    std::string s;
    if (f) std::getline(f, s);
    while (!s.empty() && (s.back() == ' ' || s.back() == '\r' || s.back() == '\t')) s.pop_back();
    return s;
}

inline bool is_dir(const std::string& path) {
    struct stat st{};
    return stat(path.c_str(), &st) == 0 && S_ISDIR(st.st_mode);
}

// The last part of where a symlink points ("brcmfmac", "usb"); "" if none.
inline std::string link_name(const std::string& path) {
    char buf[512];
    ssize_t n = readlink(path.c_str(), buf, sizeof buf - 1);
    if (n <= 0) return "";
    std::string s(buf, size_t(n));
    return s.substr(s.rfind('/') + 1);
}
}  // namespace detail

// Every Wi-Fi interface under <root>/sys/class/net, sorted by name: the
// built-in one first, then USB adapters as "usb", "usb2", "usb3"...
inline std::vector<WifiAdapter> find_adapters(const std::string& root = "") {
    using namespace detail;
    std::vector<WifiAdapter> v;
    std::string net = root + "/sys/class/net/";
    std::vector<std::string> names;
    if (DIR* d = opendir(net.c_str())) {
        while (dirent* e = readdir(d))
            if (e->d_name[0] != '.') names.push_back(e->d_name);
        closedir(d);
    }
    std::sort(names.begin(), names.end());
    for (auto& i : names) {
        std::string base = net + i;
        // Only Wi-Fi interfaces have "wireless" (or "phy80211" for cfg80211 ones).
        if (!is_dir(base + "/wireless") && !is_dir(base + "/phy80211")) continue;
        // Virtual interfaces (a hotspot's ap0, p2p-dev-*) share a chip: skip
        // anything that isn't backed by a device of its own.
        std::string driver = link_name(base + "/device/driver");
        if (driver.empty()) continue;
        WifiAdapter a;
        a.iface = i;
        a.driver = driver;
        a.usb = link_name(base + "/device/subsystem") == "usb";
        a.mac = first_line(base + "/address");
        // The USB device is the interface's parent: its "product" string is
        // what the maker called it ("802.11n NIC", "AWUS036ACM").
        if (a.usb) a.product = first_line(base + "/device/../product");
        v.push_back(a);
    }
    // Built-in first, then USB in name order.
    std::stable_sort(v.begin(), v.end(), [](const WifiAdapter& a, const WifiAdapter& b) { return !a.usb && b.usb; });
    int usb_n = 0, builtin_n = 0;
    for (auto& a : v) {
        if (a.usb) {
            ++usb_n;
            a.id = usb_n == 1 ? "usb" : "usb" + std::to_string(usb_n);
            a.label = usb_n == 1 ? "USB" : "USB " + std::to_string(usb_n);
        } else {
            ++builtin_n;
            a.id = builtin_n == 1 ? "builtin" : "builtin" + std::to_string(builtin_n);
            a.label = builtin_n == 1 ? "Built-in" : "Built-in " + std::to_string(builtin_n);
        }
    }
    return v;
}

// The adapter a saved choice ("usb", "builtin", "" for automatic) names now;
// nullptr if it's automatic or that adapter isn't plugged in.
inline const WifiAdapter* pick(const std::vector<WifiAdapter>& v, const std::string& id) {
    if (id.empty()) return nullptr;
    auto it = std::find_if(v.begin(), v.end(), [&](const WifiAdapter& a) { return a.id == id; });
    return it == v.end() ? nullptr : &*it;
}

// The adapter by interface name ("wlan1"); nullptr if it isn't one.
inline const WifiAdapter* by_iface(const std::vector<WifiAdapter>& v, const std::string& iface) {
    auto it = std::find_if(v.begin(), v.end(), [&](const WifiAdapter& a) { return a.iface == iface; });
    return it == v.end() ? nullptr : &*it;
}

}  // namespace zs::wifi
