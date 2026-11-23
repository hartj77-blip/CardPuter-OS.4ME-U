// Apps made for M5Stack's launcher (APPLaunch), installed from M5Stack's app
// store by zeroos-apps (docs/APPS.md, "M5Stack's app store"). Each launcher
// entry (.desktop file) is an app on the home screen. Opening one hands the
// screen to its program until it exits, as Camera does; holding Esc for 3 s
// stops it, as on M5's launcher. Then ZeroShell takes the screen back.
//
// Ids: zeroos-apps writes /var/lib/zeroos/apps/m5/m5-<x> for each app it
// installs (package=, desktop= lines), so the home screen and the store use
// the same id. An entry installed some other way (dpkg -i) gets m5-<file>.
#include <dirent.h>

#include <algorithm>
#include <cstdlib>
#include <fstream>
#include <map>
#include <set>
#include <sstream>

#include "apps/common.h"
#include "ui/desktop_entry.h"

namespace zs {

namespace {
using namespace theme;

std::string env_or(const char* name, const char* fallback) {
    const char* v = std::getenv(name);
    return v && *v ? v : fallback;
}

std::string read_file(const std::string& path) {
    std::ifstream f(path);
    std::stringstream ss;
    ss << f.rdbuf();
    return ss.str();
}

// Launcher entries ZeroOS shows its own way: M5's camera program is behind
// the Camera app (apps/camera), which also has the QR reader.
bool hidden(const std::string& file) { return file == "camera_app.desktop"; }

class M5App : public App {
public:
    M5App(DesktopEntry e) : e_(std::move(e)) {}
    ~M5App() override { lv_async_call_cancel(start_cb, this); }

    void build(lv_obj_t* root) override {
        root_ = root;
        lv_obj_t* t = label(root, e_.name.c_str(), &zs_font_16, kText);
        lv_obj_set_pos(t, 10, 8);
        msg_ = label(root, "Starting...", &zs_font_12, kMuted);
        lv_obj_set_width(msg_, kScreenW - 20);
        lv_label_set_long_mode(msg_, LV_LABEL_LONG_WRAP);
        lv_obj_set_pos(msg_, 10, 32);
        shell_set_actions({{"Open again", 'o', "Enter", [this] { start(); }}, {"Back", 0, "Esc", nullptr}});
        // After this page is up: the shell steps aside from here.
        lv_async_call(start_cb, this);
    }

    bool on_key(uint32_t key) override {
        if (key == LV_KEY_ENTER) {
            start();
            return true;
        }
        return false;
    }

private:
    static void start_cb(void* p) { static_cast<M5App*>(p)->start(); }

    void start() {
        bool ok;
        if (e_.terminal) {
            // In the Terminal, from M5's folder, like APPLaunch; Sysplause
            // keeps its last words on screen until a key.
            std::string cmd = "cd /usr/share/APPLaunch && " + e_.exec;
            if (e_.sysplause) cmd = "(" + cmd + "); printf '\\nPress Enter to go back'; read -r _";
            ok = shell_open_terminal(cmd);
        } else {
            ok = shell_open_external(e_.exec);
        }
        if (ok) {
            lv_label_set_text(msg_, e_.terminal ? "Ran in the Terminal. Enter opens it again."
                                                : "Hold Esc for 3 seconds to leave it.");
            return;
        }
        lv_label_set_text(msg_,
                          "Couldn't start it. It's an app made for M5Stack's launcher; "
                          "it needs the device (not the simulator).\n\n"
                          "If it was just installed, try again, or reinstall it in Apps.");
        lv_obj_set_style_text_color(msg_, c(kWarn), 0);
    }

    DesktopEntry e_;
    lv_obj_t* root_ = nullptr;
    lv_obj_t* msg_ = nullptr;
};

// Icon: the name's first letter on a colour picked from the id.
const char* letter_of(const std::string& name) {
    static std::map<char, std::string> letters;   // keeps the strings alive
    for (unsigned char ch : name) {
        if (std::isalnum(ch) && ch < 0x80) {
            char up = char(std::toupper(ch));
            auto& s = letters[up];
            if (s.empty()) s = std::string(1, up);
            return s.c_str();
        }
    }
    return ZS_ICON_GRID;
}

uint32_t color_of(const std::string& id) {
    static const uint32_t palette[] = {0x2f6f8f, 0x8f4a2f, 0x2f8f5a, 0x6a2f8f, 0x8f7a2f, 0x2f4a8f, 0x8f2f5a, 0x4a6b2f};
    uint32_t h = 2166136261u;
    for (unsigned char ch : id) h = (h ^ ch) * 16777619u;
    return palette[h % (sizeof palette / sizeof palette[0])];
}

void add(std::vector<AppInfo>& out, std::set<std::string>& taken, const std::string& id, const std::string& path) {
    if (taken.count(id)) return;
    auto e = parse_desktop_entry(read_file(path));
    if (!e) return;
    taken.insert(id);
    DesktopEntry entry = *e;
    AppInfo info{id, entry.name, letter_of(entry.name), color_of(id),
                 [entry] { return std::unique_ptr<App>(std::make_unique<M5App>(entry)); }};
    info.order = 2000;   // after ZeroOS's own apps; ties go by name
    info.builtin = false;
    out.push_back(std::move(info));
}

}  // namespace

// Called by the app registry (apps/registry.cpp) on each scan.
void m5_apps_add(std::vector<AppInfo>& out) {
    std::string dir = env_or("ZS_M5_APP_DIR", "/usr/share/APPLaunch/applications");
    std::string notes = env_or("ZS_M5_STATE_DIR", "/var/lib/zeroos/apps/m5");
    std::set<std::string> taken;
    for (auto& a : out) taken.insert(a.id);
    std::set<std::string> claimed;   // desktop files that belong to a noted app

    // Installed from the store: the id the store uses.
    std::vector<std::string> ids;
    if (DIR* d = opendir(notes.c_str())) {
        while (dirent* x = readdir(d)) {
            std::string n = x->d_name;
            if (n.rfind("m5-", 0) == 0) ids.push_back(n);
        }
        closedir(d);
    }
    std::sort(ids.begin(), ids.end());
    for (auto& id : ids) {
        std::istringstream in(read_file(notes + "/" + id));
        std::string line;
        int n = 0;
        while (std::getline(in, line)) {
            if (line.rfind("desktop=", 0) != 0) continue;
            std::string path = line.substr(8);
            std::string file = path.substr(path.rfind('/') + 1);
            // Only entries in M5's folder, as zeroos-apps writes them.
            if (path.rfind("/usr/share/APPLaunch/applications/", 0) != 0 || hidden(file)) continue;
            claimed.insert(file);
            ++n;
            add(out, taken, n == 1 ? id : id + "-" + std::to_string(n), dir + "/" + file);
        }
    }

    // Any other launcher entry (an M5 package installed by hand).
    std::vector<std::string> files;
    if (DIR* d = opendir(dir.c_str())) {
        while (dirent* x = readdir(d)) {
            std::string n = x->d_name;
            if (n.size() > 8 && n.compare(n.size() - 8, 8, ".desktop") == 0 && n[0] != '.') files.push_back(n);
        }
        closedir(d);
    }
    std::sort(files.begin(), files.end());
    for (auto& f : files) {
        if (claimed.count(f) || hidden(f)) continue;
        std::string id = desktop_app_id(f.substr(0, f.size() - 8));
        if (taken.count(id)) id += "-app";
        add(out, taken, id, dir + "/" + f);
    }
}

}  // namespace zs
