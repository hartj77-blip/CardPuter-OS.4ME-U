// The Apps store's data: the real one (zeroos-apps on the device) and a
// simulated one for the simulator and screenshots. See ui/store.h.
#include "ui/store.h"

#include <dirent.h>
#include <signal.h>
#include <sys/stat.h>
#include <sys/wait.h>

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <ctime>
#include <fstream>
#include <map>
#include <set>
#include <sstream>

#include "lvgl.h"
#include "platform/shell_cmd.h"
#include "platform/watchdog.h"
#include "ui/app.h"

namespace zs::store {

namespace {

bool g_mock = false;

const char* const kCatalog = "/var/lib/zeroos/apps/catalog";
const char* const kStatus = "/run/zeroos/apps.status";
const char* const kSources = "/etc/zeroos/apps.sources";
// Apps a ZeroOS update took off (built for an older app ABI); zeroos-apps
// upgrade puts them back. One id per line.
const char* const kRestore = "/var/lib/zeroos/apps/restore";
const char* const kM5Notes = "/var/lib/zeroos/apps/m5";   // m5-<x>: package=<its Debian package>
const char* const kM5Store = "https://cardputer.cc/generated/registry.json";
const char* const kM5StoreOld = "https://cardputerzero.github.io/generated/registry.json";   // before 2026-11

std::string run(const std::string& cmd) { return run_command(cmd); }
std::string quote(const std::string& s) { return shell_quote(s); }

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
std::vector<std::string> g_mock_sources = {"hartj77-blip/CardPuter-Apps.4ME-U", "hartj77-blip/CardPuter-OS.4ME-U",
                                          "m5stack"};
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
    {"media", "Media", "Music player with a classic look, music and video from every Jellyfin library", "mpv", 260, nullptr},
    {"recorder", "Recorder", "Voice notes: record, play back, rename, delete", "sox", 60, nullptr},
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
    {"games", "Libretro", "Play NES, Game Boy and GBA games (more consoles with downloadable cores)",
     "libretro-nestopia, libretro-gambatte, libretro-mgba, curl, unzip", 140, nullptr},
    // A few from M5Stack's app store, as its registry.json lists them.
    {"m5-acidzero", "AcidZero", "Acid bass synth with a 16-step sequencer and MIDI CC control",
     "libsdl2-2.0-0, libasound2", 58, "m5stack"},
    {"m5-zero-weather", "Zero Weather", "Weather for where you are, with a 7-day forecast", "python3, python3-pil",
     24, "m5stack"},
    {"m5-ghost-hunter", "Ghost Hunter", "A ghost-hunting game for the Cardputer Zero",
     "python3, python3-pygame, python3-numpy", 31, "m5stack"},
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
    if (g_job.action == "refresh" || g_job.action == "add-source" || g_job.action == "remove-source" ||
        g_job.action == "move-source") {
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
        e.m5 = e.id.rfind("m5-", 0) == 0;
        if (e.m5) e.version = "1.0.0";
        auto it = g_mock_installed.find(e.id);
        if (it != g_mock_installed.end()) e.installed = it->second;
        out.push_back(e);
    }
    return out;
}

// ---- Real store ---------------------------------------------------------

std::map<std::string, std::string> installed_packages() {
    std::map<std::string, std::string> out;
    // One line each, status first: a package dpkg only knows the name of
    // (the zeroos-app-abi-N that zeroos provides) has no version, so reading
    // word by word would shift every field after it.
    std::istringstream in(run("dpkg-query -W -f='${db:Status-Abbrev} ${Package} ${Version}\\n' 'zeroos-app-*'"));
    std::string line;
    while (std::getline(in, line)) {
        std::istringstream l(line);
        std::string st, pkg, ver;
        if (!(l >> st >> pkg >> ver)) continue;
        // "ii" = installed; anything else (rc: removed, config left) isn't.
        if (st.size() >= 2 && st[1] == 'i' && pkg.rfind("zeroos-app-", 0) == 0) out[pkg.substr(11)] = ver;
    }
    // Apps from M5Stack's store: zeroos-apps noted each one's package.
    std::map<std::string, std::string> m5;   // package -> id
    if (DIR* d = opendir(kM5Notes)) {
        while (dirent* x = readdir(d)) {
            std::string id = x->d_name;
            if (id.rfind("m5-", 0) != 0) continue;
            std::ifstream f(std::string(kM5Notes) + "/" + id);
            std::string l;
            while (std::getline(f, l)) {
                if (l.rfind("package=", 0) != 0) continue;
                std::string pkg = l.substr(8);
                bool ok = !pkg.empty();
                for (char ch : pkg)
                    ok = ok && ((ch >= 'a' && ch <= 'z') || (ch >= '0' && ch <= '9') || ch == '+' || ch == '.' || ch == '-');
                if (ok) m5[pkg] = id;
                break;
            }
        }
        closedir(d);
    }
    if (!m5.empty()) {
        std::string cmd = "dpkg-query -W -f='${db:Status-Abbrev} ${Package} ${Version}\\n'";
        for (auto& [pkg, id] : m5) cmd += " " + quote(pkg);
        std::istringstream q(run(cmd));
        while (std::getline(q, line)) {
            std::istringstream l(line);
            std::string st, pkg, ver;
            if (!(l >> st >> pkg >> ver)) continue;
            auto it = m5.find(pkg);
            if (st.size() >= 2 && st[1] == 'i' && it != m5.end()) out[it->second] = ver;
        }
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
        else if (k == "warn") e.warn = v;
        else if (k == "kind") e.m5 = v == "m5";
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
    // Taken off by a ZeroOS update and not back yet (it couldn't reach the
    // sources then): Update all puts them back, so Apps counts them in it.
    {
        std::ifstream r(kRestore);
        std::set<std::string> ids;
        for (std::string id; r >> id;) ids.insert(id);
        for (auto& x : out)
            if (x.installed.empty() && ids.count(x.id)) x.restore = true;
    }
    // Installed, but no source offers it now: still list it, so it can be removed.
    for (auto& [id, ver] : inst) {
        Entry x;
        x.id = id;
        const AppInfo* a = app_find(id);
        x.name = a ? a->name : id;
        x.installed = ver;
        x.m5 = id.rfind("m5-", 0) == 0;
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
// Runs of digits compare as numbers, anything else as text ("0.2.0-1",
// "1.0.3~beta" from M5's store too); an epoch ("1:") is ignored.
bool newer(const std::string& a, const std::string& b) {
    auto strip = [](const std::string& v) {
        size_t c = v.find(':');
        return c != std::string::npos && c < 4 ? v.substr(c + 1) : v;
    };
    std::string x = strip(a), y = strip(b);
    size_t i = 0, j = 0;
    while (i < x.size() || j < y.size()) {
        bool dx = i < x.size() && std::isdigit(static_cast<unsigned char>(x[i]));
        bool dy = j < y.size() && std::isdigit(static_cast<unsigned char>(y[j]));
        if (dx && dy) {
            long p = 0, q = 0;
            while (i < x.size() && std::isdigit(static_cast<unsigned char>(x[i]))) p = p * 10 + (x[i++] - '0');
            while (j < y.size() && std::isdigit(static_cast<unsigned char>(y[j]))) q = q * 10 + (y[j++] - '0');
            if (p != q) return p > q;
            continue;
        }
        char cx = i < x.size() ? x[i] : 0, cy = j < y.size() ? y[j] : 0;
        if (cx == '~' || cy == '~') return cy == '~' && cx != '~';   // ~ sorts before anything
        if (cx != cy) return cx > cy;
        ++i;
        ++j;
    }
    return false;
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

AddResult add_source(const std::string& source, const std::string& password) {
    if (g_mock) {
        if (password.empty()) return AddResult::Refused;
        g_mock_sources.push_back(source);
        mock_start("add-source", "");
        return AddResult::Started;
    }
    // polkit lets the UI user run the other jobs without asking, but not this
    // one (files/90-zeroos.rules): sudo it with the user's own password.
    // -S reads the password from stdin, -k never uses a remembered one, and
    // -p '' keeps the prompt out of the output.
    WatchdogGrace grace;   // a wrong password takes sudo a couple of seconds
    std::string unit = "zeroos-apps@" + unit_escape("add-source:" + source) + ".service";
    std::string cmd = "sudo -S -k -p '' -- systemctl start --no-block " + quote(unit) + " >/dev/null 2>&1";
    struct sigaction ign {}, old {};
    ign.sa_handler = SIG_IGN;
    sigaction(SIGPIPE, &ign, &old);   // sudo may be gone before it reads
    int st = -1;
    if (FILE* p = popen(cmd.c_str(), "w")) {
        fputs(password.c_str(), p);
        fputc('\n', p);
        st = pclose(p);
    }
    sigaction(SIGPIPE, &old, nullptr);
    return st != -1 && WIFEXITED(st) && WEXITSTATUS(st) == 0 ? AddResult::Started : AddResult::Refused;
}

void remove_source(const std::string& source) {
    if (g_mock) {
        g_mock_sources.erase(std::remove(g_mock_sources.begin(), g_mock_sources.end(), source), g_mock_sources.end());
        mock_start("remove-source", "");
    } else {
        start_job("remove-source:" + source);
    }
}

void move_source(const std::string& source, int dir) {
    if (g_mock) {
        auto& v = g_mock_sources;
        auto it = std::find(v.begin(), v.end(), source);
        if (it == v.end()) return;
        long i = it - v.begin(), j = i + (dir < 0 ? -1 : 1);
        if (j >= 0 && j < long(v.size())) std::swap(v[size_t(i)], v[size_t(j)]);
        mock_start("move-source", "");
    } else {
        start_job((dir < 0 ? "source-up:" : "source-down:") + source);
    }
}

std::string source_label(const std::string& source) {
    if (source == "m5stack" || source == kM5Store || source == kM5StoreOld) return "M5Stack's app store";
    return source;
}

std::string normalise_source(const std::string& text) {
    std::string s = text;
    s.erase(0, s.find_first_not_of(" \t"));
    s.erase(s.find_last_not_of(" \t/") + 1);
    {
        std::string low;
        for (char ch : s) low += char(std::tolower(static_cast<unsigned char>(ch)));
        if (low == "m5stack" || low == "m5" || low == "m5stack store") return "m5stack";
    }
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
