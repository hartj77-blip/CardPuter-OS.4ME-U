// Settings > Date & time: whether the clock comes from the internet, the
// time and date set by hand, and the time zone. A section of Settings
// (apps/settings_sections.h), not an app of its own.
//
//   Set automatically  Left/Right. On: the time comes from the internet
//                      whenever the device is online.
//   Time, Date         Enter, type the digits (1430 is 14:30; 20261127 is
//                      27 November 2026), Enter. While the time is
//                      automatic the system refuses it, so this asks first
//                      and turns Set automatically off.
//   Time zone          Enter: every zone, A to Z. Typing finds one ("lond").
//
// The hardware clock (RTC) is set along with the system's (hal/hal.h), so
// the time holds while the power is off.
#include <ctime>

#include "apps/common.h"
#include "apps/settings_sections.h"
#include "ui/list_view.h"

namespace zs {
namespace {
using namespace theme;

tm local_now() {
    time_t t = time(nullptr);
    tm lt{};
    localtime_r(&t, &lt);
    return lt;
}

std::string clock_text(const char* format) {
    tm lt = local_now();
    char buf[40];
    strftime(buf, sizeof buf, format, &lt);
    return buf;
}

// "Europe/New_York" as people read it.
std::string zone_text(std::string zone) {
    for (char& ch : zone)
        if (ch == '_') ch = ' ';
    return zone.empty() ? "Not set" : zone;
}

class DateTimeSection : public App {
public:
    explicit DateTimeSection(const char* back) : back_(back) {}

    ~DateTimeSection() override {   // left before a deferred step ran
        lv_async_call_cancel(switched_cb, this);
        lv_async_call_cancel(apply_cb, this);
    }

    void build(lv_obj_t* root) override {
        root_ = root;
        show_main();
    }

    bool on_key(uint32_t key) override {
        if (page_ == Page::Time || page_ == Page::Date) return entry_key(key);
        return list_.key(key);   // Zones: typing filters them
    }

    bool on_back() override {
        if (page_ == Page::Main) return false;
        show_main(page_ == Page::Time ? time_row_ : page_ == Page::Date ? date_row_ : zone_row_);
        return true;
    }

    // The rows follow the clock. Every few seconds: is the time in sync yet,
    // and was anything changed elsewhere (timedatectl over SSH)?
    void tick() override {
        if (page_ != Page::Main) return;
        ++ticks_;
        if (ticks_ % 50 == 0) {
            TimeSettings now = hal().time_settings();
            if (now.automatic != st_.automatic || now.zone != st_.zone || now.can_automatic != st_.can_automatic) {
                show_main(list_.focused());
                return;
            }
            if (now.synced != st_.synced) {
                st_ = now;
                show_state();
            }
        }
        if (ticks_ % 5 == 0) show_clock();
    }

private:
    enum class Page { Main, Time, Date, Zones };

    void clear() {
        lv_obj_clean(root_);
        lv_group_remove_all_objs(group);
        list_.reset();
        state_ = entry_ = nullptr;
    }

    // ---- The section ------------------------------------------------------

    std::string state_text(uint32_t& color) const {
        color = kMuted;
        if (!st_.automatic) return "Set by hand";
        if (st_.synced) color = kGood;
        return st_.synced ? "In sync" : "Waiting to go online";
    }

    void show_state() {
        if (!state_) return;
        uint32_t color;
        lv_label_set_text(state_, state_text(color).c_str());
        lv_obj_set_style_text_color(state_, c(color), 0);
    }

    void show_clock() {
        list_.set_value(time_row_, clock_text("%H:%M:%S"));
        list_.set_value(date_row_, clock_text("%a %-d %b %Y"));
    }

    void show_main(int focus = 0) {
        clear();
        page_ = Page::Main;
        st_ = hal().time_settings();
        uint32_t color;
        std::string state = state_text(color);
        state_ = settings_heading(root_, "Date & time", state, color);

        list_.build(root_, 2, 18, kScreenW - 4, kBodyH - 20);
        // No row for it on a system that can't fetch the time at all.
        if (st_.can_automatic) {
            list_.add_choice(LV_SYMBOL_REFRESH, "Set automatically", {"Off", "On"}, st_.automatic ? 1 : 0, [this](int i) {
                // Not now: the list this row is in is drawn again.
                want_auto_ = i == 1;
                lv_async_call_cancel(switched_cb, this);
                lv_async_call(switched_cb, this);
            });
        }
        time_row_ = list_.add(ZS_ICON_CLOCK, "Time", clock_text("%H:%M:%S"), [this] { set_by_hand(Page::Time); });
        date_row_ = list_.add(LV_SYMBOL_LIST, "Date", clock_text("%a %-d %b %Y"), [this] { set_by_hand(Page::Date); });
        zone_row_ = list_.add(ZS_ICON_GLOBE, "Time zone", zone_text(st_.zone), [this] { show_zones(); });
        list_.finish();
        if (focus > 0 && focus < list_.size()) list_.focus(focus);

        shell_set_actions({{"Select", 0, "Enter", nullptr}, {back_, 0, "Esc", nullptr}});
    }

    static void switched_cb(void* p) {
        auto* s = static_cast<DateTimeSection*>(p);
        if (!hal().set_time_automatic(s->want_auto_)) shell_toast("Couldn't change that");
        s->show_main();
    }

    // The system refuses a time while it fetches its own: ask, then turn
    // that off.
    void set_by_hand(Page what) {
        if (!st_.automatic) {
            show_entry(what);
            return;
        }
        shell_confirm(what == Page::Time ? "Set the time by hand?" : "Set the date by hand?",
                      "Set automatically will be turned off.", "Set it", [this, what] {
                          if (hal().set_time_automatic(false)) {
                              show_entry(what);
                          } else {
                              shell_toast("Couldn't turn Set automatically off");
                              show_main(what == Page::Time ? time_row_ : date_row_);
                          }
                      });
    }

    // ---- Typing a time or a date ------------------------------------------
    // Digits only, as on a microwave: the colon and the dashes put themselves
    // in. A plain line of text, not a text box, like Wi-Fi's password.

    void show_entry(Page what) {
        clear();
        page_ = what;
        typed_.clear();
        bool time = what == Page::Time;
        shell_set_actions({{"Set", 0, "Enter", nullptr}, {"Delete", 0, "Bksp", nullptr}, {"Back", 0, "Esc", nullptr}});
        lv_obj_t* page = lv_obj_create(root_);
        style_plain(page);
        lv_obj_set_size(page, kScreenW, kBodyH);
        lv_obj_remove_flag(page, LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_t* t = label(page, time ? "Time: hours and minutes" : "Date: year, month and day", &zs_font_14);
        lv_obj_set_pos(t, 10, 10);
        lv_obj_t* box = lv_obj_create(page);
        style_plain(box);
        lv_obj_set_size(box, kScreenW - 20, 30);
        lv_obj_set_pos(box, 10, 36);
        lv_obj_set_style_bg_color(box, c(kSurface), 0);
        lv_obj_set_style_bg_opa(box, LV_OPA_COVER, 0);
        lv_obj_set_style_border_color(box, c(kAccent), 0);
        lv_obj_set_style_border_width(box, 2, 0);
        lv_obj_set_style_radius(box, 4, 0);
        lv_obj_remove_flag(box, LV_OBJ_FLAG_SCROLLABLE);
        entry_ = label(box, "", &zs_font_16, 0xffffff);
        lv_obj_align(entry_, LV_ALIGN_LEFT_MID, 6, 0);
        show_typed();
        std::string now = time ? clock_text("It is %H:%M now. ") : clock_text("Today is set to %Y-%m-%d. ");
        std::string hint = now + (time ? "Type 4 digits, 24-hour: 1430 is 14:30." : "Type 8 digits: 20261127 is 27 Nov 2026.");
        lv_obj_t* h = label(page, hint.c_str(), &zs_font_12, kMuted);
        lv_obj_set_width(h, kScreenW - 20);
        lv_label_set_long_mode(h, LV_LABEL_LONG_WRAP);
        lv_obj_set_pos(h, 10, 76);
    }

    // "14:3_", "2026-11-2_".
    void show_typed() {
        bool time = page_ == Page::Time;
        std::string s;
        for (size_t i = 0; i < typed_.size(); ++i) {
            if (time ? i == 2 : (i == 4 || i == 6)) s += time ? ':' : '-';
            s += typed_[i];
        }
        if (typed_.size() < (time ? 4u : 8u)) s += "_";
        lv_label_set_text(entry_, s.c_str());
    }

    bool entry_key(uint32_t key) {
        size_t want = page_ == Page::Time ? 4 : 8;
        if (key == LV_KEY_ENTER) {
            // Not now: setting it draws the section again, under this key.
            lv_async_call_cancel(apply_cb, this);
            lv_async_call(apply_cb, this);
            return true;
        }
        if (key == LV_KEY_BACKSPACE || key == LV_KEY_DEL) {
            if (!typed_.empty()) typed_.pop_back();
        } else if (key >= '0' && key <= '9') {
            if (typed_.size() < want) typed_ += char(key);
        } else {
            return key != LV_KEY_ESC;   // Esc: back to the section; nothing else types here
        }
        show_typed();
        return true;
    }

    static void apply_cb(void* p) { static_cast<DateTimeSection*>(p)->apply(); }

    void apply() {
        if (page_ != Page::Time && page_ != Page::Date) return;
        bool time = page_ == Page::Time;
        if (typed_.size() != (time ? 4u : 8u)) {
            shell_toast(time ? "Type all 4 digits: hours, then minutes" : "Type all 8 digits: year, month, day");
            return;
        }
        auto num = [this](size_t from, size_t n) { return std::atoi(typed_.substr(from, n).c_str()); };
        tm lt = local_now();   // the half that isn't being set stays as it is
        int year = lt.tm_year + 1900, month = lt.tm_mon + 1, day = lt.tm_mday;
        int hour = lt.tm_hour, minute = lt.tm_min, second = lt.tm_sec;
        if (time) {
            hour = num(0, 2);
            minute = num(2, 2);
            second = 0;
            if (hour > 23 || minute > 59) {
                shell_toast("No such time: hours go to 23, minutes to 59");
                return;
            }
        } else {
            year = num(0, 4);
            month = num(4, 2);
            day = num(6, 2);
            // The hardware clock counts from 2000 to 2099. mktime() rolls a
            // day that doesn't exist (30 February) into the next month.
            tm check{};
            check.tm_year = year - 1900;
            check.tm_mon = month - 1;
            check.tm_mday = day;
            check.tm_hour = 12;
            check.tm_isdst = -1;
            bool real = month >= 1 && month <= 12 && day >= 1 && mktime(&check) != time_t(-1) &&
                        check.tm_mon == month - 1 && check.tm_mday == day;
            if (year < 2000 || year > 2099 || !real) {
                shell_toast(year < 2000 || year > 2099 ? "The year must be 2000 to 2099" : "No such date");
                return;
            }
        }
        if (!hal().set_time(year, month, day, hour, minute, second)) {
            shell_toast(time ? "Couldn't set the time" : "Couldn't set the date");
            return;
        }
        shell_toast(time ? fmt("Time set to %02d:%02d", hour, minute) : fmt("Date set to %04d-%02d-%02d", year, month, day));
        show_main(time ? time_row_ : date_row_);
    }

    // ---- Time zone -------------------------------------------------------

    void show_zones() {
        clear();
        page_ = Page::Zones;
        zones_ = hal().time_zones();
        settings_heading(root_, "Time zone", zone_text(st_.zone), kMuted);
        list_.build(root_, 2, 18, kScreenW - 4, kBodyH - 20);
        list_.set_empty_text(zones_.empty() ? "No time zones on this system" : "No time zone by that name");
        int current = -1;
        for (size_t i = 0; i < zones_.size(); ++i) {
            bool on = zones_[i] == st_.zone;
            int row = list_.add(on ? LV_SYMBOL_OK : nullptr, zone_text(zones_[i]), "", [this, i] { pick_zone(i); });
            if (on) current = row;
        }
        list_.finish();
        if (current > 0) list_.focus(current);
        shell_set_actions({{"Choose", 0, "Enter", nullptr}, {"Find", 0, "a-z", nullptr}, {"Back", 0, "Esc", nullptr}});
    }

    void pick_zone(size_t i) {
        if (i >= zones_.size()) return;
        std::string zone = zones_[i];
        if (hal().set_time_zone(zone)) shell_toast("Time zone: " + zone_text(zone));
        else shell_toast("Couldn't set the time zone");
        show_main(zone_row_);
    }

    lv_obj_t* root_ = nullptr;
    lv_obj_t* state_ = nullptr;   // the heading's right side: in sync, or set by hand
    lv_obj_t* entry_ = nullptr;   // what is typed, on the Time and Date pages
    ListView list_;
    Page page_ = Page::Main;
    TimeSettings st_;
    std::vector<std::string> zones_;
    std::string typed_;           // digits only
    int time_row_ = 0, date_row_ = 0, zone_row_ = 0;
    int ticks_ = 0;
    const char* back_;            // what Esc is called in the bar ("Settings" or "Back")
    bool want_auto_ = false;      // the switch, until switched_cb() acts on it
};
}  // namespace

std::unique_ptr<App> make_datetime_section(const char* back) { return std::make_unique<DateTimeSection>(back); }

}  // namespace zs
