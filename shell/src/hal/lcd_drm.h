// Which /dev/dri/cardN is the Cardputer's own LCD, for apps made for M5Stack's
// launcher that open it by number.
//
// The LCD (M5's panel-mipi-dbi driver) and HDMI (vc4) are both DRM cards, and
// the kernel numbers them in the order their drivers start. On M5's system
// the LCD is card1; apps made there open "/dev/dri/card1" by name. On ZeroOS
// the LCD's driver starts first (it is there before vc4 takes over from the
// firmware's boot framebuffer), so the LCD is card0 and card1 is HDMI. That
// order isn't promised, so it is looked up each time an app starts. When the
// LCD isn't card1, ZeroShell tells an app that names /dev/dri/card1 which two
// to swap (ZEROOS_DRM_SWAP) and preloads the library that does it:
// platform/m5_drm_shim.c. Only such an app: one that finds its card by asking
// (a compositor going through every card) must get each under its real name.
#pragma once

#include <dirent.h>
#include <sys/stat.h>

#include <cctype>
#include <cstdlib>
#include <fstream>
#include <iterator>
#include <sstream>
#include <string>

namespace zs {

// "card0" from "card0", or from "card0-SPI-1" with `connector` set to the
// rest ("SPI-1"); "" for anything else in /sys/class/drm (version, renderD128).
inline std::string drm_card_name(const std::string& entry, std::string* connector = nullptr) {
    if (entry.rfind("card", 0) != 0 || entry.size() < 5) return "";
    size_t i = 4;
    while (i < entry.size() && std::isdigit(static_cast<unsigned char>(entry[i]))) ++i;
    if (i == 4) return "";
    if (i == entry.size()) {
        if (connector) connector->clear();
        return entry;
    }
    if (entry[i] != '-' || !connector) return "";
    *connector = entry.substr(i + 1);
    return entry.substr(0, i);
}

// The LCD's DRM card ("card0"), or "" when it can't be told. `sys` is "/sys"
// (the tests give a made-up one); `fb` the LCD's framebuffer ("fb0").
inline std::string lcd_drm_card(const std::string& sys, const std::string& fb) {
    // A DRM driver's framebuffer and its card hang off the same device: here,
    // the panel on the SPI bus.
    std::string found;
    if (DIR* d = opendir((sys + "/class/graphics/" + fb + "/device/drm").c_str())) {
        while (dirent* e = readdir(d)) {
            std::string card = drm_card_name(e->d_name);
            if (!card.empty() && (found.empty() || card < found)) found = card;
        }
        closedir(d);
    }
    if (!found.empty()) return found;
    // No such link: the card whose connector is on SPI (HDMI's are HDMI-A-n).
    if (DIR* d = opendir((sys + "/class/drm").c_str())) {
        while (dirent* e = readdir(d)) {
            std::string conn;
            std::string card = drm_card_name(e->d_name, &conn);
            if (!card.empty() && conn.rfind("SPI-", 0) == 0 && (found.empty() || card < found)) found = card;
        }
        closedir(d);
    }
    return found;
}

// Does an M5 app's Exec line run something that names /dev/dri/card1? Looks
// inside the files the line names (its program, or the script handed to an
// interpreter): paths with a "/" in them, relative ones from `cwd`, where the
// app is started. A few megabytes at most, once per launch.
inline bool m5_opens_card1(const std::string& command, const std::string& cwd) {
    std::istringstream words(command);
    std::string w;
    for (int n = 0; n < 8 && words >> w; ++n) {
        while (!w.empty() && (w.front() == '"' || w.front() == '\'')) w.erase(0, 1);
        while (!w.empty() && (w.back() == '"' || w.back() == '\'' || w.back() == ';')) w.pop_back();
        if (w.find('/') == std::string::npos) continue;
        if (w[0] != '/') w = cwd + "/" + w;
        struct stat st {};
        if (stat(w.c_str(), &st) != 0 || !S_ISREG(st.st_mode) || st.st_size <= 0 || st.st_size > (32 << 20)) continue;
        std::ifstream f(w, std::ios::binary);
        std::string data((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
        if (data.find("/dev/dri/card1") != std::string::npos) return true;
    }
    return false;
}

// What an app made for M5's system is told to swap, "<path>:<path>", so that
// /dev/dri/card1 is the LCD for it as it is there. "" when it already is, or
// the LCD's card isn't known: then the app runs as it would have anyway.
// ZS_M5_DRM_SWAP (tests) overrides.
inline std::string m5_drm_swap(const std::string& lcd_card) {
    if (const char* e = std::getenv("ZS_M5_DRM_SWAP")) return e;
    if (lcd_card.empty() || lcd_card == "card1") return "";
    return "/dev/dri/card1:/dev/dri/" + lcd_card;
}

}  // namespace zs
