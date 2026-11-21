// Which /dev/fbN is the Cardputer's own LCD.
//
// The LCD (ST7789V on SPI, M5's panel-mipi-dbi driver) and HDMI (vc4) are both
// DRM devices, and the framebuffer numbers depend on what is plugged in. With
// no monitor, vc4 makes no framebuffer and the LCD is fb0 (seen in the stock
// image's boot log); with a monitor connected at boot, vc4 is fb0 and the LCD
// is fb1. M5's udev rule adds /dev/fb_lcd for it; we use that, then look in
// sysfs, and only then fall back to a guess. ZS_FRAMEBUFFER overrides everything.
#pragma once

#include <dirent.h>
#include <unistd.h>

#include <climits>
#include <cstdlib>
#include <fstream>
#include <string>

namespace zs {

inline std::string lcd_fb_read(const std::string& path) {
    std::ifstream f(path, std::ios::binary);
    std::string s((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
    return s;
}

// The LCD's framebuffer if it is there, e.g. "/dev/fb_lcd" or "/dev/fb0";
// empty while the panel driver hasn't loaded yet. `fallback` gets the first
// framebuffer that isn't HDMI's, for lcd_framebuffer() below.
inline std::string lcd_framebuffer_find(std::string* fallback = nullptr) {
    if (access("/dev/fb_lcd", F_OK) == 0) return "/dev/fb_lcd";
    std::string found;
    if (DIR* d = opendir("/sys/class/graphics")) {
        while (dirent* e = readdir(d)) {
            std::string n = e->d_name;
            if (n.rfind("fb", 0) != 0 || n.size() < 3) continue;
            std::string base = "/sys/class/graphics/" + n + "/";
            // of_node/compatible is NUL-separated; a plain find works on it.
            std::string compat = lcd_fb_read(base + "device/of_node/compatible");
            std::string name = lcd_fb_read(base + "name");
            if (compat.find("st7789") != std::string::npos ||
                name.find("mipi-dbi") != std::string::npos) {
                found = "/dev/" + n;
                break;
            }
            if (fallback && fallback->empty() && name.find("vc4") == std::string::npos)
                *fallback = "/dev/" + n;
        }
        closedir(d);
    }
    return found;
}

// Device path to draw to, e.g. "/dev/fb_lcd".
inline std::string lcd_framebuffer() {
    if (const char* e = std::getenv("ZS_FRAMEBUFFER"); e && e[0]) return e;
    std::string fallback;
    std::string found = lcd_framebuffer_find(&fallback);
    if (!found.empty()) return found;
    return fallback.empty() ? "/dev/fb0" : fallback;
}

// sysfs name of that framebuffer ("fb0"), following the /dev/fb_lcd symlink.
inline std::string lcd_fb_sysfs_name() {
    std::string dev = lcd_framebuffer();
    char buf[PATH_MAX];
    if (realpath(dev.c_str(), buf)) dev = buf;
    return dev.substr(dev.rfind('/') + 1);
}

}  // namespace zs
