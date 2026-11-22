// The Apps store's data: the real one (zeroos-apps on the device) and a
// simulated one for the simulator and screenshots. See ui/store.h.
#include "ui/store.h"

#include <sys/stat.h>

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <ctime>
#include <fstream>
#include <map>
#include <sstream>

#include "lvgl.h"
#include "ui/app.h"

namespace zs::store {

namespace {

bool g_mock = false;

const char* const kCatalog = "/var/lib/zeroos/apps/catalog";
const char* const kStatus = "/run/zeroos/apps.status";
const char* const kSources = "/etc/zeroos/apps.sources";

std::string run(const std::string& cmd) {
    std::string out;
    FILE* p = popen((cmd + " 2>/dev/null").c_str(), "r");
    if (!p) return out;
    char buf[512];
    while (fgets(buf, sizeof buf, p)) out += buf;
    pclose(p);
    return out;
}

std::string quote(const std::string& s) {
    std::string r = "'";
    for (char c : s) r += c == '\'' ? std::string("'\\''") : std::string(1, c);
    return r + "'";
}

// systemd-escape: the job's words become the instance of zeroos-apps@.service.
std::string unit_escape(const std::string& s) {
    static const char* hex = "0123456789abcdef";
    std::string r;
    for (size_t i = 0; i < s.size(); ++i) {
        unsigned char ch = static_cast<unsigned char>(s[i]);
        if (ch == '/') r += '-';
        else if ((std::isalnum(ch) || ch == ':' || ch == '_' || (ch == '.' && i > 0)))
            r += char(ch);
        else {
            r += "\\x";
            r += hex[ch >> 4];
            r += hex[ch & 15];
        }
    }
    return r;
}

void start_job(const std::string& words) {
    run("systemctl start --no-block " + quote("zeroos-apps@" + unit_escape(words) + ".service"));
}

Status::State parse_state(const std::string& s) {
    static const std::map<std::string, Status::State> m = {
        {"refreshing", Status::Refreshing}, {"downloading", Status::Downloading},
        {"installing", Status::Installing}, {"removing", Status::Removing},
        {"done", Status::Done},             {"failed", Status::Failed},
        {"offline", Status::Offline}};
    auto it = m.find(s);
    return it == m.end() ? Status::Idle : it->second;
}

// ---- Simulated store ----------------------------------------------------

struct MockJob {
    std::string action, app;
    uint32_t start = 0;
    bool active = false;
};
MockJob g_job;
Status g_mock_status;
std::map<std::string, std::string> g_mock_installed;   // id -> version
std::vector<std::string> g_mock_sources = {"hartj77-blip/CardPuter-OS.4ME-U"};
bool g_mock_init = false;
uint32_t g_mock_refreshed = 0;

struct MockApp {
    const char* id;
    const char* name;
    const char* summary;
    const char* needs;
    long kb;
    const char* source;
};
// What the real catalog says about each app (image/apps/<id>.pkg).
const MockApp kMockApps[] = {
    {"remote", "Remote", "Tailscale sign-in, SSH to your devices, remote desktop, Taildrop", "tailscale, mosh, freerdp3-wayland", 210, nullptr},
    {"sync", "Sync", "Keep Notes, Documents and Recordings the same on your PC and phone", "syncthing", 160, nullptr},
    {"notes", "Notes", "Quick text notes, saved as you leave", "", 40, nullptr},
    {"media", "Media", "Music player, music from Jellyfin, voice notes", "mpv, sox", 260, nullptr},
    {"camera", "Camera", "Viewfinder, photos and a QR code reader", "camera (M5Stack), rpicam-apps-lite", 330, nullptr},
    {"car", "Car", "USB stick mode for a car stereo, Bluetooth audio", "mtools, dosfstools, fdisk", 120, nullptr},
    {"tether", "Tether", "Get online through your phone over Wi-Fi, USB or Bluetooth", "usbmuxd", 110, nullptr},
    {"ir", "IR Remote", "Send and learn infrared remote codes", "v4l-utils, ir-keytable", 70, nullptr},
    {"gps", "GPS", "Position, saved spots and GPX tracks with a GPS module", "gpsd", 90, nullptr},
    {"mesh", "Mesh", "Meshtastic text messages over LoRa (Cap LoRa-1262)", "meshtasticd", 110, nullptr},
    {"compass", "Compass", "Compass and spirit level", "", 40, nullptr},
    {"battery", "Battery", "Time left, power draw and power profile", "", 40, nullptr},
    {"clock", "Clock", "Clock and stopwatch", "", 30, nullptr},
    {"sysmon", "System", "CPU, memory and what uses it", "", 50, nullptr},
    {"dev", "Dev Tools", "Projects with one-key run, micro editor, REPLs, Git and a serial monitor",
     "git, micro, make, gcc, libc6-dev, python3, lua5.4, bc, sqlite3", 180, nullptr},
    {"games", "Games", "Play NES, Game Boy and GBA games (more consoles with downloadable cores)",
     "libretro-nestopia, libretro-gambatte, libretro-mgba, alsa-utils, curl, unzip", 140, nullptr},
};

void mock_init() {
    if (g_mock_init) return;
    g_mock_init = true;
    // Everything the simulator has built in counts as installed.
    for (auto& a : app_registry())
        if (!a.base) g_mock_installed[a.id] = "0.4.0";
    if (app_find("code")) g_mock_installed["dev"] = "0.4.0";   // a pack: its apps have their own ids
    if (g_mock_installed.count("clock")) g_mock_installed["clock"] = "0.3.2";   // so one app shows an update
    g_mock_refreshed = lv_tick_get();
}

void mock_start(const std::string& action, const std::string& app) {
    mock_init();
    g_job = {action, app, lv_tick_get(), true};
}

void mock_tick() {
    if (!g_job.active) return;
    uint32_t ms = lv_tick_elaps(g_job.start);
    Status& s = g_mock_status;
    s.action = g_job.action;
    s.app = g_job.app;
    s.message.clear();
    if (g_job.action == "refresh" || g_job.action == "add-source" || g_job.action == "remove-source") {
        s.state = Status::Refreshing;
        if (ms > 1500) {
            s.state = Status::Done;
            g_job.active = false;
            g_mock_refreshed = lv_tick_get();
        }
        return;
    }
    if (g_job.action == "remove") {
        s.state = Status::Removing;
        if (ms > 1500) {
            g_mock_installed.erase(g_job.app);
            s.state = Status::Done;
            g_job.active = false;
        }
        return;
    }
    // install / upgrade: download for 3 s, install for 2 s.
    if (ms < 3000) {
        s.state = Status::Downloading;
        s.percent = int(ms / 30);
    } else if (ms < 5000) {
        s.state = Status::Installing;
        s.percent = 100;
    } else {
        if (g_job.action == "upgrade") {
            for (auto& [id, v] : g_mock_installed) v = "0.4.0";
        } else {
            g_mock_installed[g_job.app] = "0.4.0";
        }
        s.state = Status::Done;
        g_job.active = false;
    }
}

std::vector<Entry> mock_catalog() {
    mock_init();
    std::vector<Entry> out;
    for (auto& m : kMockApps) {
        Entry e;
        e.id = m.id;
        const AppInfo* a = app_find(m.id);
        e.name = a ? a->name : m.name;
        e.summary = m.summary;
        e.needs = m.needs;
        e.version = "0.4.0";
        e.size_kb = m.kb;
        e.source = m.source ? m.source : "hartj77-blip/CardPuter-OS.4ME-U";
        auto it = g_mock_installed.find(e.id);
        if (it != g_mock_installed.end()) e.installed = it->second;
        out.push_back(e);
    }
    return out;
}

// ---- Real store ---------------------------------------------------------

std::map<std::string, std::string> installed_packages() {
    std::map<std::string, std::string> out;
    std::istringstream in(run("dpkg-query -W -f='${Package} ${Version} ${db:Status-Abbrev}\\n' 'zeroos-app-*'"));
    std::string pkg, ver, st;
    while (in >> pkg >> ver >> st) {
        // "ii" = installed; anything else (rc: removed, config left) isn't.
        if (st.size() >= 2 && st[1] == 'i' && pkg.rfind("zeroos-app-", 0) == 0) out[pkg.substr(11)] = ver;
    }
    return out;
}

// The catalog: blank-line separated blocks of key=value lines.
std::vector<Entry> real_catalog() {
    std::vector<Entry> out;
    std::ifstream f(kCatalog);
    Entry e;
    auto flush = [&] {
        if (!e.id.empty()) out.push_back(e);
        e = Entry();
    };
    std::string line;
    while (std::getline(f, line)) {
        if (line.empty()) {
            flush();
            continue;
        }
        size_t eq = line.find('=');
        if (eq == std::string::npos) continue;
        std::string k = line.substr(0, eq), v = line.substr(eq + 1);
        if (k == "app") e.id = v;
        else if (k == "name") e.name = v;
        else if (k == "summary") e.summary = v;
        else if (k == "version") e.version = v;
        else if (k == "source") e.source = v;
        else if (k == "needs") e.needs = v;
        else if (k == "size") e.size_kb = (std::atol(v.c_str()) + 1023) / 1024;
    }
    flush();
    auto inst = installed_packages();
    for (auto& x : out) {
        auto it = inst.find(x.id);
        if (it != inst.end()) {
            x.installed = it->second;
            inst.erase(it);
        }
    }
    // Installed, but no source offers it now: still list it, so it can be removed.
    for (auto& [id, ver] : inst) {
        Entry x;
        x.id = id;
        const AppInfo* a = app_find(id);
        x.name = a ? a->name : id;
        x.installed = ver;
        x.summary = "No longer offered by your sources";
        out.push_back(x);
    }
    return out;
}

Status real_status() {
    Status s;
    std::ifstream f(kStatus);
    std::string line;
    while (std::getline(f, line)) {
        size_t eq = line.find('=');
        if (eq == std::string::npos) continue;
        std::string k = line.substr(0, eq), v = line.substr(eq + 1);
        if (k == "state") s.state = parse_state(v);
        else if (k == "action") s.action = v;
        else if (k == "app") s.app = v;
        else if (k == "percent") s.percent = std::atoi(v.c_str());
        else if (k == "message") s.message = v;
    }
    return s;
}

// Simple version order, enough for the update badge: dpkg decides for real.
bool newer(const std::string& a, const std::string& b) {
    std::istringstream x(a), y(b);
    for (;;) {
        long p = -1, q = -1;
        char dot;
        x >> p;
        y >> q;
        if (p != q) return p > q;
        if (p < 0) return false;
        bool more_x = bool(x >> dot), more_y = bool(y >> dot);
        if (!more_x && !more_y) return false;
    }
}

}  // namespace

bool Entry::update() const { return !installed.empty() && !version.empty() && newer(version, installed); }

void set_mock(bool mock) { g_mock = mock; }

std::vector<Entry> catalog() {
    std::vector<Entry> v = g_mock ? mock_catalog() : real_catalog();
    std::sort(v.begin(), v.end(), [](const Entry& a, const Entry& b) { return a.name < b.name; });
    return v;
}

long catalog_age() {
    if (g_mock) {
        mock_init();
        return long(lv_tick_elaps(g_mock_refreshed) / 1000);
    }
    struct stat st{};
    if (stat(kCatalog, &st) != 0) return -1;
    return long(std::time(nullptr) - st.st_mtime);
}

Status status() {
    if (g_mock) {
        mock_tick();
        return g_mock_status;
    }
    return real_status();
}

std::vector<std::string> sources() {
    if (g_mock) return g_mock_sources;
    std::vector<std::string> out;
    std::ifstream f(kSources);
    std::string line;
    while (std::getline(f, line)) {
        size_t a = line.find_first_not_of(" \t");
        if (a == std::string::npos || line[a] == '#') continue;
        line = line.substr(a);
        line.erase(line.find_last_not_of(" \t\r") + 1);
        out.push_back(line);
    }
    return out;
}

void refresh() { g_mock ? mock_start("refresh", "") : start_job("refresh"); }
void install(const std::string& id) { g_mock ? mock_start("install", id) : start_job("install:" + id); }
void remove(const std::string& id) { g_mock ? mock_start("remove", id) : start_job("remove:" + id); }
void upgrade_all() { g_mock ? mock_start("upgrade", "") : start_job("upgrade"); }

void add_source(const std::string& source) {
    if (g_mock) {
        g_mock_sources.push_back(source);
        mock_start("add-source", "");
    } else {
        start_job("add-source:" + source);
    }
}

void remove_source(const std::string& source) {
    if (g_mock) {
        g_mock_sources.erase(std::remove(g_mock_sources.begin(), g_mock_sources.end(), source), g_mock_sources.end());
        mock_start("remove-source", "");
    } else {
        start_job("remove-source:" + source);
    }
}

std::string normalise_source(const std::string& text) {
    std::string s = text;
    s.erase(0, s.find_first_not_of(" \t"));
    s.erase(s.find_last_not_of(" \t/") + 1);
    if (s.rfind("https://", 0) == 0 && s.rfind("https://github.com/", 0) != 0) {
        // Any https address that serves zeroos-apps.index.
        for (char ch : s)
            if (ch <= ' ' || ch == '\'' || ch == '"' || ch == '\\') return "";
        return s;
    }
    for (const char* p : {"https://github.com/", "http://github.com/", "github.com/"})
        if (s.rfind(p, 0) == 0) s = s.substr(std::string(p).size());
    if (s.size() > 4 && s.compare(s.size() - 4, 4, ".git") == 0) s.resize(s.size() - 4);
    // owner/repo, GitHub's own rules (letters, digits, - _ .).
    size_t slash = s.find('/');
    if (slash == std::string::npos || slash == 0 || slash + 1 >= s.size() || s.find('/', slash + 1) != std::string::npos)
        return "";
    for (char ch : s)
        if (!(std::isalnum(static_cast<unsigned char>(ch)) || ch == '-' || ch == '_' || ch == '.' || ch == '/')) return "";
    if (s[0] == '.' || s[slash + 1] == '.') return "";
    return s;
}

}  // namespace zs::store
