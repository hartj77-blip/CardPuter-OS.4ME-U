// Finding the Wi-Fi adapters (hal/wifi_adapters.h) in a made-up /sys: the
// built-in chip, USB Wi-Fi adapters, and what isn't one.
// Build and run: shell/tests/run.sh
#include <sys/stat.h>
#include <unistd.h>

#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <string>

#include "hal/wifi_adapters.h"

using namespace zs;

namespace {
int g_failed = 0;

void check(bool ok, const char* what) {
    if (ok) return;
    std::printf("FAIL %s\n", what);
    ++g_failed;
}

std::string g_root;

void mkdirs(const std::string& full) {
    for (size_t i = g_root.size() + 1; (i = full.find('/', i)) != std::string::npos; ++i)
        mkdir(full.substr(0, i).c_str(), 0755);
    mkdir(full.c_str(), 0755);
}

void put(const std::string& path, const std::string& text) {
    std::string full = g_root + path;
    mkdirs(full.substr(0, full.rfind('/')));
    std::ofstream(full) << text << "\n";
}

void link(const std::string& target, const std::string& path) {
    std::string full = g_root + path;
    mkdirs(full.substr(0, full.rfind('/')));
    if (symlink(target.c_str(), full.c_str()) != 0) std::printf("symlink %s failed\n", full.c_str());
}

// A network interface whose device is <dev> under /sys/devices, as the
// kernel lays it out: class/net/<i>/device -> the device, which has a driver
// and a subsystem.
void iface(const std::string& name, const std::string& dev, const std::string& driver, const std::string& subsystem,
           bool wireless, const std::string& mac) {
    std::string d = "/sys/devices/" + dev;
    mkdirs(g_root + d);
    link("/drivers/" + driver, d + "/driver");
    link("/bus/" + subsystem, d + "/subsystem");
    link(g_root + d, "/sys/class/net/" + name + "/device");
    put("/sys/class/net/" + name + "/address", mac);
    if (wireless) mkdirs(g_root + "/sys/class/net/" + name + "/wireless");
}
}  // namespace

int main() {
    char tmpl[] = "/tmp/zs-wifi-XXXXXX";
    g_root = mkdtemp(tmpl);

    // Only the built-in chip, Ethernet and loopback.
    iface("wlan0", "platform/soc/mmc1/mmc1:0001:1", "brcmfmac", "sdio", true, "d8:3a:dd:00:00:01");
    iface("eth0", "platform/usb/1-1/1-1.1/1-1.1:1.0", "sr9700", "usb", false, "00:11:22:33:44:55");
    mkdirs(g_root + "/sys/class/net/lo");
    // A hotspot interface on the built-in chip: wireless, but no device of its own.
    mkdirs(g_root + "/sys/class/net/ap0/wireless");

    auto v = wifi::find_adapters(g_root);
    check(v.size() == 1, "one adapter: the built-in one (not Ethernet, lo or ap0)");
    if (!v.empty()) {
        check(v[0].iface == "wlan0" && v[0].id == "builtin" && v[0].label == "Built-in", "built-in named");
        check(!v[0].usb && v[0].driver == "brcmfmac", "built-in is not USB");
        check(v[0].mac == "d8:3a:dd:00:00:01", "its address");
    }

    // A USB Wi-Fi adapter: its product string is on the USB device, the
    // interface's parent.
    iface("wlan1", "platform/usb/1-1/1-1.2/1-1.2:1.0", "rtl8xxxu", "usb", true, "00:e0:4c:00:00:02");
    put("/sys/devices/platform/usb/1-1/1-1.2/product", "802.11n NIC");
    v = wifi::find_adapters(g_root);
    check(v.size() == 2, "two adapters");
    if (v.size() == 2) {
        check(v[0].id == "builtin", "built-in first");
        check(v[1].iface == "wlan1" && v[1].usb && v[1].id == "usb" && v[1].label == "USB", "the USB one");
        check(v[1].product == "802.11n NIC", "its product name");
        check(v[1].driver == "rtl8xxxu", "its driver");
    }

    // A second USB adapter, named before the built-in one: still after it.
    iface("wlx00c0ca000003", "platform/usb/1-1/1-1.3/1-1.3:1.0", "mt7601u", "usb", true, "00:c0:ca:00:00:03");
    v = wifi::find_adapters(g_root);
    check(v.size() == 3, "three adapters");
    if (v.size() == 3) {
        check(v[0].id == "builtin", "built-in still first");
        check(v[1].id == "usb" && v[1].iface == "wlan1", "first USB by name");
        check(v[2].id == "usb2" && v[2].label == "USB 2" && v[2].product.empty(), "second USB, no product file");
    }

    // A saved choice, plugged in or not.
    check(wifi::pick(v, "") == nullptr, "automatic picks none");
    check(wifi::pick(v, "usb") && wifi::pick(v, "usb")->iface == "wlan1", "usb picks wlan1");
    check(wifi::pick(v, "usb3") == nullptr, "not plugged in picks none");
    check(wifi::by_iface(v, "wlan0") && wifi::by_iface(v, "wlan0")->id == "builtin", "by interface");
    check(wifi::by_iface(v, "eth0") == nullptr, "Ethernet is no adapter");

    // No /sys at all (the web demo): none.
    check(wifi::find_adapters(g_root + "/nowhere").empty(), "no /sys: none");

    std::string rm = "rm -rf '" + g_root + "'";
    if (std::system(rm.c_str()) != 0) {}
    if (g_failed) return 1;
    std::printf("wifi_adapters_test: all passed\n");
    return 0;
}
