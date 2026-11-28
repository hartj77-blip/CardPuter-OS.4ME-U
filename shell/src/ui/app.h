// App model for ZeroShell.
//
// An app owns a full-width content area (320x150 under the status bar).
// The shell calls on_key() before LVGL sees a key, so apps can grab keys;
// returning false passes the key on to the focused widget (lists, text areas).
#pragma once

#include <functional>
#include <memory>
#include <string>
#include <vector>

#include "lvgl.h"

namespace zs {

class App {
public:
    virtual ~App() = default;
    virtual void build(lv_obj_t* root) = 0;
    virtual void tick() {}                         // ~10 Hz
    virtual bool on_key(uint32_t key) { return false; }
    // Escape: return true if handled inside the app (e.g. leave a sub-page).
    virtual bool on_back() { return false; }

    lv_group_t* group = nullptr;   // focus group, set by the shell
};

struct AppInfo {
    std::string id;
    std::string name;
    const char* symbol;       // LV_SYMBOL_* or ZS_ICON_*
    uint32_t color;           // icon background, 0xRRGGBB
    std::function<std::unique_ptr<App>()> make;
    int order = 1000;         // place in the app grid (lower first); ties go by name
    bool builtin = true;      // false: loaded from an installed app module
    bool base = false;        // part of ZeroOS itself, can't be removed
};

// The apps on this device, in launcher order: the ones built into ZeroShell
// plus every installed app module (apps/registry.cpp, docs/APPS.md).
const std::vector<AppInfo>& app_registry();
// Look for installed or removed app modules again (after the Apps store
// installs or removes one). Never call it while a module's app is open.
void app_registry_reload();
// The entry for an app id, or nullptr if it isn't on this device.
const AppInfo* app_find(const std::string& id);
// Wi-Fi, Bluetooth and Battery were apps once and are sections of Settings
// now: true for their old ids ("wifi", "bluetooth", "battery"). Such an id
// is never an app, even if an old module for it is still installed, and
// shell_open_app() with it opens Settings there.
bool app_is_settings_section(const std::string& id);
// True if the open app came from a module that has since been replaced
// on disk (an upgrade): ZeroShell must restart to run the new code.
bool app_modules_stale();

// ---- App modules --------------------------------------------------------
// An installable app is a shared library, /usr/lib/zeroos/apps/<name>.so,
// built against these headers (shell/cmake/ZeroShellApps.cmake does it).
// It exports zs_app_module(), which returns its apps (one or more), ended
// by an entry whose id is nullptr. ZeroShell loads an app only if its abi
// matches ZS_APP_ABI. Bump ZS_APP_ABI whenever a header an app includes
// (ui/, hal/, apps/common.h, LVGL or lv_conf.h) changes in a way that
// breaks already-built modules: the store then offers only matching builds.
// 5: Hal gained wifi_adapters()/wifi_adapter()/set_wifi_adapter() in the
//    middle of its vtable and NetStatus gained wifi_iface. 0.9.5 shipped
//    that on 4, so abi4 apps called the wrong Hal methods (Jellyfin crashed
//    ZeroShell on open). New virtuals shift every slot after them: add them
//    last, or bump this.
#define ZS_APP_ABI 5

struct AppModule {
    int abi;                  // ZS_APP_ABI the module was built with
    const char* id;
    const char* name;
    const char* symbol;
    uint32_t color;
    int order;
    std::unique_ptr<App> (*make)();
};

// ---- Actions (docs/UX.md) ----------------------------------------------
// What a screen can do. The shell shows them three ways: the soft-key bar
// along the bottom (actions with a key), the menu on Tab and Ctrl + its
// letter (actions with a letter and a run). One list, so the hints can't go
// stale. A screen that sets actions is laid out kBodyH high (ui/theme.h),
// not kContentH: the bar takes the bottom kSoftkeyH.
struct Action {
    std::string label;             // "Rescan", "Forget", "Open"
    char letter = 0;               // 'a'..'z': in the menu, and Ctrl + it. 0: not in the menu
    const char* key = nullptr;     // shown in the soft-key bar ("Enter", "Esc", "Space"), or nullptr
    std::function<void()> run;     // nullptr: a hint only (the screen handles the key itself)
    bool enabled = true;           // greyed in the menu when false
    bool danger = false;           // destroys something: red, last in the menu, and asks first
    std::string confirm;           // danger: the question ("Forget HomeNet?"); default "<label>?"
};

// Shell services available to apps.
void shell_toast(const std::string& msg);
void shell_open_app(const std::string& id);
// Open an app and hand it a word (for example "scan" opens Camera's QR
// reader). The app reads it once with shell_app_arg() in build().
void shell_open_app(const std::string& id, const std::string& arg);
std::string shell_app_arg();
void shell_go_home();
void shell_add_focus(lv_obj_t* obj);   // add widget to the current app's focus group
void shell_screen_changed();            // power manager: screen dimmed, off or on
void shell_prepare_power_off();         // power manager: close the app and GPS track first
// Open the terminal on the built-in screen ("" = shell, or a command such as
// "ssh desktop"). False in the simulator, which has no real console.
bool shell_open_terminal(const std::string& command);
// Hand the screen to the camera app until it exits. False if it can't start.
bool shell_open_camera();
// Hand the screen to an app made for M5Stack's launcher (its .desktop Exec
// line) until it exits; then Home. False if it can't start.
bool shell_open_external(const std::string& command);
// For a screen that needs no typing: plain letters act as their Fn keys
// (f/z/x/c arrows, a/s/d volume, ...; platform/keys.h), Shift + letter is
// still the letter. Off again at each app switch; call it from build().
void shell_set_fn_layer(bool on);
// This screen's actions (see Action). Call again whenever the screen changes;
// an empty list hides the bar. Cleared at each app switch.
void shell_set_actions(std::vector<Action> actions);
// Ask before something that can't be undone: Enter runs on_yes, Esc doesn't.
// `yes` names the button ("Forget"). The one way ZeroOS asks (docs/UX.md).
void shell_confirm(const std::string& question, const std::string& detail, const std::string& yes,
                   std::function<void()> on_yes);

}  // namespace zs
