// The welcome tour (ui/welcome.h). Nine short pages, each one idea, drawn
// with keycaps like the ones on the device so the keys are easy to find.
// The soft-key bar and the Tab menu are the shell's own, so the tour uses the
// very things it explains.
#include "ui/welcome.h"

#include <cstdlib>
#include <string>
#include <vector>

#include "hal/hal.h"
#include "platform/keys.h"
#include "ui/power.h"
#include "ui/prefs.h"
#include "ui/shell.h"
#include "ui/theme.h"

namespace zs {

using namespace theme;

namespace {

enum Page { kHello, kArrows, kMoving, kHome, kMenus, kPanel, kTyping, kLock, kDone, kPages };

constexpr int kPad = 12;               // left and right margin
constexpr int kTop = 38;               // where a page's body starts, under its title
constexpr int kKeysW = 92;             // a row's keys, then what they do
constexpr int kCapH = 16;

// The text on a keycap is a child label; a lit cap turns it black.
void cap_lit(lv_obj_t* cap, bool on) {
    lv_obj_set_style_bg_opa(cap, on ? LV_OPA_COVER : LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_color(cap, c(on ? kFocus : kGrey5), 0);
    for (uint32_t i = 0; i < lv_obj_get_child_count(cap); ++i)
        lv_obj_set_style_text_color(lv_obj_get_child(cap, int32_t(i)), c(on ? kOnFocus : kText), 0);
}

// A keycap: outlined, as in the soft-key bar, but a size bigger.
lv_obj_t* cap(lv_obj_t* parent, const char* text) {
    lv_obj_t* k = lv_obj_create(parent);
    style_plain(k);
    lv_obj_set_size(k, LV_SIZE_CONTENT, kCapH);
    lv_obj_set_style_min_width(k, kCapH, 0);
    lv_obj_set_style_pad_hor(k, 4, 0);
    lv_obj_set_style_radius(k, 2, 0);
    lv_obj_set_style_border_width(k, 1, 0);
    lv_obj_set_style_bg_color(k, c(kFocus), 0);
    lv_obj_set_style_bg_grad_color(k, c(kFocusLow), 0);
    lv_obj_set_style_bg_grad_dir(k, LV_GRAD_DIR_VER, 0);
    lv_obj_remove_flag(k, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_center(label(k, text, &zs_font_12, kText));
    cap_lit(k, false);
    return k;
}

// One row: keys on the left, what they do on the right. In `keys`, words in
// a "[...]" are caps and the rest ("+", "/", "held") is plain text between them.
void row(lv_obj_t* parent, const char* keys, const char* text) {
    lv_obj_t* r = lv_obj_create(parent);
    style_plain(r);
    lv_obj_set_size(r, kScreenW - 2 * kPad, LV_SIZE_CONTENT);
    lv_obj_remove_flag(r, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t* k = lv_obj_create(r);
    style_plain(k);
    lv_obj_set_size(k, kKeysW, LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(k, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(k, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_column(k, 3, 0);
    lv_obj_remove_flag(k, LV_OBJ_FLAG_SCROLLABLE);
    std::string s = keys;
    for (size_t i = 0; i < s.size();) {
        if (s[i] == ' ') {
            ++i;
        } else if (s[i] == '[') {
            size_t end = s.find(']', i);
            if (end == std::string::npos) end = s.size();
            cap(k, s.substr(i + 1, end - i - 1).c_str());
            i = end + 1;
        } else {
            size_t end = s.find_first_of(" [", i);
            if (end == std::string::npos) end = s.size();
            label(k, s.substr(i, end - i).c_str(), &zs_font_12, kMuted);
            i = end;
        }
    }

    lv_obj_t* t = label(r, text, &zs_font_12, kText);
    lv_obj_set_width(t, kScreenW - 2 * kPad - kKeysW - 4);
    lv_label_set_long_mode(t, LV_LABEL_LONG_WRAP);
    lv_obj_set_pos(t, kKeysW + 4, 1);
}

// Rows, one under another, from the top of the body.
lv_obj_t* rows(lv_obj_t* root) {
    lv_obj_t* box = lv_obj_create(root);
    style_plain(box);
    lv_obj_set_size(box, kScreenW - 2 * kPad, kBodyH - kTop);
    lv_obj_set_pos(box, kPad, kTop);
    lv_obj_set_flex_flow(box, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_row(box, 6, 0);
    lv_obj_remove_flag(box, LV_OBJ_FLAG_SCROLLABLE);
    return box;
}

// A line of plain text under the rows.
lv_obj_t* note(lv_obj_t* parent, const char* text, uint32_t color = kMuted) {
    lv_obj_t* l = label(parent, text, &zs_font_12, color);
    lv_obj_set_width(l, kScreenW - 2 * kPad);
    lv_label_set_long_mode(l, LV_LABEL_LONG_WRAP);
    return l;
}

class Welcome : public App {
public:
    ~Welcome() override {
        // Seen, finished or skipped: it doesn't open by itself again.
        prefs::set_int("welcome.done", 1);
    }

    void build(lv_obj_t* root) override {
        root_ = root;
        lv_obj_set_style_bg_color(root, c(kBg), 0);
        lv_obj_set_style_bg_opa(root, LV_OPA_COVER, 0);
        // Back from Wi-Fi or Date & time, opened from the last page: that page.
        show(shell_app_arg() == "done" ? kDone : kHello);
    }

    void tick() override {
        if (page_ != kDone || !wifi_) return;
        if (++ticks_ % 10 == 0) wifi_status();
    }

    bool on_key(uint32_t key) override {
        if (page_ == kArrows) {
            int i = key == LV_KEY_UP ? 0 : key == LV_KEY_LEFT ? 1 : key == LV_KEY_DOWN ? 2 : key == LV_KEY_RIGHT ? 3 : -1;
            if (i >= 0) {
                arrow(i);
                return true;
            }
        }
        if (page_ == kDone && key >= '1' && key <= '3') {
            open_setup(int(key - '1'));
            return true;
        }
        if (key == LV_KEY_ENTER || key == LV_KEY_RIGHT || key == ' ') {
            next();
            return true;
        }
        if (key == LV_KEY_LEFT) {
            if (page_ > kHello) show(page_ - 1);
            return true;
        }
        // Nothing here moves: Up and Down do nothing (not the taskbar either).
        return key == LV_KEY_UP || key == LV_KEY_DOWN;
    }

    // Esc goes back a page, as everywhere; on the first page it skips the tour.
    bool on_back() override {
        if (page_ == kHello) skip();
        else show(page_ - 1);
        return true;
    }

private:
    void next() {
        if (page_ == kDone) shell_go_home();
        else show(page_ + 1);
    }

    void skip() {
        shell_confirm("Skip the tour?", "To see it later, type tour on the home screen.", "Skip",
                      [] { shell_go_home(); });
    }

    void open_setup(int i) {
        // Esc in what opens comes back to this page (Shell::remember_back).
        Shell::get().set_open_arg("done");
        if (i == 0) shell_open_app("settings", "wifi");
        else if (i == 1) shell_open_app("settings", "time");
        else shell_open_app("apps");
    }

    // ---- Pages ---------------------------------------------------------------

    void show(int page) {
        page_ = page;
        wifi_ = nullptr;
        for (auto& a : arrows_) a = nullptr;
        lv_obj_clean(root_);
        switch (page_) {
            case kHello: hello(); break;
            case kArrows: arrows(); break;
            case kMoving: moving(); break;
            case kHome: home(); break;
            case kMenus: menus(); break;
            case kPanel: panel(); break;
            case kTyping: typing(); break;
            case kLock: lock(); break;
            default: done(); break;
        }
        if (page_ != kHello) dots();
        set_actions();
    }

    void set_actions() {
        std::vector<Action> a;
        if (page_ == kHello) {
            a.push_back({"Start", 'n', "Enter", [this] { next(); }});
            a.push_back({"Skip tour", 's', "Esc", [this] { skip(); }});
            shell_set_actions(std::move(a));
            return;
        }
        if (page_ == kDone) {
            a.push_back({"Home", 'h', "Enter", [] { shell_go_home(); }});
            a.push_back({"Set up", 0, "1-3", nullptr});
            a.push_back({"Wi-Fi", 'w', nullptr, [this] { open_setup(0); }});
            a.push_back({"Date & time", 't', nullptr, [this] { open_setup(1); }});
            a.push_back({"Get apps", 'a', nullptr, [this] { open_setup(2); }});
        } else {
            a.push_back({"Next", 'n', "Enter", [this] { next(); }});
        }
        a.push_back({"Back", 'b', "Esc", [this] { show(page_ - 1); }});
        // On the last page there is nothing left to skip.
        if (page_ != kDone) a.push_back({"Skip tour", 's', nullptr, [this] { skip(); }});
        shell_set_actions(std::move(a));
    }

    lv_obj_t* title(const char* text) {
        lv_obj_t* t = label(root_, text, &zs_font_20, kText);
        lv_obj_set_pos(t, kPad, 8);
        return t;
    }

    // Where you are in the tour: a dot a page, top right.
    void dots() {
        constexpr int kDot = 5, kGap = 4;
        int n = kPages - 1;   // the welcome page has no dot
        int x = kScreenW - kPad - n * kDot - (n - 1) * kGap - 6;
        for (int i = 1; i < kPages; ++i) {
            lv_obj_t* d = lv_obj_create(root_);
            style_plain(d);
            bool here = i == page_;
            lv_obj_set_size(d, here ? kDot + 6 : kDot, kDot);
            lv_obj_set_pos(d, x, 16);
            x += (here ? kDot + 6 : kDot) + kGap;
            lv_obj_set_style_radius(d, LV_RADIUS_CIRCLE, 0);
            lv_obj_set_style_bg_color(d, c(here ? kFocus : i < page_ ? kGrey6 : kGrey4), 0);
            lv_obj_set_style_bg_opa(d, LV_OPA_COVER, 0);
        }
    }

    void hello() {
        lv_obj_t* name = label(root_, "ZeroOS", &zs_font_28, kText);
        lv_obj_align(name, LV_ALIGN_TOP_MID, 0, 18);
        lv_obj_t* sub = label(root_, "Welcome to your Cardputer Zero", &zs_font_14, kText);
        lv_obj_align(sub, LV_ALIGN_TOP_MID, 0, 56);
        lv_obj_t* how = label(root_, "Everything works from the keyboard.\nHere's how, in about a minute.", &zs_font_12,
                              kMuted);
        lv_obj_set_style_text_align(how, LV_TEXT_ALIGN_CENTER, 0);
        lv_obj_align(how, LV_ALIGN_TOP_MID, 0, 82);
    }

    // Practice: the arrows are on the Fn layer. Each one lights as it's pressed.
    void arrows() {
        title("The arrow keys");
        lv_obj_t* t = note(root_, "Hold Fn and press F, Z, X or C.\nOr tap Fn first, then the letter.", kText);
        lv_obj_set_width(t, 170);
        lv_obj_set_pos(t, kPad, kTop);
        status_ = note(root_, "Try all four now.");
        lv_obj_set_width(status_, 170);
        lv_obj_set_pos(status_, kPad, kTop + 44);
        // F above X, Z X C in a row: as they sit on the keyboard.
        const char* letters[] = {"F", "Z", "X", "C"};
        const char* symbols[] = {LV_SYMBOL_UP, LV_SYMBOL_LEFT, LV_SYMBOL_DOWN, LV_SYMBOL_RIGHT};
        const int col[] = {1, 0, 1, 2}, line[] = {0, 1, 1, 1};
        constexpr int kW = 34, kH = 30, kGap = 4;
        int x0 = kScreenW - kPad - 3 * kW - 2 * kGap - 8, y0 = kTop - 2;
        for (int i = 0; i < 4; ++i) {
            lv_obj_t* k = cap(root_, letters[i]);
            lv_obj_set_size(k, kW, kH);
            lv_obj_set_pos(k, x0 + col[i] * (kW + kGap), y0 + line[i] * (kH + kGap));
            lv_obj_t* l = lv_obj_get_child(k, 0);
            lv_obj_align(l, LV_ALIGN_TOP_MID, 0, 1);
            lv_obj_t* s = label(k, symbols[i], &zs_font_12, kText);
            lv_obj_align(s, LV_ALIGN_BOTTOM_MID, 0, -1);
            cap_lit(k, false);
            arrows_[i] = k;
        }
        pressed_ = 0;
    }

    void arrow(int i) {
        if (!arrows_[i]) return;
        cap_lit(arrows_[i], true);
        pressed_ |= 1 << i;
        if (pressed_ == 0xF) {
            lv_label_set_text(status_, LV_SYMBOL_OK "  That's all four.\nEnter for the next page.");
            lv_obj_set_style_text_color(status_, c(kGood), 0);
        }
    }

    void moving() {
        title("Open and go back");
        lv_obj_t* r = rows(root_);
        row(r, "[Enter]", "Open, choose, OK");
        row(r, "[Esc]", "Back. The ` key works too");
        // The Terminal has the screen and the keys to itself (Shell::
        // hold_home does nothing there): tmux's Ctrl+Q is the way out. One
        // line each: a fifth line would push the last row under the bar.
        row(r, "[Esc] held", app_find("terminal") ? "Home. In the Terminal: Ctrl+Q" : "Home, from anywhere");
        row(r, "[1] - [9]", "Pick the 1st to 9th item on screen");
    }

    void home() {
        title("The home screen");
        lv_obj_t* r = rows(root_);
        // Examples that find something on every device: no installable app
        // ("music" finds only Media, which isn't on a new device).
        row(r, "[a] - [z]", "Just type to find apps, settings and actions: wifi, bright, mute");
        row(r, "[1] - [7]", "Open your pinned apps");
        row(r, "[Tab]", "All apps, A to Z. There, Tab, P and a number pins one");
    }

    void menus() {
        title("Menus and help");
        lv_obj_t* r = rows(root_);
        row(r, "[Tab]", "A menu of what you can do here");
        row(r, "[Ctrl] + [a-z]", "Do a menu item straight away");
        row(r, "[0]", "All the keys for this screen");
        note(r, "Try Tab or 0 now.");
    }

    void panel() {
        title("Quick settings");
        lv_obj_t* r = rows(root_);
        row(r, "[Ctrl] + [Space]", "Wi-Fi, Bluetooth, sound, battery");
        row(r, "[Fn] + [S] / [D]", "Volume down / up");
        row(r, "[Fn] + [U] / [I]", "Brightness down / up");
        row(r, "[Fn] + [A]", "Sound on or off");
    }

    void typing() {
        title("Typing");
        lv_obj_t* r = rows(root_);
        row(r, "[Tab]", "Finish the word shown up top");
        row(r, "[Tab] [Tab]", "Swap it for the next one");
        row(r, "[Sym]", "Punctuation and symbols");
        note(r, "Shift, Fn, Ctrl, Sym: tap one, then the key.");
    }

    void lock() {
        title("Lock and screenshots");
        lv_obj_t* r = rows(root_);
        // The key that wakes the screen does nothing else (Power::on_key);
        // then the lock screen takes only Tab. One line each, or the note
        // below goes off the bottom.
        row(r, "[Tab] held", "Lock. Any key, then Tab, unlocks");
        row(r, "[Fn] + [J]", "Screenshot: Pictures/Screenshots");
        // Only with a motion sensor (not on the Lite), and as Settings has it.
        // hal().imu() is read each tick, long before this page is reached.
        if (hal().imu().available && power().sleep_face_down())
            row(r, "Face down", power().wake_on_pickup() ? "Screen off. Pick it up to wake" : "Screen off");
        // Power::tick: only on battery power; off again charging past 20%.
        note(r, "On battery, power saving comes on at 15%.");
    }

    void done() {
        title("You're all set");
        lv_obj_t* r = rows(root_);
        lv_obj_set_style_pad_row(r, 5, 0);
        wifi_ = setup_row(r, "[1]", "Wi-Fi", "");
        wifi_status();
        TimeSettings ts = hal().time_settings();
        std::string zone = ts.zone.empty() ? "Not set" : ts.zone;
        for (char& ch : zone)
            if (ch == '_') ch = ' ';
        setup_row(r, "[2]", "Date & time", zone.c_str());
        setup_row(r, "[3]", "Apps", "Get more apps");
        note(r, "Enter goes home. To see this tour again, type tour there.");
    }

    lv_obj_t* setup_row(lv_obj_t* parent, const char* key, const char* name, const char* value) {
        row(parent, key, name);
        lv_obj_t* line = lv_obj_get_child(parent, int32_t(lv_obj_get_child_count(parent)) - 1);
        lv_obj_t* v = label(line, value, &zs_font_12, kMuted);
        lv_obj_align(v, LV_ALIGN_TOP_RIGHT, 0, 1);
        return v;
    }

    void wifi_status() {
        NetStatus n = hal().net();
        bool online = n.wifi_connected || n.ethernet;
        std::string text = n.wifi_connected && !n.wifi_ssid.empty() ? LV_SYMBOL_OK "  " + n.wifi_ssid
                           : n.ethernet                            ? LV_SYMBOL_OK "  Online by cable"
                           : n.wifi_connected                      ? LV_SYMBOL_OK "  Connected"
                                                                   : "Not connected";
        lv_label_set_text(wifi_, text.c_str());
        lv_obj_set_style_text_color(wifi_, c(online ? kGood : kMuted), 0);
    }

    lv_obj_t* root_ = nullptr;
    lv_obj_t* status_ = nullptr;
    lv_obj_t* wifi_ = nullptr;
    lv_obj_t* arrows_[4] = {};
    int page_ = kHello;
    int pressed_ = 0;
    int ticks_ = 0;
};

}  // namespace

std::unique_ptr<App> make_welcome() { return std::make_unique<Welcome>(); }

bool welcome_due() {
    const char* env = std::getenv("ZS_WELCOME");
    if (env && std::string(env) == "0") return false;
    if (prefs::get_int("welcome.done", 0)) return false;
    // Used ZeroOS before the tour came (it has opened an app, or pinned one):
    // no need to show it, and not later either.
    if (!prefs::get_str("home.recent", "").empty() || prefs::get_str("home.pinned", "\x01") != "\x01") {
        prefs::set_int("welcome.done", 1);
        return false;
    }
    return true;
}

}  // namespace zs
