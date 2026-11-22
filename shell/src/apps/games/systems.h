// Which consoles the Games app knows, where their games go, and which libretro
// cores play them (docs/GAMES.md).
#pragma once

#include <string>
#include <vector>

namespace zs::games {

// A libretro core: <file>_libretro.so.
struct CoreDef {
    const char* file;       // "gambatte"
    const char* name;       // "Gambatte"
    const char* package;    // Debian package with it, or nullptr
    const char* download;   // buildbot name to download it by, or nullptr
    int size_kb;            // download size, roughly
};

struct System {
    const char* id;         // "gb"
    const char* name;       // "Game Boy"
    const char* folder;     // ~/Games/<folder>
    const char* exts;       // "gb|dmg"
    std::vector<const char*> cores;   // CoreDef::file, best first
    bool hidden = false;    // only listed when it has games (the test core)
};

const std::vector<System>& systems();
const std::vector<CoreDef>& cores();
const CoreDef* core_def(const std::string& file);

// ~/Games, and its saves/, states/, system/ (BIOS files) and cores/ folders.
std::string games_dir();
std::string saves_dir();
std::string states_dir();
std::string system_dir();
std::string user_cores_dir();

// Full path of an installed core (user's ~/Games/cores first), or "".
std::string find_core(const std::string& file);
// The best installed core for a system, or "" (then file_out is the first choice).
std::string core_for(const System& s, std::string* file_out = nullptr);

// The games for a system: full paths, sorted by name.
std::vector<std::string> games_for(const System& s);

// "Super Game (USA).sfc" -> "Super Game (USA)"
std::string game_title(const std::string& path);

// Where to download a core from: libretro's nightly builds for this CPU.
std::string core_url(const CoreDef& c);

}  // namespace zs::games
