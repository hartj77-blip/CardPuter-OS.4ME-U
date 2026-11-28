// Settings > Wi-Fi: turn it on or off, scan, pick a network, type the
// password, connect (NetworkManager). A section of Settings
// (apps/settings_sections.h), not an app of its own.
//
//   Left/Right on the first row  Wi-Fi on or off
//   Left/Right on "Adapter"      which adapter scans and joins: Auto, the
//                                built-in one or a USB Wi-Fi adapter (shown
//                                only once there is more than one)
//   Enter  join the network in focus.       Tab: R rescan
//   Typing finds a network (ui/list_view.h).
#include "apps/common.h"
#include "apps/settings_sections.h"
#include "ui/list_view.h"

namespace zs {
namespace {
using namespace theme;

class WifiSection : public App {
public:
    explicit WifiSection(const char* back) : back_(back) {}

    void build(lv_obj_t* root) override {
        root_ = root;
        show_list();
    }

    ~WifiSection() override {   // left before a deferred step ran: don't run it on a freed section
        lv_async_call_cancel(connect_cb, this);
        lv_async_call_cancel(switched_cb, this);
        lv_async_call_cancel(adapter_cb, this);
    }

    bool on_back() override {
        if (pw_page_) {
            show_list(pick_ + first_net_);   // on the network it was for
            return true;
        }
        return false;
    }

    bool on_key(uint32_t key) override {
        if (pw_page_) return password_key(key);
        return list_.key(key);   // typing filters the networks
    }

private:
    void clear() {
        lv_obj_clean(root_);
        lv_group_remove_all_objs(group);
        pw_page_ = nullptr;
    }

    // The switch, then the networks. `focus`: the row to start on.
    void show_list(int focus = 0) {
        clear();
        NetStatus st = hal().net();
        bool on = st.wifi_enabled;
        nets_.clear();
        if (on) nets_ = hal().wifi_scan();
        adapters_ = hal().wifi_adapters();
        std::string chosen = hal().wifi_adapter();
        // The choice only once there is one to make, or one was made (to
        // go back to Auto after the USB adapter is taken out).
        bool choose = on && (adapters_.size() > 1 || !chosen.empty());

        std::string status = !on ? "" : st.wifi_connected ? "Connected to " + st.wifi_ssid : "Not connected";
        if (st.wifi_connected && adapters_.size() > 1)
            for (auto& a : adapters_)
                if (a.iface == st.wifi_iface) status += " (" + a.label + ")";
        settings_heading(root_, "Wi-Fi", status, st.wifi_connected ? kGood : kMuted);

        list_.build(root_, 2, 18, kScreenW - 4, kBodyH - 20);
        list_.add_choice(LV_SYMBOL_WIFI, "Wi-Fi", {"Off", "On"}, on ? 1 : 0, [this](int i) {
            // Not now: the list this row is in is drawn again.
            want_on_ = i == 1;
            lv_async_call_cancel(switched_cb, this);
            lv_async_call(switched_cb, this);
        });
        first_net_ = 1;
        if (choose) {
            std::vector<std::string> names = {"Auto"};
            int at = 0;
            for (size_t i = 0; i < adapters_.size(); ++i) {
                names.push_back(adapters_[i].label);
                if (adapters_[i].id == chosen) at = int(i) + 1;
            }
            // Chosen but not plugged in: say so, and Auto is what's used.
            if (!chosen.empty() && at == 0) {
                names.push_back(chosen == "builtin" ? "Built-in (missing)" : "USB (not plugged in)");
                at = int(names.size()) - 1;
            }
            list_.add_choice(LV_SYMBOL_USB, "Adapter", names, at, [this, names](int i) {
                // "Auto" or an adapter; the "not plugged in" entry keeps the choice.
                want_adapter_ = i == 0 ? "" : size_t(i) <= adapters_.size() ? adapters_[size_t(i) - 1].id
                                                                              : hal().wifi_adapter();
                want_adapter_name_ = names[size_t(i)];
                lv_async_call_cancel(adapter_cb, this);
                lv_async_call(adapter_cb, this);
            });
            first_net_ = 2;
        }
        for (size_t i = 0; i < nets_.size(); ++i) {
            auto& n = nets_[i];
            std::string value = fmt("%d%%", n.signal) + (n.secure ? "" : "  open");
            list_.add(n.connected ? LV_SYMBOL_OK : LV_SYMBOL_WIFI, n.ssid, value, [this, i] {
                pick_ = int(i);
                show_password();
            });
        }
        list_.finish();
        if (focus > 0 && focus < list_.size()) list_.focus(focus);
        if (on && nets_.empty()) {
            lv_obj_t* tip = label(root_, "No networks found. Tab > R rescans.", &zs_font_12, kMuted);
            lv_obj_set_pos(tip, 10, 24 + 24 * first_net_);   // under the rows above it (24 px each)
        }

        std::vector<Action> acts;
        acts.push_back({on ? "Join" : "Turn on", 0, "Enter", nullptr});
        acts.push_back({"Rescan", 'r', nullptr, [this] { rescan(); }, on});
        acts.push_back({back_, 0, "Esc", nullptr});
        shell_set_actions(std::move(acts));
    }

    void rescan() {
        show_list(list_.focused());
        shell_toast("Rescanned");
    }

    static void switched_cb(void* p) {
        auto* s = static_cast<WifiSection*>(p);
        hal().set_wifi_enabled(s->want_on_);
        shell_toast(s->want_on_ ? "Wi-Fi on" : "Wi-Fi off");
        s->show_list();
    }

    static void adapter_cb(void* p) {
        auto* s = static_cast<WifiSection*>(p);
        if (s->want_adapter_ == hal().wifi_adapter()) return;
        hal().set_wifi_adapter(s->want_adapter_);
        shell_toast(s->want_adapter_.empty() ? "Adapter: automatic" : "Adapter: " + s->want_adapter_name_);
        s->show_list(1);   // its networks, still on the Adapter row
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

    static void connect_cb(void* p) {
        auto* s = static_cast<WifiSection*>(p);
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
    std::vector<WifiAdapter> adapters_;
    int first_net_ = 1;               // the list row of the first network
    std::string want_adapter_;        // the Adapter row, until adapter_cb() acts on it
    std::string want_adapter_name_;
    ListView list_;
    int pick_ = -1;
    std::string pw_;
    const char* back_;        // what Esc is called in the bar ("Settings" or "Back")
    bool want_on_ = false;    // the switch, until switched_cb() acts on it
};
}  // namespace

std::unique_ptr<App> make_wifi_section(const char* back) { return std::make_unique<WifiSection>(back); }

}  // namespace zs
