// Grove: use things plugged into the Grove port (docs/GROVE.md).
// For now, a NeoPixel (WS2812 / SK6812) LED strip such as M5Stack's Grove
// RGB LED strip. More Grove devices go in the list on the first page.
//
// LED strip page:
//   Up / Down     choose a row
//   Left / Right  change it (Enter also steps it on)
//   Space         lights on / off
// The strip itself is driven by zeroos-grove-ledd (shell/ledd/), a small
// root service, because the Grove pins need SMI and DMA for the strip's
// timing. This page tells it what to show over a socket; the lights keep
// going after you leave. The preview on screen runs the same effect code.
#include <fcntl.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>

#include <algorithm>
#include <cerrno>
#include <cstring>
#include <ctime>

#include "apps/common.h"
#include "grove/leds.h"
#include "ui/prefs.h"
#include "ui/tracker.h"

namespace zs {
namespace {
using namespace theme;

constexpr const char* kSocket = "/run/zeroos-grove-ledd.sock";

// One line out, one line back, to zeroos-grove-ledd. Never blocks: a
// missing service just leaves connected() false, and it tries again.
class LeddClient {
public:
    ~LeddClient() { drop(); }
    bool connected() const { return fd_ >= 0; }

    // Connect if we aren't (at most every two seconds). True on a new connection.
    bool ensure() {
        if (fd_ >= 0) return false;
        time_t now = std::time(nullptr);
        if (now < next_try_) return false;
        next_try_ = now + 2;
        int fd = socket(AF_UNIX, SOCK_STREAM | SOCK_CLOEXEC, 0);
        if (fd < 0) return false;
        sockaddr_un a{};
        a.sun_family = AF_UNIX;
        std::strncpy(a.sun_path, kSocket, sizeof a.sun_path - 1);
        if (connect(fd, reinterpret_cast<sockaddr*>(&a), sizeof a) < 0) {
            close(fd);
            return false;
        }
        fcntl(fd, F_SETFL, fcntl(fd, F_GETFL) | O_NONBLOCK);
        fd_ = fd;
        return true;
    }

    void send_line(const std::string& l) {
        if (fd_ < 0) return;
        std::string s = l + "\n";
        if (::send(fd_, s.data(), s.size(), MSG_NOSIGNAL) != ssize_t(s.size())) drop();
    }

    // The replies that came in since last time.
    std::vector<std::string> replies() {
        std::vector<std::string> out;
        if (fd_ < 0) return out;
        char buf[1024];
        for (;;) {
            ssize_t n = recv(fd_, buf, sizeof buf, 0);
            if (n > 0) {
                in_.append(buf, size_t(n));
                continue;
            }
            if (n == 0 || (errno != EAGAIN && errno != EWOULDBLOCK)) drop();
            break;
        }
        size_t nl;
        while ((nl = in_.find('\n')) != std::string::npos) {
            out.push_back(in_.substr(0, nl));
            in_.erase(0, nl + 1);
        }
        return out;
    }

private:
    void drop() {
        if (fd_ >= 0) close(fd_);
        fd_ = -1;
        in_.clear();
    }
    int fd_ = -1;
    std::string in_;
    time_t next_try_ = 0;
};

struct Named {
    const char* name;
    uint32_t rgb;
};
const Named kColors[] = {
    {"Red", 0xff0000},    {"Orange", 0xff5000}, {"Yellow", 0xffb000}, {"Green", 0x00ff00},
    {"Cyan", 0x00ffd0},   {"Blue", 0x0000ff},   {"Purple", 0x8000ff}, {"Pink", 0xff0070},
    {"White", 0xffffff},  {"Warm white", 0xff9840},
};
constexpr int kNColors = int(sizeof kColors / sizeof kColors[0]);
const leds::Effect kEffects[] = {leds::Effect::Solid, leds::Effect::Breathe, leds::Effect::Rainbow,
                                 leds::Effect::Chase, leds::Effect::Sparkle};
constexpr int kNEffects = int(sizeof kEffects / sizeof kEffects[0]);
const int kBrightness[] = {2, 5, 10, 15, 20, 25, 30, 40, 50, 60, 70, 80, 90, 100};
constexpr int kNBrightness = int(sizeof kBrightness / sizeof kBrightness[0]);
constexpr int kPreviewMax = 30;   // dots drawn; a longer strip shows its first 30

const char* effect_title(leds::Effect e) {
    switch (e) {
    case leds::Effect::Solid: return "Solid";
    case leds::Effect::Breathe: return "Breathe";
    case leds::Effect::Rainbow: return "Rainbow";
    case leds::Effect::Chase: return "Chase";
    case leds::Effect::Sparkle: return "Sparkle";
    case leds::Effect::Pixels: return "Set by a script";
    case leds::Effect::Off: break;
    }
    return "Off";
}

class GroveApp : public App {
public:
    ~GroveApp() override {
        if (timer_) lv_timer_delete(timer_);
    }

    void build(lv_obj_t* root) override {
        root_ = root;
        load();
        show_devices();
    }

    void tick() override {
        if (!strip_page_) return;
        if (client_.ensure()) {
            client_.send_line("status");
            adopt_ = true;   // take on what the strip is already doing
        }
        for (auto& r : client_.replies()) on_reply(r);
        update_status();
    }

    bool on_key(uint32_t key) override {
        if (!strip_page_) return false;
        switch (key) {
        case LV_KEY_UP: select(row_ - 1); return true;
        case LV_KEY_DOWN: select(row_ + 1); return true;
        case LV_KEY_LEFT: change(-1); return true;
        case LV_KEY_RIGHT:
        case LV_KEY_ENTER: change(1); return true;
        case ' ': set_on(!on_); return true;
        }
        return false;
    }

    bool on_back() override {
        if (!strip_page_) return false;
        show_devices();
        return true;
    }

private:
    enum Row { RowLights, RowEffect, RowColor, RowBrightness, RowSpeed, RowCount, NRows };

    // ---- First page: what's on the port --------------------------------
    void show_devices() {
        clear();
        strip_page_ = false;
        page_ = lv_obj_create(root_);
        style_plain(page_);
        lv_obj_set_size(page_, kScreenW, kContentH);
        lv_obj_remove_flag(page_, LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_t* list = make_list(page_, 4, 4, kScreenW - 8, 78);
        list_button(list, LV_SYMBOL_CHARGE, "LED strip (NeoPixel)", [](lv_event_t* e) {
            static_cast<GroveApp*>(lv_event_get_user_data(e))->show_strip();
        }, this);
        finish_list(list);
        lv_obj_t* note = label(page_,
                               "Power the Cardputer off before plugging in.\n"
                               "Grove pins: GPIO 14 and 15 (UART mode), 5 V.\n"
                               "More Grove devices will be listed here.",
                               &zs_font_12, kMuted);
        lv_obj_set_pos(note, 8, 90);
    }

    // ---- LED strip page --------------------------------------------------
    void show_strip() {
        clear();
        strip_page_ = true;
        page_ = lv_obj_create(root_);
        style_plain(page_);
        lv_obj_set_size(page_, kScreenW, kContentH);
        lv_obj_remove_flag(page_, LV_OBJ_FLAG_SCROLLABLE);

        // The strip on screen: one dot per LED.
        int n = std::min(s_.count, kPreviewMax);
        int pitch = std::min(16, (kScreenW - 16) / n);
        int size = std::max(4, pitch - 3);
        int x0 = (kScreenW - pitch * n) / 2;
        dots_.clear();
        for (int i = 0; i < n; ++i) {
            lv_obj_t* d = lv_obj_create(page_);
            lv_obj_remove_style_all(d);
            lv_obj_set_size(d, size, size);
            lv_obj_set_pos(d, x0 + i * pitch + (pitch - size) / 2, 4 + (14 - size) / 2);
            lv_obj_set_style_radius(d, LV_RADIUS_CIRCLE, 0);
            lv_obj_set_style_bg_opa(d, LV_OPA_COVER, 0);
            lv_obj_set_style_border_width(d, 1, 0);
            lv_obj_set_style_border_color(d, c(kSurface2), 0);
            dots_.push_back(d);
        }

        static const char* kNames[NRows] = {"Lights", "Effect", "Colour", "Brightness", "Speed", "LEDs"};
        for (int r = 0; r < NRows; ++r) {
            lv_obj_t* row = lv_obj_create(page_);
            lv_obj_remove_style_all(row);
            lv_obj_set_size(row, kScreenW - 12, 18);
            lv_obj_set_pos(row, 6, 22 + r * 18);
            lv_obj_set_style_radius(row, 5, 0);
            lv_obj_set_style_bg_color(row, c(kAccent), 0);
            lv_obj_t* k = label(row, kNames[r], &zs_font_12, kMuted);
            lv_obj_align(k, LV_ALIGN_LEFT_MID, 6, 0);
            lv_obj_t* v = label(row, "", &zs_font_12, kText);
            lv_obj_align(v, LV_ALIGN_RIGHT_MID, -6, 0);
            rows_[r] = row;
            keys_[r] = k;
            values_[r] = v;
        }
        status_ = label(page_, "", &zs_font_12, kMuted);
        lv_obj_set_width(status_, kScreenW - 16);
        lv_label_set_long_mode(status_, LV_LABEL_LONG_DOT);
        lv_obj_set_pos(status_, 8, 133);

        select(row_);
        show_values();
        timer_ = lv_timer_create([](lv_timer_t* t) {
            static_cast<GroveApp*>(lv_timer_get_user_data(t))->animate();
        }, 40, this);
        animate();
        tick();
    }

    void clear() {
        if (timer_) lv_timer_delete(timer_);
        timer_ = nullptr;
        if (group) lv_group_remove_all_objs(group);
        if (page_) lv_obj_delete(page_);
        page_ = nullptr;
        dots_.clear();
    }

    void select(int r) {
        row_ = std::clamp(r, 0, NRows - 1);
        for (int i = 0; i < NRows; ++i) {
            bool on = i == row_;
            lv_obj_set_style_bg_opa(rows_[i], on ? LV_OPA_COVER : LV_OPA_TRANSP, 0);
            lv_obj_set_style_text_color(keys_[i], c(on ? kOnAccent : kMuted), 0);
            lv_obj_set_style_text_color(values_[i], c(on ? kOnAccent : kText), 0);
        }
    }

    void change(int d) {
        switch (row_) {
        case RowLights: set_on(!on_); return;
        case RowEffect: {
            int i = 0;
            while (i < kNEffects && kEffects[i] != effect_) ++i;
            // From "set by a script" (not in the list), go to the first.
            effect_ = kEffects[i == kNEffects ? 0 : (i + d + kNEffects) % kNEffects];
            break;
        }
        case RowColor: {
            color_ = (color_ + d + kNColors) % kNColors;
            break;
        }
        case RowBrightness: {
            int i = 0;
            while (i < kNBrightness - 1 && kBrightness[i] < s_.brightness) ++i;
            s_.brightness = kBrightness[std::clamp(i + d, 0, kNBrightness - 1)];
            break;
        }
        case RowSpeed: s_.speed = std::clamp(s_.speed + d, 1, leds::kMaxSpeed); break;
        case RowCount: {
            s_.count = std::clamp(s_.count + d, 1, leds::kMaxLeds);
            save();
            show_strip();   // redraw the dots for the new length
            push();
            return;
        }
        }
        if (!on_ && row_ != RowLights) on_ = true;   // changing the look turns it on: you see what you chose
        save();
        show_values();
        push();
    }

    void set_on(bool on) {
        on_ = on;
        if (on) hal().set_ext_power(true);
        show_values();
        push();
        // The 5 V stays on for the mesh or a GPS track that also need it.
        if (!on && !hal().mesh().running && !track_state().recording) hal().set_ext_power(false);
    }

    // What to send: the strip's settings, with the effect off while it's off.
    leds::Settings wanted() const {
        leds::Settings w = s_;
        w.effect = on_ ? effect_ : leds::Effect::Off;
        uint32_t c = kColors[color_].rgb;
        w.color = {uint8_t(c >> 16), uint8_t(c >> 8), uint8_t(c)};
        return w;
    }

    void push() {
        leds::Settings w = wanted();
        client_.send_line(w.effect == leds::Effect::Off ? "off" : "set " + leds::to_line(w));
    }

    void show_values() {
        if (!strip_page_) return;
        lv_label_set_text(values_[RowLights], on_ ? "On" : "Off");
        lv_label_set_text(values_[RowEffect], fmt("< %s >", effect_title(effect_)).c_str());
        lv_label_set_text(values_[RowColor], fmt("< %s >", kColors[color_].name).c_str());
        lv_label_set_text(values_[RowBrightness], fmt("< %d%% >", s_.brightness).c_str());
        lv_label_set_text(values_[RowSpeed], fmt("< %d >", s_.speed).c_str());
        lv_label_set_text(values_[RowCount], fmt("< %d >", s_.count).c_str());
    }

    void animate() {
        // The preview at full brightness (dim LEDs are bright to the eye; a
        // dim dot on the LCD would just look off), but black when off.
        leds::Settings w = wanted();
        w.brightness = 100;
        leds::render(w, lv_tick_get(), &px_);
        for (size_t i = 0; i < dots_.size() && i < px_.size(); ++i) {
            auto p = px_[i];
            lv_obj_set_style_bg_color(dots_[i], lv_color_make(p.r, p.g, p.b), 0);
        }
    }

    // ---- The service's replies -----------------------------------------
    void on_reply(const std::string& r) {
        if (r.rfind("error ", 0) == 0) {
            reply_error_ = r.substr(6);
            return;
        }
        if (r.rfind("ok ", 0) != 0) return;
        reply_error_.clear();
        std::string body = r.substr(3);
        size_t hw = body.find(" hardware=");
        std::string settings = body.substr(0, hw);
        hardware_ = hw == std::string::npos ? "" : body.substr(hw + 10, body.find(' ', hw + 10) - hw - 10);
        size_t e = body.find(" error=");
        hw_error_ = e == std::string::npos ? "" : body.substr(e + 7);
        if (adopt_) {
            adopt_ = false;
            leds::Settings now = s_;
            if (leds::apply_line(settings, &now).empty() && now.effect != leds::Effect::Off) {
                // The strip is already lit (by us earlier, or a script): show that.
                on_ = true;
                effect_ = now.effect;
                s_.brightness = now.brightness;
                s_.speed = now.speed;
                s_.pixels = now.pixels;
                uint32_t c = (uint32_t(now.color.r) << 16) | (uint32_t(now.color.g) << 8) | now.color.b;
                for (int i = 0; i < kNColors; ++i)
                    if (kColors[i].rgb == c) color_ = i;
                if (now.count != s_.count) {
                    s_.count = now.count;
                    show_strip();
                    return;
                }
                show_values();
            }
        }
    }

    void update_status() {
        std::string text;
        uint32_t color = kMuted;
        if (!client_.connected()) {
#if ZS_BACKEND_DEVICE
            text = "Strip service isn't running (is the Grove app installed?)";
            color = kWarn;
#else
            text = "Simulator: the preview only";
#endif
        } else if (!reply_error_.empty()) {
            text = reply_error_;
            color = kBad;
        } else if (hardware_ == "error") {
            text = "Strip: " + hw_error_;
            color = kBad;
        } else if (hardware_ == "on" && on_) {
            text = "Lit. Space: on/off. Leave the app: it stays lit.";
            color = kGood;
        } else {
            text = on_ ? "Starting the strip..." : "Off. Space turns it on.";
        }
        if (text != status_text_) {
            status_text_ = text;
            lv_label_set_text(status_, text.c_str());
            lv_obj_set_style_text_color(status_, c(color), 0);
        }
    }

    // ---- Saved between visits (~/.config/zeroos/settings.conf) -----------
    void load() {
        leds::effect_from_name(prefs::get_str("grove.effect", "rainbow"), &effect_);
        if (effect_ == leds::Effect::Off || effect_ == leds::Effect::Pixels) effect_ = leds::Effect::Rainbow;
        color_ = std::clamp(prefs::get_int("grove.color", 1), 0, kNColors - 1);
        s_.brightness = std::clamp(prefs::get_int("grove.brightness", 10), 1, 100);
        s_.speed = std::clamp(prefs::get_int("grove.speed", 5), 1, leds::kMaxSpeed);
        s_.count = std::clamp(prefs::get_int("grove.count", 15), 1, leds::kMaxLeds);
    }
    void save() {
        if (effect_ != leds::Effect::Pixels) prefs::set_str("grove.effect", leds::effect_name(effect_));
        prefs::set_int("grove.color", color_);
        prefs::set_int("grove.brightness", s_.brightness);
        prefs::set_int("grove.speed", s_.speed);
        prefs::set_int("grove.count", s_.count);
    }

    lv_obj_t* root_ = nullptr;
    lv_obj_t* page_ = nullptr;
    lv_obj_t* status_ = nullptr;
    lv_obj_t *rows_[NRows] = {}, *keys_[NRows] = {}, *values_[NRows] = {};
    std::vector<lv_obj_t*> dots_;
    lv_timer_t* timer_ = nullptr;
    bool strip_page_ = false;
    int row_ = RowEffect;

    leds::Settings s_;
    leds::Effect effect_ = leds::Effect::Rainbow;
    int color_ = 1;
    bool on_ = false;
    std::vector<leds::Rgb> px_;

    LeddClient client_;
    bool adopt_ = false;
    std::string hardware_, hw_error_, reply_error_, status_text_;
};
}  // namespace

std::unique_ptr<App> make_grove_app() { return std::make_unique<GroveApp>(); }

}  // namespace zs
