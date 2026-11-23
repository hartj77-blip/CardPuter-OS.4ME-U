// Wi-Fi: scan, pick a network, type the password, connect (NetworkManager).
#include "apps/common.h"

namespace zs {
namespace {
using namespace theme;

class WifiApp : public App {
public:
    void build(lv_obj_t* root) override {
        root_ = root;
        show_list();
    }

    ~WifiApp() override {   // left before a deferred step ran: don't run it on a freed app
        lv_async_call_cancel(show_password_cb, this);
        lv_async_call_cancel(connect_cb, this);
    }

    bool on_back() override {
        if (pw_page_) {
            show_list();
            return true;
        }
        return false;
    }

    bool on_key(uint32_t key) override {
        if (pw_page_) return password_key(key);
        // R rescans until lists filter as you type (docs/UX.md); then it's
        // Tab > R or Ctrl + R only.
        if (key == 'r' || key == 'R') {
            rescan();
            return true;
        }
        return false;
    }

private:
    void clear() {
        lv_obj_clean(root_);
        lv_group_remove_all_objs(group);
        pw_page_ = nullptr;
    }

    void show_list() {
        clear();
        nets_ = hal().wifi_scan();
        NetStatus st = hal().net();

        lv_obj_t* hdr = label(root_, st.wifi_connected ? ("Connected to " + st.wifi_ssid).c_str() : "Not connected",
                              &zs_font_12, st.wifi_connected ? kGood : kMuted);
        lv_obj_set_pos(hdr, 8, 4);

        lv_obj_t* list = make_list(root_, 4, 22, kScreenW - 8, kBodyH - 24);
        for (size_t i = 0; i < nets_.size(); ++i) {
            auto& n = nets_[i];
            std::string text = n.ssid + fmt("   %d%%", n.signal) + (n.secure ? "" : "   open");
            lv_obj_t* b = list_button(list, n.connected ? LV_SYMBOL_OK : LV_SYMBOL_WIFI, text.c_str(), on_pick, this);
            lv_obj_set_user_data(b, (void*)(intptr_t)i);
        }
        finish_list(list);

        bool on = st.wifi_enabled;
        std::vector<Action> acts;
        acts.push_back({"Join", 0, "Enter", nullptr});
        acts.push_back({"Rescan", 'r', nullptr, [this] { rescan(); }});
        acts.push_back({on ? "Turn Wi-Fi off" : "Turn Wi-Fi on", 'w', nullptr, [this, on] {
                            hal().set_wifi_enabled(!on);
                            shell_toast(on ? "Wi-Fi off" : "Wi-Fi on");
                            show_list();
                        }});
        shell_set_actions(std::move(acts));
    }

    void rescan() {
        show_list();
        shell_toast("Rescanned");
    }

    static void on_pick(lv_event_t* e) {
        auto* self = static_cast<WifiApp*>(lv_event_get_user_data(e));
        size_t i = (size_t)(intptr_t)lv_obj_get_user_data(lv_event_get_target_obj(e));
        // Defer: we're inside an event of a widget that show_password() deletes.
        self->pick_ = int(i);
        lv_async_call(show_password_cb, self);
    }

    void show_password() {
        if (pick_ < 0 || pick_ >= int(nets_.size())) return;
        WifiNetwork n = nets_[pick_];
        if (!n.secure) {
            connect(n.ssid, "");
            return;
        }
        clear();
        shell_set_actions({{"Connect", 0, "Enter", nullptr}, {"Delete", 0, "Bksp", nullptr}, {"Back", 0, "Esc", nullptr}});
        pw_page_ = lv_obj_create(root_);
        style_plain(pw_page_);
        lv_obj_set_size(pw_page_, kScreenW, kBodyH);
        lv_obj_t* t = label(pw_page_, ("Password for " + n.ssid).c_str(), &zs_font_14);
        lv_obj_set_pos(t, 10, 10);
        // A plain line of text, not a text box: shown as typed (easier to get
        // right on a small keyboard), no cursor to move, no scrolling.
        lv_obj_t* box = lv_obj_create(pw_page_);
        style_plain(box);
        lv_obj_set_size(box, kScreenW - 20, 30);
        lv_obj_set_pos(box, 10, 36);
        lv_obj_set_style_bg_color(box, c(kSurface), 0);
        lv_obj_set_style_bg_opa(box, LV_OPA_COVER, 0);
        lv_obj_set_style_border_color(box, c(kAccent), 0);
        lv_obj_set_style_border_width(box, 2, 0);
        lv_obj_set_style_radius(box, 4, 0);
        lv_obj_remove_flag(box, LV_OBJ_FLAG_SCROLLABLE);
        pw_label_ = label(box, "", &zs_font_16, 0xffffff);
        lv_obj_set_width(pw_label_, kScreenW - 36);
        lv_label_set_long_mode(pw_label_, LV_LABEL_LONG_CLIP);
        lv_obj_align(pw_label_, LV_ALIGN_LEFT_MID, 6, 0);
        pw_.clear();
        show_typed();
        lv_obj_t* h = label(pw_page_, "Type the password. It shows as you type.", &zs_font_12, kMuted);
        lv_obj_set_pos(h, 10, 76);
    }

    // What's typed, with a cursor; only the end of it if it's too long to fit.
    void show_typed() {
        const int room = kScreenW - 40;
        std::string shown = pw_ + "_";
        size_t from = 0;
        auto width = [](const std::string& t) {
            return lv_text_get_width(t.c_str(), uint32_t(t.size()), &zs_font_16, 0);
        };
        if (width(shown) > room)
            while (from + 1 < shown.size() && width("..." + shown.substr(from)) > room) ++from;
        lv_label_set_text(pw_label_, (from ? "..." + shown.substr(from) : shown).c_str());
    }

    bool password_key(uint32_t key) {
        if (key == LV_KEY_ENTER) {
            lv_async_call(connect_cb, this);
        } else if (key == LV_KEY_BACKSPACE || key == LV_KEY_DEL) {
            if (!pw_.empty()) pw_.pop_back();
        } else if (key >= 32 && key < 127) {
            if (pw_.size() < 63) pw_ += char(key);   // WPA passwords are at most 63 characters
        } else {
            return key != LV_KEY_ESC;   // Esc: back to the list
        }
        show_typed();
        return true;
    }

    static void show_password_cb(void* p) { static_cast<WifiApp*>(p)->show_password(); }
    static void connect_cb(void* p) {
        auto* s = static_cast<WifiApp*>(p);
        if (s->pick_ >= 0 && s->pick_ < int(s->nets_.size())) s->connect(s->nets_[size_t(s->pick_)].ssid, s->pw_);
    }

    void connect(const std::string& ssid, const std::string& pw) {
        shell_toast("Connecting to " + ssid + "...");
        bool ok = hal().wifi_connect(ssid, pw);
        shell_toast(ok ? "Connected to " + ssid : "Could not connect");
        show_list();
    }

    lv_obj_t* root_ = nullptr;
    lv_obj_t* pw_page_ = nullptr;
    lv_obj_t* pw_label_ = nullptr;
    std::vector<WifiNetwork> nets_;
    int pick_ = -1;
    std::string pw_;
};
}  // namespace

std::unique_ptr<App> make_wifi_app() { return std::make_unique<WifiApp>(); }

}  // namespace zs
