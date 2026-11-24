// See ui/app_watch.h.
#include "ui/app_watch.h"

#include "lvgl.h"
#include "platform/platform.h"
#include "ui/actions.h"
#include "ui/app.h"
#include "ui/quick_panel.h"
#include "ui/shell.h"

#if defined(__linux__) && !defined(__EMSCRIPTEN__)
#include <sys/inotify.h>
#include <sys/stat.h>
#include <unistd.h>

#include <cstdio>

namespace zs {

namespace {
constexpr uint32_t kSettleMs = 1500;

// In the folder itself: anything that adds, removes or replaces a file.
// dpkg writes x.dpkg-new and renames it (MOVED_TO); cp ends in CLOSE_WRITE.
constexpr uint32_t kDirMask = IN_CREATE | IN_DELETE | IN_MOVED_TO | IN_MOVED_FROM | IN_CLOSE_WRITE |
                              IN_DELETE_SELF | IN_MOVE_SELF | IN_ONLYDIR;
// A folder that isn't there yet (the M5 one appears with the first M5 app;
// dpkg may delete it with the last): its nearest parent that is, for its
// creation only.
constexpr uint32_t kParentMask = IN_CREATE | IN_MOVED_TO | IN_DELETE_SELF | IN_MOVE_SELF | IN_ONLYDIR;

int g_fd = -1;
std::vector<int> g_wds;          // watches now on
std::vector<bool> g_is_parent;   // ...and which are stand-ins for a missing folder
lv_timer_t* g_settle = nullptr;
bool g_changed = false;
bool g_told_stale = false;

void settled(lv_timer_t*);

bool is_dir(const std::string& p) {
    struct stat st{};
    return stat(p.c_str(), &st) == 0 && S_ISDIR(st.st_mode);
}

// (Re)place the watches: each folder, or its nearest parent that exists.
void arm() {
    for (int wd : g_wds) inotify_rm_watch(g_fd, wd);   // their IN_IGNORED events are skipped
    g_wds.clear();
    g_is_parent.clear();
    for (std::string dir : app_registry_dirs()) {
        bool parent = false;
        while (!dir.empty() && dir != "/" && !is_dir(dir)) {
            dir = dir.substr(0, dir.find_last_of('/'));
            if (dir.empty()) dir = "/";
            parent = true;
        }
        int wd = inotify_add_watch(g_fd, dir.c_str(), parent ? kParentMask : kDirMask);
        if (wd < 0) continue;
        g_wds.push_back(wd);
        g_is_parent.push_back(parent);
    }
}

void start_settle() {
    if (g_settle) {
        lv_timer_reset(g_settle);
        return;
    }
    g_settle = lv_timer_create(settled, kSettleMs, nullptr);
    lv_timer_set_repeat_count(g_settle, 1);   // deletes itself after it runs
}

void settled(lv_timer_t*) {
    g_settle = nullptr;
    // Reading the list again replaces every entry in it. Only on the home
    // screen is nothing holding one (app.h, app_registry_reload()).
    if (Shell::get().current_app() != "home") {
        g_changed = true;
        return;
    }
    // A menu, question or the taskbar panel is open over home: they hold
    // home's actions. Try again once they are closed.
    if (actions_menu_is_open() || actions_sheet_is_open() || actions_confirm_is_open() || quick_panel_is_open()) {
        start_settle();
        return;
    }
    std::fprintf(stderr, "zeroshell: apps changed on disk, reading them again\n");
    app_registry_reload();
    g_changed = false;
    home_apps_changed();
    // An app that was already loaded was replaced (an upgrade): its old code
    // stays in use until ZeroShell starts again. The Apps screen restarts by
    // itself after its own upgrades; from the command line, say so once.
    if (app_modules_stale() && !g_told_stale) {
        g_told_stale = true;
        shell_toast("An app was updated: restart to use the new version");
    }
}

void readable(void*) {
    alignas(inotify_event) char buf[4096];
    bool any = false, rearm = false;
    for (;;) {
        ssize_t n = read(g_fd, buf, sizeof buf);
        if (n <= 0) break;   // EAGAIN: all read
        for (char* p = buf; p < buf + n;) {
            auto* e = reinterpret_cast<inotify_event*>(p);
            p += sizeof(inotify_event) + e->len;
            if (e->mask & IN_IGNORED) continue;   // a watch we took off in arm()
            any = true;
            if (e->mask & (IN_DELETE_SELF | IN_MOVE_SELF | IN_Q_OVERFLOW)) rearm = true;
            for (size_t i = 0; i < g_wds.size(); ++i)
                if (g_wds[i] == e->wd && g_is_parent[i]) rearm = true;   // the folder may be there now
        }
    }
    if (!any) return;
    if (rearm) arm();
    start_settle();   // wait until the files stop changing: each event starts the wait again
}
}  // namespace

void app_watch_start() {
    if (g_fd >= 0) return;
    g_fd = inotify_init1(IN_NONBLOCK | IN_CLOEXEC);
    if (g_fd < 0) {
        std::perror("zeroshell: inotify");
        return;
    }
    arm();
    platform_watch_fd(g_fd, readable, nullptr);
}

bool app_watch_take_changed() {
    bool c = g_changed;
    g_changed = false;
    return c;
}

}  // namespace zs

#else   // no inotify (the browser build): new apps show after a restart

namespace zs {
void app_watch_start() {}
bool app_watch_take_changed() { return false; }
}  // namespace zs

#endif
