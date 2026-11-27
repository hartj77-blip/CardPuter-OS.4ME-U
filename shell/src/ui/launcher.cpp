// Home (docs/UX.md): a search box, the pinned apps on 1 to 7, what you opened
// last, and All apps, with what is playing, the time and the date in the
// corner. Typing searches apps, settings and actions; Enter (or
// the result's number) opens the one in focus.
//
//   home      letters search; 1-7 pinned; Up/Down/Left/Right move; Enter opens;
//             Tab opens All apps
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
#include "ui/player.h"
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
constexpr int kPinW = kScreenW / kPins;
constexpr int kSearchH = 22, kPinsY = kSearchH + 5, kPinsH = 36, kRecentY = kPinsY + kPinsH + 2;
constexpr int kRowH = 19, kRecents = 5;
// Recent and All apps take the left of the screen; the clock, the bottom right.
constexpr int kRowW = 150;   // room for a 16-letter name
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

std::vector<std::string> recent() {
    std::vector<std::string> out;
    for (auto& id : split(prefs::get_str("home.recent", "")))
        if (app_find(id)) out.push_back(id);
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

// A pinned app: its icon as the author drew it; in focus, a white frame
// round the cell over a faint lift of the photo.
void style_pin(lv_obj_t* o) {
    style_item(o);
    lv_obj_set_style_bg_color(o, c(kGrey8), LV_STATE_FOCUSED);
    lv_obj_set_style_bg_opa(o, LV_OPA_20, LV_STATE_FOCUSED);
    lv_obj_set_style_border_color(o, c(kGrey8), LV_STATE_FOCUSED);
    lv_obj_set_style_border_width(o, 1, LV_STATE_FOCUSED);
}

// A Recent row: lit white with black text in focus, like every list. Labels
// that should follow it take their colour from the row (no colour of their own).
void style_row(lv_obj_t* o) {
    style_item(o);
    lv_obj_set_style_text_color(o, c(kText), 0);
    style_focus_fill(o, LV_STATE_FOCUSED);
    lv_obj_set_style_text_color(o, c(kOnFocus), LV_STATE_FOCUSED);
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
    {"update", "upgrade version software"},
    {"sysmon", "system memory cpu task processes about"},
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
    {"Wi-Fi", "wireless internet network networks join", "settings", "wifi", LV_SYMBOL_WIFI},
    {"Bluetooth", "bt pair devices keyboard gamepad speaker headphones", "settings", "bluetooth",
     LV_SYMBOL_BLUETOOTH},
    {"Display", "screen brightness dim", "settings", "display", LV_SYMBOL_EYE_OPEN},
    {"Brightness", "screen display", "settings", "display", LV_SYMBOL_EYE_OPEN},
    {"Dim screen after", "display sleep timeout", "settings", "display", LV_SYMBOL_EYE_CLOSE},
    {"Screen off after", "display sleep timeout", "settings", "display", LV_SYMBOL_POWER},
    {"Word suggestions", "typing keyboard autocomplete autofill predict text", "settings", "display", LV_SYMBOL_KEYBOARD},
    {"Sound", "audio volume mute output speaker", "settings", "sound", LV_SYMBOL_VOLUME_MAX},
    {"Volume", "sound audio", "settings", "sound", LV_SYMBOL_VOLUME_MAX},
    {"Power profile", "battery saver performance", "settings", "battery", LV_SYMBOL_CHARGE},
    {"Battery & power", "charge time left draw voltage current temperature health cycles", "settings", "battery",
     LV_SYMBOL_BATTERY_3},
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
        std::string kw = a.id + " " + (w == kAppWords.end() ? "" : w->second);
        int sc = search_score(a.name, kw, q);
        // "dev" finds Code ahead of "About this device": an app's own word
        // beats the same letters inside a setting's name.
        if (sc && sc < 70 && search_keyword_starts(kw, q)) sc = 70;
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

    // Home's clock, once a minute, and what is playing.
    void tick() override {
        if (!time_) return;
        PlaybackState pb = hal().playback();
        std::string playing;
        if (!pb.path.empty()) {
            const QueueItem* q = player_current();
            playing = std::string(pb.paused ? LV_SYMBOL_PAUSE "  " : LV_SYMBOL_PLAY "  ") +
                      (q ? q->title : pb.path.substr(pb.path.rfind('/') + 1));
        }
        if (playing != shown_playing_) {
            shown_playing_ = playing;
            lv_label_set_text(playing_, playing.c_str());
        }
        time_t now = time(nullptr);
        tm lt{};
        localtime_r(&now, &lt);
        if (lt.tm_min == shown_minute_) return;
        shown_minute_ = lt.tm_min;
        char buf[32];
        strftime(buf, sizeof buf, "%H:%M", &lt);
        lv_label_set_text(time_, buf);
        strftime(buf, sizeof buf, "%a %-d %b", &lt);
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
        row_objs_.clear();
        row_ids_.clear();
        open_ids_.clear();
        row_runs_.clear();
        results_.reset();   // their rows went with the screen
        time_ = date_ = playing_ = nullptr;
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
        const auto& apps = app_registry();
        (void)apps;
        pins_ = pinned();
        for (int i = 0; i < int(pins_.size()); ++i) {
            const AppInfo* a = app_find(pins_[size_t(i)]);
            if (!a) continue;
            lv_obj_t* cell = lv_obj_create(root_);
            style_pin(cell);
            lv_obj_set_size(cell, kPinW - 2, kPinsH);
            lv_obj_set_pos(cell, 1 + i * kPinW, kPinsY);
            lv_obj_t* ic = app_icon(cell, *a, 20);
            lv_obj_align(ic, LV_ALIGN_TOP_MID, 0, 1);
            lv_obj_t* name = label(cell, a->name.c_str(), &zs_font_12, 0xffffff);
            lv_obj_set_width(name, kPinW - 2);
            lv_obj_set_style_text_align(name, LV_TEXT_ALIGN_CENTER, 0);
            lv_label_set_long_mode(name, LV_LABEL_LONG_DOT);
            lv_obj_set_style_text_letter_space(name, -1, 0);
            lv_obj_align(name, LV_ALIGN_BOTTOM_MID, 0, 0);
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
            add_open(cell, pins_[size_t(i)]);
            pin_objs_.push_back(cell);
        }

        lv_obj_t* head = label(root_, "RECENT", &zs_font_12, kMuted);
        lv_obj_set_pos(head, 9, kRecentY);

        rows_box_ = lv_obj_create(root_);
        style_plain(rows_box_);
        lv_obj_set_pos(rows_box_, 4, kRecentY + 14);
        lv_obj_set_size(rows_box_, kRowW, kBodyH - kRecentY - 14);
        lv_obj_set_flex_flow(rows_box_, LV_FLEX_FLOW_COLUMN);
        lv_obj_set_style_pad_row(rows_box_, 1, 0);
        lv_obj_set_scrollbar_mode(rows_box_, LV_SCROLLBAR_MODE_OFF);
        int shown = 0;
        for (auto& id : recent()) {
            if (shown == kRecents) break;
            const AppInfo* a = app_find(id);
            if (!a) continue;
            add_row(a->symbol, a->color, a->name, id, [id] { shell_open_app(id); });
            ++shown;
        }
        if (shown == 0) {
            lv_obj_t* tip = label(rows_box_, "Opened apps show here", &zs_font_12, kMuted);
            lv_obj_set_style_pad_left(tip, 5, 0);
        }
        add_row(ZS_ICON_GRID, 0x5e6c7b, "All apps", "",
                [this] { lv_async_call(all_apps_cb, this); }, std::to_string(app_registry().size()));

        // The time and date, in the corner under the pins.
        time_ = label(root_, "", &zs_font_28, kText);
        lv_obj_align(time_, LV_ALIGN_BOTTOM_RIGHT, -8, -18);
        date_ = label(root_, "", &zs_font_12, kGrey7);
        lv_obj_align(date_, LV_ALIGN_BOTTOM_RIGHT, -9, -3);
        // Above them, the track playing or paused (none: nothing).
        playing_ = label(root_, "", &zs_font_12, kGrey7);
        lv_obj_set_size(playing_, kScreenW - kRowW - 14, 14);
        lv_label_set_long_mode(playing_, LV_LABEL_LONG_DOT);
        lv_obj_set_style_text_align(playing_, LV_TEXT_ALIGN_RIGHT, 0);
        lv_obj_align(playing_, LV_ALIGN_BOTTOM_RIGHT, -9, -50);
        shown_playing_.clear();
        shown_minute_ = -1;
        tick();

        if (!pin_objs_.empty()) lv_group_focus_obj(pin_objs_[std::min<size_t>(pin_sel_, pin_objs_.size() - 1)]);
        set_home_actions();
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

    void add_row(const char* icon, uint32_t color, const std::string& text, const std::string& id,
                 std::function<void()> run, const std::string& value = "") {
        lv_obj_t* row = lv_obj_create(rows_box_);
        style_row(row);
        lv_obj_set_size(row, kRowW, kRowH);
        lv_obj_add_flag(row, LV_OBJ_FLAG_SCROLL_ON_FOCUS);
        lv_obj_t* ic = lv_obj_create(row);
        style_plain(ic);
        lv_obj_set_size(ic, 15, 15);
        lv_obj_set_style_radius(ic, 4, 0);
        lv_obj_set_style_bg_color(ic, c(color), 0);
        lv_obj_set_style_bg_opa(ic, LV_OPA_COVER, 0);
        lv_obj_remove_flag(ic, LV_OBJ_FLAG_CLICKABLE);
        lv_obj_center(label(ic, icon, &zs_font_12, 0xffffff));
        lv_obj_align(ic, LV_ALIGN_LEFT_MID, 4, 0);
        lv_obj_t* t = label(row, text.c_str(), &zs_font_14);
        lv_obj_remove_local_style_prop(t, LV_STYLE_TEXT_COLOR, 0);   // the row's, so focus turns it black
        lv_obj_set_size(t, kRowW - 26 - (value.empty() ? 4 : 26), 17);   // a long name ends in "..."
        lv_label_set_long_mode(t, LV_LABEL_LONG_DOT);
        lv_obj_align(t, LV_ALIGN_LEFT_MID, 26, 0);
        if (!value.empty()) {
            lv_obj_t* v = label(row, value.c_str(), &zs_font_12, kMuted);
            lv_obj_align(v, LV_ALIGN_RIGHT_MID, -6, 0);
        }
        row_runs_[row] = std::move(run);
        lv_obj_add_event_cb(
            row,
            [](lv_event_t* e) {
                auto* self = static_cast<Launcher*>(lv_event_get_user_data(e));
                auto it = self->row_runs_.find(lv_event_get_current_target_obj(e));
                if (it != self->row_runs_.end() && it->second) {
                    std::function<void()> fn = it->second;
                    fn();
                }
            },
            LV_EVENT_CLICKED, this);
        shell_add_focus(row);
        row_objs_.push_back(row);
        row_ids_.push_back(id);
    }

    // Up/Down between the pinned apps and the rows, Left/Right along the pins.
    bool home_nav(uint32_t key) {
        lv_obj_t* f = lv_group_get_focused(group);
        auto pin = std::find(pin_objs_.begin(), pin_objs_.end(), f);
        auto row = std::find(row_objs_.begin(), row_objs_.end(), f);
        if (pin != pin_objs_.end()) {
            int i = int(pin - pin_objs_.begin()), n = int(pin_objs_.size());
            switch (key) {
                case LV_KEY_LEFT: focus_pin((i + n - 1) % n); return true;
                case LV_KEY_RIGHT: focus_pin((i + 1) % n); return true;
                case LV_KEY_DOWN:
                    if (!row_objs_.empty()) lv_group_focus_obj(row_objs_.front());
                    return true;
                case LV_KEY_UP:
                    if (!row_objs_.empty()) lv_group_focus_obj(row_objs_.back());
                    return true;
                default: return false;
            }
        }
        if (row != row_objs_.end()) {
            int i = int(row - row_objs_.begin()), n = int(row_objs_.size());
            switch (key) {
                case LV_KEY_DOWN:
                    if (i + 1 < n) lv_group_focus_obj(row_objs_[size_t(i + 1)]);
                    else if (!pin_objs_.empty()) focus_pin(pin_sel_);
                    return true;
                case LV_KEY_UP:
                    if (i > 0) lv_group_focus_obj(row_objs_[size_t(i - 1)]);
                    else if (!pin_objs_.empty()) focus_pin(pin_sel_);
                    return true;
                case LV_KEY_LEFT:
                case LV_KEY_RIGHT: return true;
                default: return false;
            }
        }
        return false;
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
    lv_obj_t* rows_box_ = nullptr;
    lv_obj_t* time_ = nullptr;
    lv_obj_t* date_ = nullptr;
    lv_obj_t* playing_ = nullptr;
    std::string shown_playing_;
    int shown_minute_ = -1;
    lv_obj_t* all_head_ = nullptr;
    std::string query_;
    std::string pin_pending_;   // All apps: Tab, P, then 1 to 7
    bool all_apps_ = false;
    std::vector<std::string> pins_;
    std::vector<lv_obj_t*> pin_objs_, row_objs_;
    std::vector<std::string> row_ids_, all_ids_;
    std::map<lv_obj_t*, std::string> open_ids_;
    std::map<lv_obj_t*, std::function<void()>> row_runs_;
    int pin_sel_ = 0;
    ListView results_, all_;
};

}  // namespace

std::unique_ptr<App> make_launcher() { return std::make_unique<Launcher>(); }

void home_apps_changed() {
    if (g_home) g_home->apps_changed();
}

// An app was opened: it goes to the front of Recent.
void home_opened(const std::string& id) {
    if (id == "home" || id.empty()) return;
    std::vector<std::string> r = split(prefs::get_str("home.recent", ""));
    r.erase(std::remove(r.begin(), r.end(), id), r.end());
    r.insert(r.begin(), id);
    if (r.size() > 12) r.resize(12);
    prefs::set_str("home.recent", join(r));
}

}  // namespace zs
