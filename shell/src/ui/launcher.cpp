// Home (docs/UX.md): a search box; the time and date beside the apps you
// opened last (Recent); the pinned apps on 1 to 7 along the bottom. Typing
// searches apps, settings and actions; Enter (or the result's number) opens
// the one in focus.
//
//   home      letters search; 1-7 pinned; Left/Right along a row, Up/Down
//             between the pins and Recent; Enter opens; Tab opens All apps
//   search    1-9 pick a result; Backspace edits; Esc clears
//   all apps  A-Z, typing filters (ui/list_view.h); Esc back. Tab on an app:
//             P then 1-7 pins it on that key (or moves it there), U unpins
//
// Pinned and recent apps are kept in ~/.config/zeroos/settings.conf
// (home.pinned, home.recent: app ids, comma-separated).
#include <algorithm>
#include <cctype>
#include <ctime>
#include <functional>
#include <map>
#include <sstream>

#include "hal/hal.h"
#include "platform/keys.h"
#include "ui/list_view.h"
#include "ui/power.h"
#include "ui/prefs.h"
#include "ui/quick_panel.h"
#include "ui/search_match.h"
#include "ui/shell.h"
#include "ui/suggest_bar.h"
#include "ui/theme.h"

namespace zs {

using namespace theme;

namespace {

constexpr int kPins = 7;           // keys 1..7
constexpr int kSearchH = 22;
// The band under the search box: the clock on the left, Recent on the right.
constexpr int kBandY = kSearchH + 5;
constexpr int kRecentX = 166, kRecents = 3;
constexpr int kRecentW = (kScreenW - 4 - kRecentX) / kRecents;
constexpr int kRecentY = kBandY + 14, kRecentH = 42;
// The pinned apps along the bottom, like a dock.
constexpr int kPinsH = 48, kPinsY = kBodyH - kPinsH - 2;
// A fresh ZeroOS pins the first of these that are installed, then Settings.
const char* const kDefaultPins[] = {"terminal", "files", "notes", "media", "remote", "camera", "apps"};

std::vector<std::string> split(const std::string& s) {
    std::vector<std::string> out;
    std::stringstream ss(s);
    std::string item;
    while (std::getline(ss, item, ','))
        if (!item.empty()) out.push_back(item);
    return out;
}

std::string join(const std::vector<std::string>& v) {
    std::string s;
    for (auto& x : v) s += (s.empty() ? "" : ",") + x;
    return s;
}

std::vector<std::string> pinned() {
    std::vector<std::string> out;
    // "\x01": never set (a fresh device). An empty string is "nothing pinned".
    std::string saved = prefs::get_str("home.pinned", "\x01");
    if (saved != "\x01") {
        for (auto& id : split(saved))
            if (app_find(id) && int(out.size()) < kPins) out.push_back(id);
        return out;
    }
    for (const char* id : kDefaultPins)
        if (app_find(id) && int(out.size()) < kPins - 1) out.push_back(id);
    if (app_find("settings")) out.push_back("settings");
    return out;
}

void save_pinned(const std::vector<std::string>& v) { prefs::set_str("home.pinned", join(v)); }

// The apps opened last, newest first, leaving out the pinned ones (they are
// a key away already).
std::vector<std::string> recent(const std::vector<std::string>& pins) {
    std::vector<std::string> out;
    for (auto& id : split(prefs::get_str("home.recent", "")))
        if (app_find(id) && std::find(pins.begin(), pins.end(), id) == pins.end()) out.push_back(id);
    return out;
}

// Lower case, letters and digits only: "Wi-Fi" and "wifi" are the same word.
std::string norm(const std::string& s) { return search_norm(s); }

// A Yaru-style icon: a white glyph on a coloured rounded square.
lv_obj_t* app_icon(lv_obj_t* parent, const AppInfo& info, int size) {
    lv_obj_t* box = lv_obj_create(parent);
    style_plain(box);
    lv_obj_set_size(box, size, size);
    lv_obj_set_style_radius(box, size * 9 / 32, 0);
    lv_obj_set_style_bg_color(box, c(info.color), 0);
    lv_obj_set_style_bg_opa(box, LV_OPA_COVER, 0);
    lv_obj_remove_flag(box, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_remove_flag(box, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_center(label(box, info.symbol, size >= 30 ? &zs_font_16 : &zs_font_12, 0xffffff));
    return box;
}

// Translucent white behind the item in focus, as in GNOME.
void style_item(lv_obj_t* o) {
    style_plain(o);
    lv_obj_set_style_outline_width(o, 0, LV_STATE_FOCUS_KEY);
    lv_obj_remove_flag(o, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(o, LV_OBJ_FLAG_CLICKABLE);
}

// A pinned or recent app: its icon as the author drew it; in focus, a white
// frame round the cell over a faint lift of the photo.
void style_pin(lv_obj_t* o) {
    style_item(o);
    lv_obj_set_style_bg_color(o, c(kGrey8), LV_STATE_FOCUSED);
    lv_obj_set_style_bg_opa(o, LV_OPA_20, LV_STATE_FOCUSED);
    lv_obj_set_style_border_color(o, c(kGrey8), LV_STATE_FOCUSED);
    lv_obj_set_style_border_width(o, 1, LV_STATE_FOCUSED);
}

// ---- Search ---------------------------------------------------------------

struct Result {
    const char* icon;
    std::string text;
    std::string kind;                      // "App", "Setting", "Action"
    std::function<void()> run;             // Enter
    std::vector<std::string> choices;      // a value row (Volume): Left/Right
    int at = 0;
    std::function<void(int)> change;
    int score = 0;                         // how well it matched (ui/search_match.h)
};

// More words an app is found by ("music" finds Media). Ids of apps that may
// not be installed are fine: only apps on the device are searched.
const std::map<std::string, const char*> kAppWords = {
    {"media", "music songs video player jellyfin audio podcast"},
    {"terminal", "shell console command ssh bash"},
    {"files", "folder documents browse storage"},
    {"notes", "text write memo editor"},
    {"settings", "preferences options config setup"},
    {"apps", "store install download add remove"},
    {"wifi", "wireless internet network"},
    {"bluetooth", "bt pair headphones speaker"},
    {"update", "upgrade version software"},
    {"sysmon", "system memory cpu task processes about"},
    {"battery", "power charge"},
    {"clock", "time alarm timer stopwatch date"},
    {"camera", "photo picture qr scan"},
    {"ir", "remote tv infrared"},
    {"remote", "tailscale vnc pc control"},
    {"games", "play emulator retro"},
    {"ai", "chat claude assistant"},
    {"gps", "location map track"},
    {"mesh", "meshtastic lora radio message"},
    {"tether", "hotspot phone usb internet share"},
    {"dock", "desk monitor hdmi"},
    {"car", "obd vehicle drive"},
    {"compass", "heading north direction"},
    {"sync", "syncthing backup"},
    {"code", "programming editor python dev"},
    {"repl", "python console dev"},
    {"git", "repository commit dev"},
    {"serial", "uart usb console dev"},
    {"browser", "web internet"},
    {"recorder", "voice audio record microphone"},
};

struct SettingEntry {
    const char* text;
    const char* words;   // more to match on
    const char* app;     // the app to open
    const char* arg;     // and what to hand it (a Settings section)
    const char* icon;
};
const SettingEntry kSettings[] = {
    {"Wi-Fi networks", "wireless internet join", "wifi", "", LV_SYMBOL_WIFI},
    {"Bluetooth", "bt", "settings", "bluetooth", LV_SYMBOL_BLUETOOTH},
    {"Bluetooth devices", "bt pair keyboard gamepad speaker headphones", "bluetooth", "", LV_SYMBOL_BLUETOOTH},
    {"Display", "screen brightness dim", "settings", "display", LV_SYMBOL_EYE_OPEN},
    {"Brightness", "screen display", "settings", "display", LV_SYMBOL_EYE_OPEN},
    {"Dim screen after", "display sleep timeout", "settings", "display", LV_SYMBOL_EYE_CLOSE},
    {"Screen off after", "display sleep timeout", "settings", "display", LV_SYMBOL_POWER},
    {"Word suggestions", "typing keyboard autocomplete autofill predict text", "settings", "display", LV_SYMBOL_KEYBOARD},
    {"Sound", "audio volume mute output speaker", "settings", "sound", LV_SYMBOL_VOLUME_MAX},
    {"Volume", "sound audio", "settings", "sound", LV_SYMBOL_VOLUME_MAX},
    {"Power profile", "battery saver performance", "settings", "battery", LV_SYMBOL_CHARGE},
    {"Battery & power", "charge", "settings", "battery", LV_SYMBOL_BATTERY_3},
    {"Tailscale", "remote vpn", "settings", "remote", LV_SYMBOL_SHUFFLE},
    {"Sync", "syncthing", "settings", "remote", LV_SYMBOL_LOOP},
    {"Software update", "upgrade version zeroos", "update", "", LV_SYMBOL_DOWNLOAD},
    {"About this device", "system memory storage cpu", "sysmon", "", LV_SYMBOL_LIST},
    {"Running programs", "kill stop force quit close task manager process memory frozen", "settings", "running",
     LV_SYMBOL_PLAY},
    {"Restart or shut down", "reboot power off", "settings", "power", LV_SYMBOL_POWER},
};

std::vector<std::string> percents() {
    std::vector<std::string> v;
    for (int p = 0; p <= 100; p += 5) v.push_back(std::to_string(p) + "%");
    return v;
}

std::vector<Result> search(const std::string& q) {
    std::vector<Result> out;
    // Apps.
    for (const AppInfo& a : app_registry()) {
        auto w = kAppWords.find(a.id);
        int sc = search_score(a.name, a.id + " " + (w == kAppWords.end() ? "" : w->second), q);
        if (sc) {
            std::string id = a.id;
            out.push_back({a.symbol, a.name, "App", [id] { shell_open_app(id); }});
            out.back().score = sc;
        }
    }
    // Actions: done right here, without opening anything.
    auto& h = hal();
    NetStatus n = h.net();
    auto toggle = [&](const char* icon, const std::string& text, const char* words, std::function<void()> run) {
        if (int sc = search_score(text, words, q)) {
            out.push_back({icon, text, "Action", std::move(run)});
            out.back().score = sc;
        }
    };
    // Each reads the state when it runs (the row is relabelled after).
    toggle(LV_SYMBOL_WIFI, n.wifi_enabled ? "Turn Wi-Fi off" : "Turn Wi-Fi on", "wireless", [] {
        bool on = hal().net().wifi_enabled;
        hal().set_wifi_enabled(!on);
        shell_toast(on ? "Wi-Fi off" : "Wi-Fi on");
    });
    toggle(LV_SYMBOL_BLUETOOTH, n.bt_enabled ? "Turn Bluetooth off" : "Turn Bluetooth on", "bt", [] {
        bool on = hal().net().bt_enabled;
        hal().set_bt_enabled(!on);
        shell_toast(on ? "Bluetooth off" : "Bluetooth on");
    });
    toggle(LV_SYMBOL_MUTE, h.muted() ? "Sound on" : "Mute", "sound volume silent", [] {
        bool m = hal().muted();
        hal().set_muted(!m);
        shell_toast(m ? "Sound on" : "Sound off");
    });
    toggle(LV_SYMBOL_KEYBOARD, suggest_enabled() ? "Turn word suggestions off" : "Turn word suggestions on",
           "typing keyboard autocomplete autofill predict", [] {
        bool on = suggest_enabled();
        suggest_set_enabled(!on);
        shell_toast(on ? "Word suggestions off" : "Word suggestions on");
    });
    toggle(LV_SYMBOL_TRASH, "Forget learned words", "typing suggestions autocomplete clear reset", [] {
        shell_confirm("Forget learned words?", "Suggestions go back to the built-in words.", "Forget", [] {
            suggest_forget();
            shell_toast("Learned words forgotten");
        });
    });
    if (int sc = search_score("Volume", "sound audio louder quieter", q)) {
        Result r{LV_SYMBOL_VOLUME_MAX, "Volume", "Action", nullptr, percents(), (h.volume() + 2) / 5};
        r.change = [](int i) { hal().set_volume(i * 5); };
        r.score = sc;
        out.push_back(r);
    }
    if (int sc = search_score("Brightness", "screen display light dim", q)) {
        Result r{LV_SYMBOL_EYE_OPEN, "Brightness", "Action", nullptr, percents(), (power().user_brightness() + 2) / 5};
        r.change = [](int i) { power().set_user_brightness(i * 5); };
        r.score = sc;
        out.push_back(r);
    }
    // Settings: open the section.
    for (const SettingEntry& s : kSettings) {
        int sc = search_score(s.text, s.words, q);
        if (!sc || !app_find(s.app)) continue;
        std::string app = s.app, arg = s.arg;
        out.push_back({s.icon, s.text, "Setting", [app, arg] {
                           if (arg.empty()) shell_open_app(app);
                           else shell_open_app(app, arg);
                       }});
        out.back().score = sc;
    }
    // Best match first; equally good ones as docs/UX.md groups them: apps,
    // then settings, then actions (each group in the order found).
    auto group = [](const Result& r) { return r.kind == "App" ? 0 : r.kind == "Setting" ? 1 : 2; };
    std::stable_sort(out.begin(), out.end(), [&](const Result& a, const Result& b) {
        if (a.score != b.score) return a.score > b.score;
        return group(a) < group(b);
    });
    return out;
}

// ---- Home -------------------------------------------------------------------

class Launcher;
Launcher* g_home = nullptr;   // the home screen, while it is open

class Launcher : public App {
public:
    Launcher() { g_home = this; }
    ~Launcher() override {
        lv_async_call_cancel(all_apps_cb, this);
        if (g_home == this) g_home = nullptr;
    }

    // The app list was read again (ui/app_watch.cpp): draw it anew, on the
    // same page. All apps keeps what was typed and the app in focus.
    void apps_changed() {
        if (!all_apps_) {
            refresh();
            return;
        }
        int i = all_.focused();
        std::string id = i >= 0 ? all_ids_[size_t(i)] : "";
        std::string typed = all_.filter();
        show_all_apps();
        for (char ch : typed) all_.key(uint32_t((unsigned char)ch));
        auto it = std::find(all_ids_.begin(), all_ids_.end(), id);
        if (it != all_ids_.end()) all_.focus(int(it - all_ids_.begin()));
    }

    void build(lv_obj_t* root) override {
        root_ = root;
        // The photograph is the screen's, under the status bar too (Shell::
        // set_photo). Search and All apps cover it with plain black.
        lv_obj_set_style_bg_color(root, c(kBg), 0);
        show_home();
    }

    // The clock on home, once a minute.
    void tick() override {
        if (!time_) return;
        time_t now = time(nullptr);
        tm lt{};
        localtime_r(&now, &lt);
        if (lt.tm_min == shown_minute_) return;
        shown_minute_ = lt.tm_min;
        char buf[48];
        strftime(buf, sizeof buf, "%H:%M", &lt);
        lv_label_set_text(time_, buf);
        strftime(buf, sizeof buf, "%A %-d %B", &lt);
        lv_label_set_text(date_, buf);
    }

    bool on_key(uint32_t key) override {
        if (all_apps_) {
            if (!pin_pending_.empty()) {   // Tab, P: waiting for 1 to 7
                std::string id = pin_pending_;
                cancel_pin();
                if (key >= '1' && key < '1' + kPins) pin_to(id, int(key - '1'));
                else shell_toast("Not pinned");
                return true;
            }
            return all_.key(key);
        }
        if (key == kKeyNext) {   // Tab: All apps, where pins are made
            show_all_apps();
            return true;
        }
        // Typing: the search. Digits pick (pinned apps, or the results).
        if (key == LV_KEY_BACKSPACE || key == LV_KEY_DEL) {
            if (query_.empty()) return true;
            query_.pop_back();
            refresh();
            return true;
        }
        if (key >= 32 && key < 127 && !std::isdigit(int(key)) && !(key == ' ' && query_.empty())) {
            if (query_.size() < 24) query_ += char(key);
            refresh();
            return true;
        }
        if (!query_.empty()) return results_.key(key);   // 1-9, Left/Right on a value row
        if (key >= '1' && key <= '9') {   // the pinned apps (8 and 9: nothing)
            size_t i = key - '1';
            if (i < pins_.size()) shell_open_app(pins_[i]);
            return true;
        }
        return home_nav(key);
    }

    bool on_back() override {
        if (all_apps_ && !pin_pending_.empty()) {
            cancel_pin();
            return true;
        }
        if (all_apps_) {
            show_home();
            return true;
        }
        if (!query_.empty()) {
            query_.clear();
            refresh();
            return true;
        }
        return true;   // home: Esc has nowhere further to go
    }

private:
    // ---- The page --------------------------------------------------------

    void clear() {
        lv_obj_clean(root_);
        lv_group_remove_all_objs(group);
        pin_objs_.clear();
        recent_objs_.clear();
        open_ids_.clear();
        time_ = date_ = nullptr;
        results_.reset();   // their rows went with the screen
        all_.reset();
        box_ = nullptr;
        all_head_ = nullptr;
    }

    void show_home() {
        all_apps_ = false;
        query_.clear();
        refresh();
    }

    void search_box() {
        box_ = lv_obj_create(root_);
        style_plain(box_);
        lv_obj_set_size(box_, kScreenW - 8, kSearchH);
        lv_obj_set_pos(box_, 4, 3);
        lv_obj_set_style_radius(box_, 0, 0);
        lv_obj_set_style_bg_color(box_, c(0x000000), 0);
        lv_obj_set_style_bg_opa(box_, LV_OPA_60, 0);
        lv_obj_remove_flag(box_, LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_t* p = label(box_, LV_SYMBOL_RIGHT, &zs_font_12, kAccent);
        lv_obj_align(p, LV_ALIGN_LEFT_MID, 7, 0);
        bool empty = query_.empty();
        lv_obj_t* t = label(box_, empty ? "Type to search apps, settings, actions" : (query_ + "_").c_str(),
                            empty ? &zs_font_12 : &zs_font_14, empty ? kGrey5 : kText);
        lv_obj_set_width(t, kScreenW - 40);
        lv_label_set_long_mode(t, LV_LABEL_LONG_DOT);
        lv_obj_align(t, LV_ALIGN_LEFT_MID, 22, 0);
    }

    void photo(bool on) {
        Shell::get().set_photo(on);
        lv_obj_set_style_bg_opa(root_, on ? LV_OPA_TRANSP : LV_OPA_COVER, 0);
    }

    void refresh() {
        clear();
        photo(query_.empty());
        search_box();
        if (query_.empty()) home_page();
        else results_page();
    }

    void home_page() {
        pins_ = pinned();
        clock_page();
        recent_page();
        constexpr int w = kScreenW / kPins;
        for (int i = 0; i < int(pins_.size()); ++i) {
            lv_obj_t* cell = app_cell(pins_[size_t(i)], 1 + i * w, kPinsY, w - 2, kPinsH, 26);
            // The number key, in the corner.
            lv_obj_t* badge = lv_obj_create(cell);
            style_plain(badge);
            lv_obj_set_size(badge, 11, 11);
            lv_obj_set_style_radius(badge, 0, 0);
            lv_obj_set_style_bg_color(badge, c(0x000000), 0);
            lv_obj_set_style_bg_opa(badge, LV_OPA_60, 0);
            lv_obj_remove_flag(badge, LV_OBJ_FLAG_CLICKABLE);
            char d[2] = {char('1' + i), 0};
            lv_obj_center(label(badge, d, &zs_font_12, 0xffffff));
            lv_obj_set_pos(badge, 1, 0);
            pin_objs_.push_back(cell);
        }
        if (!pin_objs_.empty()) focus_pin(pin_sel_);
        else if (!recent_objs_.empty()) lv_group_focus_obj(recent_objs_.front());
        set_home_actions();
    }

    // The time, large, over the photo, and the date under it.
    void clock_page() {
        time_ = label(root_, "", &zs_font_28, kText);
        lv_obj_set_pos(time_, 10, kBandY + 3);
        date_ = label(root_, "", &zs_font_12, kGrey7);
        lv_obj_set_width(date_, kRecentX - 14);   // a long date stops short of Recent
        lv_label_set_long_mode(date_, LV_LABEL_LONG_DOT);
        lv_obj_set_pos(date_, 11, kBandY + 36);
        shown_minute_ = -1;
        tick();
    }

    void recent_page() {
        lv_obj_t* head = label(root_, "RECENT", &zs_font_12, kMuted);
        lv_obj_set_pos(head, kRecentX + 4, kBandY);
        std::vector<std::string> ids = recent(pins_);
        if (ids.size() > size_t(kRecents)) ids.resize(size_t(kRecents));
        for (size_t i = 0; i < ids.size(); ++i)
            recent_objs_.push_back(
                app_cell(ids[i], kRecentX + int(i) * kRecentW, kRecentY, kRecentW - 2, kRecentH, 22));
        if (ids.empty()) {
            lv_obj_t* tip = label(root_, "Apps you open show up here", &zs_font_12, kMuted);
            lv_obj_set_width(tip, kScreenW - kRecentX - 8);
            lv_obj_set_pos(tip, kRecentX + 4, kRecentY + 4);
        }
    }

    // An app's icon with its name under it, opened by Enter.
    lv_obj_t* app_cell(const std::string& id, int x, int y, int w, int h, int icon) {
        const AppInfo* a = app_find(id);
        lv_obj_t* cell = lv_obj_create(root_);
        style_pin(cell);
        lv_obj_set_size(cell, w, h);
        lv_obj_set_pos(cell, x, y);
        lv_obj_t* ic = app_icon(cell, *a, icon);
        lv_obj_align(ic, LV_ALIGN_TOP_MID, 0, h > 44 ? 4 : 2);
        lv_obj_t* name = label(cell, a->name.c_str(), &zs_font_12, 0xffffff);
        lv_obj_set_size(name, w, 14);
        lv_obj_set_style_text_align(name, LV_TEXT_ALIGN_CENTER, 0);
        lv_label_set_long_mode(name, LV_LABEL_LONG_DOT);
        lv_obj_set_style_text_letter_space(name, -1, 0);
        lv_obj_align(name, LV_ALIGN_BOTTOM_MID, 0, h > 44 ? -2 : 0);
        add_open(cell, id);
        return cell;
    }

    void add_open(lv_obj_t* obj, const std::string& id) {
        open_ids_[obj] = id;
        lv_obj_add_event_cb(
            obj,
            [](lv_event_t* e) {
                auto* self = static_cast<Launcher*>(lv_event_get_user_data(e));
                auto it = self->open_ids_.find(lv_event_get_current_target_obj(e));
                if (it != self->open_ids_.end()) shell_open_app(it->second);
            },
            LV_EVENT_CLICKED, this);
        shell_add_focus(obj);
    }

    // Left/Right along a row (round the end), Up/Down between the pinned
    // apps and Recent. Back on the pins, the one last in focus.
    bool home_nav(uint32_t key) {
        lv_obj_t* f = lv_group_get_focused(group);
        auto pin = std::find(pin_objs_.begin(), pin_objs_.end(), f);
        auto rec = std::find(recent_objs_.begin(), recent_objs_.end(), f);
        bool on_pin = pin != pin_objs_.end();
        if (!on_pin && rec == recent_objs_.end()) return false;
        std::vector<lv_obj_t*>& row = on_pin ? pin_objs_ : recent_objs_;
        int i = int((on_pin ? pin : rec) - row.begin()), n = int(row.size());
        switch (key) {
            case LV_KEY_LEFT:
            case LV_KEY_RIGHT: {
                int j = (i + (key == LV_KEY_RIGHT ? 1 : n - 1)) % n;
                if (on_pin) focus_pin(j);
                else lv_group_focus_obj(row[size_t(j)]);
                return true;
            }
            case LV_KEY_UP:
            case LV_KEY_DOWN:
                if (on_pin) {
                    if (!recent_objs_.empty())
                        lv_group_focus_obj(recent_objs_[size_t(std::min(rec_sel_, int(recent_objs_.size()) - 1))]);
                } else {
                    rec_sel_ = i;
                    focus_pin(pin_sel_);
                }
                return true;
            default: return false;
        }
    }

    void focus_pin(int i) {
        if (pin_objs_.empty()) return;
        pin_sel_ = std::clamp(i, 0, int(pin_objs_.size()) - 1);
        lv_group_focus_obj(pin_objs_[size_t(pin_sel_)]);
    }

    void set_home_actions() {
        shell_set_actions({
            {"Open", 0, "Enter", nullptr},
            {"Search", 0, "a-z", nullptr},
            {"All apps", 0, "Tab", nullptr},
        });
    }

    static std::string app_name(const std::string& id) {
        const AppInfo* a = app_find(id);
        return a ? a->name : id;
    }

    // All apps, Tab, P: the next key 1 to 7 says where it goes.
    void ask_pin_key() {
        int i = all_.focused();
        if (i < 0) {
            shell_toast("Pick an app first");
            return;
        }
        pin_pending_ = all_ids_[size_t(i)];
        if (all_head_) lv_label_set_text(all_head_, ("PIN " + app_name(pin_pending_) + ": PRESS 1 TO 7").c_str());
    }

    void cancel_pin() {
        pin_pending_.clear();
        if (all_head_) lv_label_set_text(all_head_, fmt_count(int(all_ids_.size())).c_str());
    }

    // Put an app on key `slot` (0-based). An app already there swaps places
    // with it if it was pinned too, else is unpinned. Past the last pin, it
    // goes on the next free key.
    void pin_to(const std::string& id, int slot) {
        std::vector<std::string> p = pinned();
        std::string name = app_name(id);
        auto it = std::find(p.begin(), p.end(), id);
        int old = it == p.end() ? -1 : int(it - p.begin());
        if (old == slot) {
            shell_toast(name + " is already on " + std::to_string(slot + 1));
        } else if (slot >= int(p.size()) - (old >= 0 ? 1 : 0)) {
            if (old >= 0) p.erase(it);
            p.push_back(id);
            shell_toast("Pinned " + name + " on " + std::to_string(p.size()));
        } else if (old >= 0) {
            std::string other = p[size_t(slot)];
            std::swap(p[size_t(old)], p[size_t(slot)]);
            shell_toast(name + " on " + std::to_string(slot + 1) + ", " + app_name(other) + " on " +
                        std::to_string(old + 1));
        } else {
            std::string other = p[size_t(slot)];
            p[size_t(slot)] = id;
            shell_toast("Pinned " + name + " on " + std::to_string(slot + 1) + " (" + app_name(other) +
                        " unpinned)");
        }
        save_pinned(p);
        relabel_all();   // in place: the filter and focus stay
    }

    void unpin_focused() {
        int i = all_.focused();
        if (i < 0) return;
        const std::string& id = all_ids_[size_t(i)];
        std::vector<std::string> p = pinned();
        auto it = std::find(p.begin(), p.end(), id);
        if (it == p.end()) {
            shell_toast(app_name(id) + " isn't pinned");
            return;
        }
        p.erase(it);
        save_pinned(p);
        shell_toast("Unpinned " + app_name(id));
        relabel_all();
    }

    // All apps' "Pinned n" labels, after a pin or unpin.
    void relabel_all() {
        std::vector<std::string> p = pinned();
        for (size_t i = 0; i < all_ids_.size(); ++i) {
            auto at = std::find(p.begin(), p.end(), all_ids_[i]);
            all_.set_value(int(i), at == p.end() ? "" : "Pinned " + std::to_string(int(at - p.begin()) + 1));
        }
    }

    // ---- Search results --------------------------------------------------

    void results_page() {
        results_.build(root_, 2, kSearchH + 5, kScreenW - 4, kBodyH - kSearchH - 6);
        results_.set_empty_text("Nothing found for \"" + query_ + "\"");
        for (Result& r : search(query_)) {
            if (!r.choices.empty()) {
                results_.add_choice(r.icon, r.text, r.choices, r.at, r.change);
            } else if (r.kind == "Action") {
                // Done in place: show the results again, relabelled ("Turn Wi-Fi on").
                std::function<void()> run = r.run;
                results_.add(r.icon, r.text, r.kind, [this, run] {
                    int at = results_.focused();
                    run();
                    refresh();
                    results_.focus(at);
                });
            } else {
                results_.add(r.icon, r.text, r.kind, r.run);
            }
        }
        results_.finish();
        shell_set_actions({{"Open", 0, "Enter", nullptr}, {"Pick", 0, "1-9", nullptr}, {"Clear", 0, "Esc", nullptr}});
    }

    // ---- All apps --------------------------------------------------------

    static void all_apps_cb(void* p) { static_cast<Launcher*>(p)->show_all_apps(); }

    void show_all_apps() {
        all_apps_ = true;
        query_.clear();
        clear();
        photo(false);
        std::vector<const AppInfo*> apps;
        for (const AppInfo& a : app_registry()) apps.push_back(&a);
        std::sort(apps.begin(), apps.end(), [](const AppInfo* x, const AppInfo* y) { return norm(x->name) < norm(y->name); });
        pin_pending_.clear();
        all_head_ = label(root_, fmt_count(int(apps.size())).c_str(), &zs_font_12, kMuted);
        lv_obj_set_pos(all_head_, 9, 3);
        all_.build(root_, 2, 18, kScreenW - 4, kBodyH - 19);
        all_ids_.clear();
        std::vector<std::string> p = pinned();
        for (const AppInfo* a : apps) {
            std::string id = a->id;
            auto at = std::find(p.begin(), p.end(), id);
            std::string value = at == p.end() ? "" : "Pinned " + std::to_string(int(at - p.begin()) + 1);
            all_.add(a->symbol, a->name, value, [id] { shell_open_app(id); });
            all_ids_.push_back(id);
        }
        all_.set_empty_text("No app by that name");
        all_.finish();
        shell_set_actions({
            {"Open", 0, "Enter", nullptr},
            {"Find", 0, "a-z", nullptr},
            {"Pin to a key (1-7)", 'p', nullptr, [this] { ask_pin_key(); }},
            {"Unpin", 'u', nullptr, [this] { unpin_focused(); }},
        });
    }

    static std::string fmt_count(int n) { return "ALL APPS  (" + std::to_string(n) + ")"; }

    lv_obj_t* root_ = nullptr;
    lv_obj_t* box_ = nullptr;
    lv_obj_t* time_ = nullptr;
    lv_obj_t* date_ = nullptr;
    int shown_minute_ = -1;
    lv_obj_t* all_head_ = nullptr;
    std::string query_;
    std::string pin_pending_;   // All apps: Tab, P, then 1 to 7
    bool all_apps_ = false;
    std::vector<std::string> pins_;
    std::vector<lv_obj_t*> pin_objs_, recent_objs_;
    std::vector<std::string> all_ids_;
    std::map<lv_obj_t*, std::string> open_ids_;
    int pin_sel_ = 0, rec_sel_ = 0;
    ListView results_, all_;
};

}  // namespace

std::unique_ptr<App> make_launcher() { return std::make_unique<Launcher>(); }

void home_apps_changed() {
    if (g_home) g_home->apps_changed();
}

// An app was opened: it goes to the front of Recent (home shows the newest
// few that aren't pinned).
void home_opened(const std::string& id) {
    if (id == "home" || id.empty()) return;
    std::vector<std::string> r = split(prefs::get_str("home.recent", ""));
    r.erase(std::remove(r.begin(), r.end(), id), r.end());
    r.insert(r.begin(), id);
    if (r.size() > 12) r.resize(12);
    prefs::set_str("home.recent", join(r));
}

}  // namespace zs
