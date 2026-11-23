#include "ui/shell.h"

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <ctime>

#include "hal/hal.h"
#include "platform/keys.h"
#include "platform/platform.h"
#include "ui/actions.h"
#include "ui/numkeys.h"
#include "ui/player.h"
#include "ui/power.h"
#include "ui/quick_panel.h"
#include "ui/remote.h"
#include "ui/sync.h"
#include "ui/theme.h"
#include "ui/tracker.h"

namespace zs {

using namespace theme;

namespace {
lv_indev_read_cb_t g_orig_read = nullptr;
uint32_t g_last_key = 0;
lv_indev_state_t g_last_state = LV_INDEV_STATE_RELEASED;
bool g_consumed = false;      // the held key belongs to the shell, not LVGL
bool g_swallowed = false;     // the held key woke the screen; nothing else sees it
bool g_repeat = false;        // ...and it's a key worth repeating while held
bool g_as_enter = false;      // a number key that picked an item: LVGL sees Enter
bool g_esc_held = false;      // Esc is down: held long enough, it goes home
uint32_t g_press_ms = 0, g_repeat_ms = 0, g_press_app = 0;
lv_obj_t* g_enter_box = nullptr;   // text box that had focus when Enter went down
constexpr uint32_t kRepeatDelay = 400, kRepeatEvery = 80;
constexpr uint32_t kHoldHomeMs = 600;   // hold Esc this long: home (docs/UX.md)
constexpr uint32_t kExternalQuitMs = 3000;   // an M5Stack app: hold Esc this long to stop it (as M5's launcher)

// Only movement and deleting repeat. Enter, Tab and letters are actions
// (install, record, save a waypoint): holding one must not do it 10 times.
bool repeats(uint32_t key) {
    return key == LV_KEY_UP || key == LV_KEY_DOWN || key == LV_KEY_LEFT || key == LV_KEY_RIGHT ||
           key == LV_KEY_BACKSPACE || key == LV_KEY_DEL || key == kKeyVolumeUp || key == kKeyVolumeDown ||
           key == kKeyBrightnessUp || key == kKeyBrightnessDown || key == kKeyPageUp || key == kKeyPageDown;
}

// Wraps the keypad driver: on each new press, offer the key to the shell first.
// Keys the shell consumes never reach LVGL, so the shell repeats them itself
// while held (the TCA8418 driver has no kernel auto-repeat). Keys passed on
// to widgets get LVGL's own long-press repeat.
void keypad_hook(lv_indev_t* indev, lv_indev_data_t* data) {
    g_orig_read(indev, data);
    bool pressed = data->state == LV_INDEV_STATE_PRESSED;
    bool new_press = pressed && (g_last_state == LV_INDEV_STATE_RELEASED || data->key != g_last_key);
    if (pressed) g_last_key = data->key;
    g_last_state = data->state;
    if (Shell::get().in_external()) Shell::get().external_key(data->key, pressed);
    if (new_press && power().on_key(lv_tick_get())) {
        g_swallowed = true;
    } else if (new_press) {
        g_press_ms = g_repeat_ms = lv_tick_get();
        g_press_app = Shell::get().app_generation();
        g_consumed = Shell::get().handle_key(data->key);
        g_as_enter = Shell::get().take_press_as_enter();
        g_repeat = g_consumed && repeats(Shell::get().last_key());
        // Esc (or ` standing in for it) did Back on the press; held on, it
        // goes all the way home.
        g_esc_held = Shell::get().last_key() == LV_KEY_ESC;
    } else if (pressed && g_esc_held && lv_tick_elaps(g_press_ms) > kHoldHomeMs) {
        g_esc_held = false;
        Shell::get().hold_home();
        // The rest of the press is the shell's: nothing on the home screen
        // sees an Esc release.
        g_consumed = true;
    } else if (pressed && g_repeat && lv_tick_elaps(g_press_ms) > kRepeatDelay &&
               lv_tick_elaps(g_repeat_ms) > kRepeatEvery) {
        // The press opened or closed an app: the rest of it isn't for the new
        // one (it stays consumed, so LVGL doesn't see it either).
        if (Shell::get().app_generation() != g_press_app) {
            g_repeat = false;
        } else {
            g_repeat_ms = lv_tick_get();
            Shell::get().handle_key(data->key);
            Shell::get().take_press_as_enter();
        }
    } else if (!pressed) {
        g_consumed = false;
        g_swallowed = false;
        g_repeat = false;
        g_esc_held = false;
        g_as_enter = false;
    }
    // 3..9 picked an item and focused it: the rest of the press is an Enter
    // on it, so it opens just as if Enter had been pressed there.
    if (g_as_enter && pressed) data->key = LV_KEY_ENTER;
    // Enter in a one-line text box fires READY on the press, and the app
    // usually closes the box then. LVGL sends the release (a click) to
    // whatever has focus by then: a list row behind the box, which would
    // connect, open SSH or accept a request nobody chose. If the focus moved
    // while Enter was down, drop the release.
    lv_group_t* g = lv_indev_get_group(indev);
    lv_obj_t* focused = g ? lv_group_get_focused(g) : nullptr;
    if (new_press && !g_consumed && !g_swallowed && data->key == LV_KEY_ENTER && focused &&
        lv_obj_check_type(focused, &lv_textarea_class)) {
        g_enter_box = focused;
    } else if (g_enter_box && (data->key != LV_KEY_ENTER || focused != g_enter_box)) {
        if (focused != g_enter_box) lv_indev_wait_release(indev);
        g_enter_box = nullptr;
    }
    if (!pressed) g_enter_box = nullptr;
    if (pressed && (g_consumed || g_swallowed)) data->state = LV_INDEV_STATE_RELEASED;  // LVGL never sees it
}

const char* battery_symbol(int pct) {
    if (pct >= 90) return LV_SYMBOL_BATTERY_FULL;
    if (pct >= 65) return LV_SYMBOL_BATTERY_3;
    if (pct >= 40) return LV_SYMBOL_BATTERY_2;
    if (pct >= 15) return LV_SYMBOL_BATTERY_1;
    return LV_SYMBOL_BATTERY_EMPTY;
}
}  // namespace

Shell& Shell::get() {
    static Shell s;
    return s;
}

void Shell::start(lv_indev_t* keypad) {
    keypad_ = keypad;
    if (keypad_) {
        g_orig_read = lv_indev_get_read_cb(keypad_);
        lv_indev_set_read_cb(keypad_, keypad_hook);
    }

    // Dark theme with Ubuntu orange for focus, sliders, cursors.
    lv_display_t* disp = lv_display_get_default();
    lv_theme_t* th = lv_theme_default_init(disp, c(kAccent), c(kAccent), true, &zs_font_12);
    lv_display_set_theme(disp, th);

    screen_ = lv_screen_active();
    lv_obj_set_style_bg_color(screen_, c(kBg), 0);
    lv_obj_set_style_bg_opa(screen_, LV_OPA_COVER, 0);
    lv_obj_set_style_text_color(screen_, c(kText), 0);
    lv_obj_remove_flag(screen_, LV_OBJ_FLAG_SCROLLABLE);

    build_statusbar();

    content_ = lv_obj_create(screen_);
    style_plain(content_);
    lv_obj_set_size(content_, kScreenW, kContentH);
    lv_obj_set_pos(content_, 0, kStatusH);
    lv_obj_remove_flag(content_, LV_OBJ_FLAG_SCROLLABLE);

    toast_ = lv_label_create(screen_);
    // Notifications drop from under the clock, as on Ubuntu.
    lv_obj_set_style_bg_color(toast_, c(kSurface2), 0);
    lv_obj_set_style_bg_opa(toast_, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(toast_, 11, 0);
    lv_obj_set_style_pad_hor(toast_, 10, 0);
    lv_obj_set_style_pad_ver(toast_, 4, 0);
    lv_obj_set_style_border_color(toast_, c(0x5a5a5a), 0);
    lv_obj_set_style_border_width(toast_, 1, 0);
    lv_obj_set_style_max_width(toast_, kScreenW - 16, 0);
    lv_obj_align(toast_, LV_ALIGN_TOP_MID, 0, kStatusH + 4);
    lv_obj_add_flag(toast_, LV_OBJ_FLAG_HIDDEN);

    quick_panel_build(screen_);
    actions_build(screen_);

    // Simulator stand-in for the backlight: a black layer over everything.
    blank_ = lv_obj_create(lv_layer_top());
    style_plain(blank_);
    lv_obj_set_size(blank_, kScreenW, kScreenH);
    lv_obj_set_style_bg_color(blank_, c(0x000000), 0);
    lv_obj_set_style_bg_opa(blank_, LV_OPA_COVER, 0);
    lv_obj_remove_flag(blank_, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_flag(blank_, LV_OBJ_FLAG_HIDDEN);

    power().start();
    go_home();
    apply_pending();
    tick_timer_ = lv_timer_create(tick_cb, 100, this);
    update_statusbar();
}

// Ubuntu's top bar: what's open on the left, date and time in the middle,
// system status on the right.
void Shell::build_statusbar() {
    lv_obj_t* bar = lv_obj_create(screen_);
    style_plain(bar);
    lv_obj_set_size(bar, kScreenW, kStatusH);
    lv_obj_set_pos(bar, 0, 0);
    lv_obj_set_style_bg_color(bar, c(kTopBar), 0);
    lv_obj_set_style_bg_opa(bar, LV_OPA_COVER, 0);
    lv_obj_set_style_pad_hor(bar, 7, 0);
    lv_obj_remove_flag(bar, LV_OBJ_FLAG_SCROLLABLE);

    sb_title_ = label(bar, "", &zs_font_12, kText);
    lv_obj_align(sb_title_, LV_ALIGN_LEFT_MID, 0, 0);

    // Home has no title: show GNOME's workspace pill instead.
    sb_home_ = lv_obj_create(bar);
    style_plain(sb_home_);
    lv_obj_set_size(sb_home_, 26, 6);
    lv_obj_align(sb_home_, LV_ALIGN_LEFT_MID, 0, 0);
    lv_obj_remove_flag(sb_home_, LV_OBJ_FLAG_SCROLLABLE);
    for (int i = 0; i < 2; ++i) {
        lv_obj_t* d = lv_obj_create(sb_home_);
        style_plain(d);
        lv_obj_set_size(d, i == 0 ? 16 : 6, 6);
        lv_obj_set_pos(d, i == 0 ? 0 : 20, 0);
        lv_obj_set_style_radius(d, 3, 0);
        lv_obj_set_style_bg_color(d, c(i == 0 ? kText : 0x6e6e6e), 0);
        lv_obj_set_style_bg_opa(d, LV_OPA_COVER, 0);
    }

    sb_time_ = label(bar, "--:--", &zs_font_12, kText);
    lv_obj_align(sb_time_, LV_ALIGN_CENTER, 0, 0);

    sb_icons_ = label(bar, "", &zs_font_12, kText);
    lv_obj_align(sb_icons_, LV_ALIGN_RIGHT_MID, 0, 0);

    // Ctrl tapped and waiting for its key, as the keyboard's LEDs show Shift,
    // Fn and Sym. Placed when shown, just right of the title.
    sb_ctrl_ = lv_obj_create(bar);
    style_plain(sb_ctrl_);
    lv_obj_set_size(sb_ctrl_, LV_SIZE_CONTENT, 14);
    lv_obj_set_style_pad_hor(sb_ctrl_, 4, 0);
    lv_obj_set_style_radius(sb_ctrl_, 3, 0);
    lv_obj_set_style_bg_color(sb_ctrl_, c(kAccent), 0);
    lv_obj_set_style_bg_opa(sb_ctrl_, LV_OPA_COVER, 0);
    lv_obj_remove_flag(sb_ctrl_, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_center(label(sb_ctrl_, "Ctrl", &zs_font_12, kOnAccent));
    lv_obj_add_flag(sb_ctrl_, LV_OBJ_FLAG_HIDDEN);
}

void Shell::update_statusbar() {
    char buf[96];
    auto& h = hal();
    BatteryInfo b = h.battery();
    NetStatus n = h.net();
    std::string icons;
    if (h.muted()) icons += LV_SYMBOL_MUTE "  ";
    if (h.record_state().recording) icons += LV_SYMBOL_STOP " REC  ";
    else if (h.playback().playing) icons += LV_SYMBOL_AUDIO "  ";
    if (track_state().recording) icons += LV_SYMBOL_GPS "  ";
    if (n.usb_gadget) icons += LV_SYMBOL_USB "  ";
    if (!n.tether.empty()) icons += ZS_ICON_PHONE "  ";
    if (!n.tailscale_ip.empty()) icons += "TS  ";
    if (sync_running_cached()) icons += LV_SYMBOL_LOOP "  ";
    if (n.bt_enabled) icons += LV_SYMBOL_BLUETOOTH "  ";
    if (n.wifi_connected) icons += LV_SYMBOL_WIFI "  ";
    if (b.charging) icons += LV_SYMBOL_CHARGE " ";
    icons += battery_symbol(b.percent < 0 ? 0 : b.percent);
    if (b.percent >= 0) {
        snprintf(buf, sizeof buf, " %d%%", b.percent);
        icons += buf;
    }
    int left = power().minutes_left();
    if (power().show_time_left() && !b.external_power && left >= 0) {
        snprintf(buf, sizeof buf, " %d:%02d", left / 60, left % 60);
        icons += buf;
    }
    lv_label_set_text(sb_icons_, icons.c_str());
    lv_obj_set_style_text_color(sb_icons_, c(b.percent >= 0 && b.percent < 15 && !b.charging ? kBad : kText), 0);

    bool home = app_id_ == "home";
    if (home) lv_obj_remove_flag(sb_home_, LV_OBJ_FLAG_HIDDEN);
    else lv_obj_add_flag(sb_home_, LV_OBJ_FLAG_HIDDEN);

    // The clock gives way as the bar fills: first it drops the date, then it
    // moves next to the status icons, and last it takes the title's place.
    time_t now = time(nullptr);
    tm lt{};
    localtime_r(&now, &lt);
    lv_obj_update_layout(sb_icons_);
    lv_obj_update_layout(sb_title_);
    const int gap = 8;
    int left_end = home ? 26 : lv_obj_get_width(sb_title_);
    int right_start = lv_obj_get_x(sb_icons_);
    auto fits = [&](const char* f) {
        strftime(buf, sizeof buf, f, &lt);
        lv_label_set_text(sb_time_, buf);
        lv_obj_align(sb_time_, LV_ALIGN_CENTER, 0, 0);
        lv_obj_update_layout(sb_time_);
        int x = lv_obj_get_x(sb_time_);
        return x >= left_end + gap && x + lv_obj_get_width(sb_time_) + gap <= right_start;
    };
    bool title = true;
    if (!fits("%b %-d  %H:%M") && !fits("%H:%M")) {
        int x = right_start - gap - lv_obj_get_width(sb_time_);
        title = x >= left_end + gap;
        lv_obj_align(sb_time_, LV_ALIGN_LEFT_MID, title ? x : 0, 0);
    }
    if (title || home) lv_obj_remove_flag(sb_title_, LV_OBJ_FLAG_HIDDEN);
    else lv_obj_add_flag(sb_title_, LV_OBJ_FLAG_HIDDEN);
    if (!title && home) lv_obj_add_flag(sb_home_, LV_OBJ_FLAG_HIDDEN);
}

void Shell::tick_cb(lv_timer_t* t) {
    auto* s = static_cast<Shell*>(lv_timer_get_user_data(t));
    hal().poll();
    power().tick(lv_tick_get());
    if (platform_ctrl_pending() == lv_obj_has_flag(s->sb_ctrl_, LV_OBJ_FLAG_HIDDEN)) {
        if (platform_ctrl_pending()) {
            // After the app's title (home: its workspace pill), whose width varies.
            bool home = !lv_obj_has_flag(s->sb_home_, LV_OBJ_FLAG_HIDDEN);
            lv_obj_align_to(s->sb_ctrl_, home ? s->sb_home_ : s->sb_title_, LV_ALIGN_OUT_RIGHT_MID, 6, 0);
            lv_obj_remove_flag(s->sb_ctrl_, LV_OBJ_FLAG_HIDDEN);
        }
        else lv_obj_add_flag(s->sb_ctrl_, LV_OBJ_FLAG_HIDDEN);
    }
    remote_tick(lv_tick_get());
    sync_tick(lv_tick_get());
    player_tick();
    track_tick();
    // New mesh message while you're elsewhere: show it.
    static int mesh_seen = 0, mesh_n = 0;
    if (++mesh_n % 5 == 0) {
        int seq = hal().mesh_seq();
        if (seq > mesh_seen) {
            auto msgs = hal().mesh_messages();
            if (s->current_app() != "mesh" && !msgs.empty() && !msgs.back().mine) {
                const MeshMessage& m = msgs.back();
                s->toast(LV_SYMBOL_ENVELOPE " " + m.from + ": " + m.text.substr(0, 60));
                hal().beep(1320, 60);
            }
        }
        mesh_seen = seq;
    }
    static int n = 0;
    // Esc held down in an M5Stack app (keys come only as they change while
    // it has the screen, so this is checked here, not in the keypad hook).
    if (s->in_external() && s->external_esc_since_ &&
        lv_tick_elaps(s->external_esc_since_) > kExternalQuitMs) {
        s->external_esc_since_ = 0;
        std::fprintf(stderr, "zeroshell: Esc held, stopping %s\n", s->app_id_.c_str());
        hal().external_close();
    }
    // Terminal closed (exit, or tmux detach with Ctrl+Q), camera app or
    // M5Stack app closed: take the screen back.
    if (s->handoff_ && lv_tick_elaps(s->handoff_since_) > 1500) {
        // Nobody is looking: don't leave the sensor running with the screen off.
        if (s->handoff_camera_ && power().screen_off()) hal().camera_close();
        bool active = s->handoff_camera_     ? hal().camera_active()
                      : s->handoff_external_ ? hal().external_active()
                                             : hal().terminal_active();
        if (!active) {
            if (s->handoff_external_) {
                // Its page in ZeroShell only says "running": go home.
                s->go_home();
            } else if (!s->handoff_camera_) {
                platform_console_handoff(false);
                // The Terminal app has nothing to show on the device: go home.
                if (s->app_id_ == "terminal") s->go_home();
            }
            s->handoff_ = s->handoff_camera_ = s->handoff_external_ = false;
            s->external_esc_since_ = 0;
            power().set_handoff(false);
            s->screen_changed();
        }
    }
    // Screen off or terminal: the timer runs every 500 ms and nothing is drawn.
    if (!power().screen_off() && !s->handoff_ && ++n % 10 == 0) s->update_statusbar();
    if (s->app_) s->app_->tick();
    number_hints_tick();
    if (n % 10 == 0) quick_panel_refresh();
    s->apply_pending();
    if (s->toast_shown_ && lv_tick_elaps(s->toast_since_) > 2000) {   // wrap-safe
        lv_obj_add_flag(s->toast_, LV_OBJ_FLAG_HIDDEN);
        s->toast_shown_ = false;
    }
}

void Shell::switch_to(std::unique_ptr<App> app, const std::string& title) {
    ++app_generation_;
    fn_layer_ = false;
    number_hints_hide();
    quick_panel_close();
    actions_confirm_close();
    actions_clear();
    app_root_ = nullptr;
    // Destroy the old app while its widgets still exist: destructors read
    // them (Notes saves its text), then the widgets and group go away.
    app_.reset();
    if (group_) {
        if (keypad_) lv_indev_set_group(keypad_, nullptr);
        lv_group_delete(group_);
        group_ = nullptr;
    }
    lv_obj_clean(content_);
    app_ = std::move(app);

    group_ = lv_group_create();
    lv_group_set_wrap(group_, true);
    app_->group = group_;
    if (keypad_) lv_indev_set_group(keypad_, group_);

    lv_obj_t* root = lv_obj_create(content_);
    style_plain(root);
    lv_obj_set_size(root, kScreenW, kContentH);
    lv_obj_remove_flag(root, LV_OBJ_FLAG_SCROLLABLE);
    app_root_ = root;
    app_->build(root);
    fit_app_root();

    lv_label_set_text(sb_title_, title.c_str());
    update_statusbar();
}

void Shell::open_app(const std::string& id) {
    // Deferred: apps often call this from their own on_key(), and switching
    // destroys the calling app. apply_pending() runs once the call returns.
    pending_ = id;
    app_arg_.clear();
    remember_back(id);
}

void Shell::open_app(const std::string& id, const std::string& arg) {
    pending_ = id;
    app_arg_ = arg;
    remember_back(id);
}

// One app opening another (Settings > Wi-Fi, Files > Notes): Esc at the new
// one's first screen comes back here, not home. Home itself isn't kept: it's
// where an empty stack goes anyway.
void Shell::remember_back(const std::string& id) {
    if (app_id_.empty() || app_id_ == "home" || app_id_ == id) return;
    if (!back_.empty() && back_.back().first == app_id_) return;
    // With what it was opened with, so it comes back the same (Settings at
    // its Bluetooth section, not its first page).
    back_.push_back({app_id_, app_open_arg_});
    if (back_.size() > 8) back_.erase(back_.begin());
}

void Shell::go_back() {
    if (back_.empty()) {
        go_home();
        return;
    }
    pending_ = back_.back().first;
    app_arg_ = back_.back().second;
    back_.pop_back();
}

std::string Shell::take_app_arg() {
    std::string a;
    a.swap(app_arg_);
    return a;
}

void Shell::go_home() {
    pending_ = "home";
    back_.clear();
}

void Shell::set_actions(std::vector<Action> actions) {
    actions_set(std::move(actions));
    fit_app_root();
}

// A screen with actions has the soft-key bar under it: kBodyH, not kContentH.
void Shell::fit_app_root() {
    if (app_root_) lv_obj_set_height(app_root_, actions_bar_shown() ? kBodyH : kContentH);
}

void Shell::hold_home() {
    if (handoff_) return;   // the terminal or camera has the screen
    if (quick_panel_is_open()) quick_panel_close();
    actions_menu_close();
    actions_sheet_close();
    actions_confirm_close();
    number_hints_hide();
    if (app_id_ != "home") go_home();
    apply_pending();
}

void Shell::apply_pending() {
    if (pending_.empty()) return;
    std::string id;
    id.swap(pending_);
    // In the journal (journalctl -u zeroshell): which app was open when
    // something went wrong. The device tests read it too.
    std::fprintf(stderr, "zeroshell: open %s\n", id.c_str());
    if (id == "home") {
        app_id_ = "home";
        switch_to(make_launcher(), "");
        return;
    }
    if (app_find(id)) home_opened(id);
    if (const AppInfo* info = app_find(id)) {
        app_id_ = id;
        // Copy what switch_to needs: an app may reload the registry.
        auto make = info->make;
        std::string name = info->name;
        app_open_arg_ = app_arg_;
        switch_to(make(), name);
        app_arg_.clear();   // build() had its chance to read it
        return;
    }
    app_arg_.clear();
    // Installable apps (docs/APPS.md) that aren't on this device yet.
    toast("Not installed: get it from Apps");
}

void Shell::toast(const std::string& msg) {
    lv_label_set_text(toast_, msg.c_str());
    lv_obj_remove_flag(toast_, LV_OBJ_FLAG_HIDDEN);
    lv_obj_move_foreground(toast_);
    toast_since_ = lv_tick_get();
    toast_shown_ = true;
}

void Shell::prepare_power_off() {
    // Close the open app (Notes saves) and finish the GPS track, so a flat
    // battery loses nothing that was on screen. Going home destroys the app
    // properly (with its widgets), so nothing dangles if power-off fails.
    if (app_id_ != "home") {
        app_id_ = "home";
        switch_to(make_launcher(), "");
    }
    track_stop();
}

void Shell::add_focus(lv_obj_t* obj) {
    if (group_) lv_group_add_obj(group_, obj);
}

bool Shell::handle_key(uint32_t key) {
    bool used = false;
    press_as_enter_ = false;
    lv_obj_t* focused = group_ ? lv_group_get_focused(group_) : nullptr;
    bool in_text = focused && lv_obj_check_type(focused, &lv_textarea_class);
    bool panel = quick_panel_is_open();
    // Esc (top-left) is Back. ` doubles as Back for keyboards without an
    // Esc key. In a text box it types a `, unless the box is empty: then it's
    // Back too, so there's always a way out of a password or search box.
    if (key == '`' && (panel || !in_text || lv_textarea_get_text(focused)[0] == '\0')) key = LV_KEY_ESC;
    // Screens that need no typing (the home screen, the taskbar panel) get
    // the Fn layer without Fn held: f/z/x/c are arrows, a/s/d volume, and so
    // on (keys.h). Shift + a letter is still the letter.
    if ((panel || (fn_layer_ && !in_text)) && key < 128 && std::islower(int(key))) {
        key = fn_layer_key(key);
        if (!key) return true;   // nothing on the Fn layer there
    }
    last_key_ = key;
    // Whatever was asked for, the number badges have done their job (0
    // itself shows or hides them).
    if (key != '0') number_hints_hide();
    if (key == ctrl_key(' ')) {
        // Ctrl + Space: the taskbar panel, from anywhere, text box or not.
        actions_menu_close();
        if (panel) quick_panel_close();
        else quick_panel_open();
        used = true;
    } else if (actions_confirm_is_open() && actions_confirm_key(key)) {
        used = true;   // the confirm box: Enter or Esc, nothing else
    } else if (actions_sheet_is_open()) {
        actions_sheet_close();   // the key sheet: any key closes it
        used = true;
    } else if (actions_menu_is_open() && actions_menu_key(key)) {
        used = true;   // the Tab menu takes every key but the shell's own
    } else if (panel && quick_panel_key(key)) {
        used = true;
    } else if (!panel && app_ && app_->on_key(key)) {
        used = true;
    } else if (!panel && key == kKeyNext && actions_have_menu()) {
        // Tab: the screen's menu (docs/UX.md). Screens without one keep Tab.
        actions_menu_open(focused, lv_label_get_text(sb_title_));
        used = true;
    } else if (is_ctrl_key(key)) {
        // Ctrl + a letter: that menu action. Never to a widget either (a
        // text box would type it).
        if (!panel) actions_run_letter(char(key - kKeyCtrl));
        used = true;
    } else if (is_shell_key(key)) {
        system_key(key);
        used = true;   // never to a widget: a text box would type it
    } else if (!panel && !in_text && key >= '0' && key <= '9') {
        used = number_key(key);
    } else if ((key == LV_KEY_UP || key == LV_KEY_DOWN) && group_) {
        // Up/Down move focus between widgets, but never out of a text box:
        // a pop-up box would stay on screen with typing going to the list
        // hidden behind it. (Multi-line boxes move their cursor instead.)
        if (!in_text) {
            if (key == LV_KEY_UP) lv_group_focus_prev(group_);
            else lv_group_focus_next(group_);
            used = true;
        }
    } else if (key == LV_KEY_ESC) {
        if (app_ && app_->on_back()) {
            used = true;
        } else if (app_id_ != "home") {
            go_back();   // to the app that opened this one, else home
            used = true;
        }
    }
    apply_pending();
    return used;
}

void Shell::system_key(uint32_t key) {
    char buf[32];
    bool panel = quick_panel_is_open();
    switch (key) {
        case kKeyVolumeUp:
        case kKeyVolumeDown:
            std::snprintf(buf, sizeof buf, "Volume %d%%", step_volume(key == kKeyVolumeUp ? +1 : -1));
            if (!panel) toast(buf);
            break;
        case kKeyMute:
            hal().set_muted(!hal().muted());
            if (!panel) toast(hal().muted() ? "Sound off" : "Sound on");
            break;
        case kKeyBrightnessUp:
        case kKeyBrightnessDown:
            std::snprintf(buf, sizeof buf, "Brightness %d%%", step_brightness(key == kKeyBrightnessUp ? +1 : -1));
            if (!panel) toast(buf);
            break;
        case kKeyPlayPause: player_media_key(MediaKey::PlayPause); break;
        case kKeyNextTrack: player_media_key(MediaKey::Next); break;
        case kKeyPrevTrack: player_media_key(MediaKey::Previous); break;
        case kKeyPageUp:
        case kKeyPageDown: {
            // Five items at a time, stopping at the ends.
            if (!group_ || panel) break;
            int n = int(lv_group_get_obj_count(group_));
            lv_obj_t* f = lv_group_get_focused(group_);
            int at = 0;
            while (at < n && lv_group_get_obj_by_index(group_, uint32_t(at)) != f) ++at;
            if (at == n) at = 0;
            int to = std::clamp(at + (key == kKeyPageDown ? 5 : -5), 0, n - 1);
            if (n > 0) lv_group_focus_obj(lv_group_get_obj_by_index(group_, uint32_t(to)));
            break;
        }
        default: break;   // F1..F12: nothing yet
    }
    quick_panel_refresh();
    update_statusbar();
}

bool Shell::number_key(uint32_t key) {
    switch (key) {
        case '0':
            // The key sheet where the screen has actions; elsewhere, badges on
            // what 1 to 9 pick (lists show theirs all the time).
            if (actions_bar_shown()) actions_sheet_open(lv_label_get_text(sb_title_));
            else if (number_hints_shown()) number_hints_hide();
            else if (number_hints_show(number_targets(group_)) == 0) toast("Nothing to pick here");
            return true;
        default: {
            auto targets = number_targets(group_);
            size_t i = key - '1';
            if (i >= targets.size()) return true;
            lv_group_focus_obj(targets[i]);
            lv_obj_scroll_to_view(targets[i], LV_ANIM_OFF);
            // Not consumed: the keypad hook passes the press on as Enter.
            press_as_enter_ = true;
            return false;
        }
    }
}

bool Shell::open_terminal(const std::string& command) {
    platform_console_handoff(true);
    if (!hal().terminal_open(command)) {
        platform_console_handoff(false);
        return false;
    }
    handoff_ = true;
    handoff_since_ = lv_tick_get();
    power().set_handoff(true);
    screen_changed();
    return true;
}

bool Shell::open_camera() {
    if (!hal().camera_open()) return false;
    handoff_ = handoff_camera_ = true;
    handoff_since_ = lv_tick_get();
    power().set_handoff(true);
    screen_changed();
    return true;
}

bool Shell::open_external(const std::string& command) {
    if (!hal().external_open(command)) return false;
    handoff_ = handoff_external_ = true;
    external_esc_since_ = 0;
    handoff_since_ = lv_tick_get();
    power().set_handoff(true);
    screen_changed();
    return true;
}

void Shell::external_key(uint32_t key, bool pressed) {
    if (pressed && (key == LV_KEY_ESC || key == '`')) {
        if (!external_esc_since_) external_esc_since_ = lv_tick_get() | 1;   // 0 means "not held"
    } else {
        external_esc_since_ = 0;
    }
}

void Shell::screen_changed() {
    lv_display_t* disp = lv_display_get_default();
    bool off = power().screen_off();
    bool idle = off || handoff_;   // nothing of ours on screen
    platform_set_low_power(idle);
    if (!idle) {
        lv_display_enable_invalidation(disp, true);
        lv_timer_set_period(tick_timer_, 100);
    }
    if (!hal().has_backlight()) {
        // Draw what the backlight would do: dark when off, shaded when dim.
        if (off || power().dimmed()) {
            lv_obj_set_style_bg_opa(blank_, off ? LV_OPA_COVER : LV_OPA_50, 0);
            lv_obj_remove_flag(blank_, LV_OBJ_FLAG_HIDDEN);
        } else {
            lv_obj_add_flag(blank_, LV_OBJ_FLAG_HIDDEN);
        }
    }
    if (idle) {
        // Draw the last frame (not over the console), then stop drawing.
        if (!handoff_) lv_refr_now(disp);
        lv_display_enable_invalidation(disp, false);
        lv_timer_set_period(tick_timer_, 500);
    } else {
        update_statusbar();
        lv_obj_invalidate(screen_);
    }
}

// Service functions declared in app.h
void shell_toast(const std::string& msg) { Shell::get().toast(msg); }
void shell_open_app(const std::string& id) { Shell::get().open_app(id); }
void shell_open_app(const std::string& id, const std::string& arg) { Shell::get().open_app(id, arg); }
std::string shell_app_arg() { return Shell::get().take_app_arg(); }
void shell_go_home() { Shell::get().go_home(); }
void shell_add_focus(lv_obj_t* obj) { Shell::get().add_focus(obj); }
void shell_screen_changed() { Shell::get().screen_changed(); }
void shell_prepare_power_off() { Shell::get().prepare_power_off(); }
bool shell_open_terminal(const std::string& command) { return Shell::get().open_terminal(command); }
bool shell_open_camera() { return Shell::get().open_camera(); }
bool shell_open_external(const std::string& command) { return Shell::get().open_external(command); }
void shell_set_fn_layer(bool on) { Shell::get().set_fn_layer(on); }
void shell_set_actions(std::vector<Action> actions) { Shell::get().set_actions(std::move(actions)); }
void shell_confirm(const std::string& question, const std::string& detail, const std::string& yes,
                   std::function<void()> on_yes) {
    actions_confirm(question, detail, yes, std::move(on_yes));
}

}  // namespace zs
