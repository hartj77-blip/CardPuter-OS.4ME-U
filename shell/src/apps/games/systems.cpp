#include "apps/games/systems.h"

#include <sys/utsname.h>

#include <algorithm>
#include <cstdlib>
#include <cstring>

#include "apps/common.h"
#include "apps/kit/run.h"

namespace zs::games {

const std::vector<CoreDef>& cores() {
    // The Debian ones come with the Libretro pack (image/apps/games.pkg); the rest
    // download from libretro's nightly builds in the Libretro app (C).
    static const std::vector<CoreDef> v = {
        {"nestopia", "Nestopia", "libretro-nestopia", "nestopia", 1900},
        {"fceumm", "FCEUmm", nullptr, "fceumm", 1500},
        {"gambatte", "Gambatte", "libretro-gambatte", "gambatte", 450},
        {"mgba", "mGBA", "libretro-mgba", "mgba", 1300},
        {"snes9x2010", "Snes9x 2010", nullptr, "snes9x2010", 1100},
        {"bsnes_mercury_performance", "bsnes-mercury", "libretro-bsnes-mercury-performance", nullptr, 0},
        {"genesis_plus_gx", "Genesis Plus GX", nullptr, "genesis_plus_gx", 2600},
        {"picodrive", "PicoDrive", nullptr, "picodrive", 1300},
        {"mednafen_pce_fast", "Beetle PCE Fast", nullptr, "mednafen_pce_fast", 900},
        {"stella2014", "Stella 2014", nullptr, "stella2014", 900},
        {"pcsx_rearmed", "PCSX ReARMed", nullptr, "pcsx_rearmed", 1400},
        {"mednafen_wswan", "Beetle WonderSwan", "libretro-beetle-wswan", "mednafen_wswan", 400},
        {"zstest", "Test pattern", nullptr, nullptr, 0},
    };
    return v;
}

const std::vector<System>& systems() {
    static const std::vector<System> v = {
        {"nes", "NES / Famicom", "nes", "nes|fds|unf", {"nestopia", "fceumm"}},
        {"gb", "Game Boy", "gb", "gb|dmg", {"gambatte", "mgba"}},
        {"gbc", "Game Boy Color", "gbc", "gbc|cgb", {"gambatte", "mgba"}},
        {"gba", "Game Boy Advance", "gba", "gba", {"mgba"}},
        {"snes", "Super Nintendo", "snes", "sfc|smc", {"snes9x2010", "bsnes_mercury_performance"}},
        {"md", "Mega Drive / Genesis", "md", "md|gen|smd|bin", {"genesis_plus_gx", "picodrive"}},
        {"sms", "Master System / Game Gear", "sms", "sms|gg|sg", {"genesis_plus_gx", "picodrive"}},
        {"pce", "PC Engine", "pce", "pce", {"mednafen_pce_fast"}},
        {"a26", "Atari 2600", "a26", "a26", {"stella2014"}},
        {"psx", "PlayStation", "psx", "cue|chd|pbp|m3u", {"pcsx_rearmed"}},
        {"ws", "WonderSwan", "ws", "ws|wsc", {"mednafen_wswan"}},
        {"test", "Test pattern", "test", "zstest", {"zstest"}, true},
    };
    return v;
}

const CoreDef* core_def(const std::string& file) {
    for (auto& c : cores())
        if (file == c.file) return &c;
    return nullptr;
}

std::string games_dir() {
    if (const char* e = std::getenv("ZS_GAMES_DIR"); e && e[0]) return e;
    return home_dir() + "/Games";
}
std::string saves_dir() { return games_dir() + "/saves"; }
std::string states_dir() { return games_dir() + "/states"; }
std::string system_dir() { return games_dir() + "/system"; }
std::string user_cores_dir() { return games_dir() + "/cores"; }

std::string find_core(const std::string& file) {
    std::string name = file + "_libretro.so";
    std::vector<std::string> dirs;
    if (const char* e = std::getenv("ZS_GAMES_CORES"); e && e[0]) dirs.push_back(e);   // tests
    dirs.push_back(user_cores_dir());
    dirs.push_back("/usr/lib/aarch64-linux-gnu/libretro");
    dirs.push_back("/usr/lib/arm-linux-gnueabihf/libretro");
    dirs.push_back("/usr/lib/x86_64-linux-gnu/libretro");
    dirs.push_back("/usr/lib/libretro");
    for (auto& d : dirs)
        if (kit::exists(d + "/" + name)) return d + "/" + name;
    return "";
}

std::string core_for(const System& s, std::string* file_out) {
    for (const char* f : s.cores) {
        std::string p = find_core(f);
        if (!p.empty()) {
            if (file_out) *file_out = f;
            return p;
        }
    }
    if (file_out) *file_out = s.cores.empty() ? "" : s.cores.front();
    return "";
}

namespace {
std::string lower(std::string s) {
    for (auto& ch : s) ch = char(std::tolower(static_cast<unsigned char>(ch)));
    return s;
}

bool ext_matches(const std::string& name, const char* exts) {
    auto dot = name.find_last_of('.');
    if (dot == std::string::npos) return false;
    std::string ext = lower(name.substr(dot + 1));
    std::string list = std::string("|") + exts + "|";
    return list.find("|" + ext + "|") != std::string::npos;
}
}  // namespace

std::vector<std::string> games_for(const System& s) {
    std::vector<std::string> v;
    std::string dir = games_dir() + "/" + s.folder;
    for (auto& f : kit::list_files(dir))
        if (ext_matches(f, s.exts)) v.push_back(dir + "/" + f);
    // PlayStation: a .cue lists its .bin; with an .m3u (several discs) only show that.
    if (std::string(s.id) == "psx") {
        bool m3u = std::any_of(v.begin(), v.end(), [](const std::string& p) { return lower(p).size() > 4 && lower(p).compare(lower(p).size() - 4, 4, ".m3u") == 0; });
        if (m3u) v.erase(std::remove_if(v.begin(), v.end(), [](const std::string& p) {
                             std::string l = lower(p);
                             return l.compare(l.size() - 4, 4, ".m3u") != 0;
                         }), v.end());
    }
    return v;
}

std::string game_title(const std::string& path) {
    std::string n = path.substr(path.find_last_of('/') + 1);
    auto dot = n.find_last_of('.');
    return dot == std::string::npos || dot == 0 ? n : n.substr(0, dot);
}

std::string core_url(const CoreDef& c) {
    std::string arch = "aarch64";
    struct utsname u;
    if (uname(&u) == 0) {
        std::string m = u.machine;
        if (m == "x86_64") arch = "x86_64";
        else if (m.rfind("armv7", 0) == 0) arch = "armhf";
    }
    return "https://buildbot.libretro.com/nightly/linux/" + arch + "/latest/" + std::string(c.download) +
           "_libretro.so.zip";
}

}  // namespace zs::games
