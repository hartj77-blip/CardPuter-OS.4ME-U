// Settings: one numbered list of sections (docs/UX.md), each a list of its
// own. Values change with Left/Right (or Enter); sections that are apps of
// their own (Wi-Fi, Update, About) open them, and Esc there comes back here.
#include <algorithm>
#include <functional>
#include <string>
#include <vector>

#include "apps/common.h"
#include "ui/list_view.h"
#include "ui/power.h"
#include "ui/remote.h"
#include "ui/suggest_bar.h"
#include "ui/sync.h"

namespace zs {
namespace {
using namespace theme;

enum class Page { Root, Bluetooth, Display, Sound, Battery, Remote, Power };

// A value row's choices: what it shows and what it stores.
struct Choice {
    const char* text;
    int value;
};

const char* const kOffOn[] = {"Off", "On"};

class SettingsApp : public App {
public:
    void build(lv_obj_t* root) override {
        root_ = root;
        // Opened at a section (home search: "Brightness" opens Display).
        static const std::pair<const char*, Page> kSections[] = {
            {"bluetooth", Page::Bluetooth}, {"display", Page::Display}, {"sound", Page::Sound},
            {"battery", Page::Battery},     {"remote", Page::Remote},   {"power", Page::Power}};
        std::string arg = shell_app_arg();
        for (auto& [name, page] : kSections)
            if (arg == name) {
                direct_ = true;   // before show(): the bar says Esc goes Back
                show(page);
                return;
            }
        show(Page::Root);
    }

    bool on_key(uint32_t key) override { return list_.key(key); }

    bool on_back() override {
        if (page_ == Page::Root || direct_) return false;   // opened at a section: Esc leaves
        Page from = page_;
        show(Page::Root);
        focus_section(from);
        return true;
    }

private:
    void show(Page p) {
        page_ = p;
        has_values_ = false;
        lv_obj_clean(root_);
        lv_group_remove_all_objs(group);
        static const char* const kNames[] = {"", "Bluetooth", "Display", "Sound", "Battery & power",
                                             "Remote & sync", "Restart or shut down"};
        if (p == Page::Root) {
            list_.build(root_, 2, 2, kScreenW - 4, kBodyH - 4);
        } else {
            lv_obj_t* h = label(root_, (std::string("Settings  " LV_SYMBOL_RIGHT "  ") + kNames[int(p)]).c_str(),
                                &zs_font_12, kMuted);
            lv_obj_set_pos(h, 8, 2);
            list_.build(root_, 2, 18, kScreenW - 4, kBodyH - 20);
        }
        switch (p) {
            case Page::Root: root_page(); break;
            case Page::Bluetooth: bluetooth_page(); break;
            case Page::Display: display_page(); break;
            case Page::Sound: sound_page(); break;
            case Page::Battery: battery_page(); break;
            case Page::Remote: remote_page(); break;
            case Page::Power: power_page(); break;
        }
        list_.finish();
        if (p == Page::Root) {
            shell_set_actions({{"Open", 0, "Enter", nullptr}, {"Find", 0, "a-z", nullptr}, {"Keys", 0, "0", nullptr}});
        } else if (has_values_) {
            shell_set_actions({{"Change", 0, LV_SYMBOL_LEFT LV_SYMBOL_RIGHT, nullptr},
                               {direct_ ? "Back" : "Settings", 0, "Esc", nullptr}});
        } else {
            shell_set_actions({{"Select", 0, "Enter", nullptr}, {direct_ ? "Back" : "Settings", 0, "Esc", nullptr}});
        }
    }

    void open(Page p) {
        show(p);
    }

    // Back on the root page: focus the section we came from.
    void focus_section(Page from) {
        static const Page order[] = {Page::Bluetooth, Page::Display, Page::Sound, Page::Battery, Page::Remote,
                                     Page::Power};
        int row = 0;
        for (int i = 0; i < 6; ++i)
            if (order[i] == from) row = i == 5 ? 8 : i + 1;   // Wi-Fi is row 0; Power is last
        for (int i = 0; i < row; ++i) lv_group_focus_next(group);
    }

    // ---- Root -----------------------------------------------------------

    void root_page() {
        auto& h = hal();
        NetStatus n = h.net();
        auto& p = power();
        std::string wifi = !n.wifi_enabled ? "Off" : n.wifi_connected ? n.wifi_ssid : "Not connected";
        static const char* const kProfiles[] = {"Saver", "Balanced", "Performance"};
        std::string version = h.update().current;

        list_.add(LV_SYMBOL_WIFI, "Wi-Fi", wifi, [] { shell_open_app("wifi"); });
        list_.add(LV_SYMBOL_BLUETOOTH, "Bluetooth", n.bt_enabled ? "On" : "Off", [this] { open(Page::Bluetooth); });
        list_.add(LV_SYMBOL_EYE_OPEN, "Display", fmt("%d%%, dim %s", p.user_brightness(), secs(p.dim_after_s()).c_str()),
                  [this] { open(Page::Display); });
        list_.add(LV_SYMBOL_VOLUME_MAX, "Sound", h.muted() ? "Muted" : fmt("%d%%", h.volume()),
                  [this] { open(Page::Sound); });
        list_.add(LV_SYMBOL_BATTERY_3, "Battery & power", kProfiles[std::clamp(int(p.profile()), 0, 2)],
                  [this] { open(Page::Battery); });
        list_.add(LV_SYMBOL_SHUFFLE, "Remote & sync", "", [this] { open(Page::Remote); });
        list_.add(LV_SYMBOL_DOWNLOAD, "Software update", version.empty() ? "" : version,
                  [] { shell_open_app("update"); });
        list_.add(LV_SYMBOL_LIST, "About this device", "", [] { shell_open_app("sysmon"); });
        list_.add(LV_SYMBOL_POWER, "Restart or shut down", "", [this] { open(Page::Power); });
        list_.set_empty_text("No setting by that name");
    }

    // ---- Sections -------------------------------------------------------

    void bluetooth_page() {
        NetStatus n = hal().net();
        toggle(LV_SYMBOL_BLUETOOTH, "Bluetooth", n.bt_enabled, [](bool on) { hal().set_bt_enabled(on); });
        choice(LV_SYMBOL_CLOSE, "Turn off when unused",
               {{"5 min", 5}, {"10 min", 10}, {"30 min", 30}, {"Never", 0}}, power().bt_auto_off_min(),
               [](int v) { power().set_bt_auto_off_min(v); });
        list_.add(LV_SYMBOL_LIST, "Devices", "", [] { shell_open_app("bluetooth"); });
    }

    void display_page() {
        auto& p = power();
        percent(LV_SYMBOL_EYE_OPEN, "Brightness", p.user_brightness(), 5,
                [](int v) { power().set_user_brightness(v); });
        choice(LV_SYMBOL_EYE_CLOSE, "Dim screen after",
               {{"15 s", 15}, {"30 s", 30}, {"1 min", 60}, {"2 min", 120}, {"Never", 0}}, p.dim_after_s(),
               [](int v) { power().set_dim_after_s(v); });
        choice(LV_SYMBOL_POWER, "Screen off after",
               {{"30 s", 30}, {"1 min", 60}, {"2 min", 120}, {"5 min", 300}, {"10 min", 600}, {"Never", 0}},
               p.off_after_s(), [](int v) { power().set_off_after_s(v); });
        if (hal().imu().available) {
            toggle(LV_SYMBOL_UP, "Wake on pickup", p.wake_on_pickup(), [](bool on) { power().set_wake_on_pickup(on); });
            toggle(LV_SYMBOL_DOWN, "Screen off face down", p.sleep_face_down(),
                   [](bool on) { power().set_sleep_face_down(on); });
        }
        toggle(LV_SYMBOL_KEYBOARD, "Word suggestions", suggest_enabled(), [](bool on) { suggest_set_enabled(on); });
    }

    void sound_page() {
        auto& h = hal();
        percent(LV_SYMBOL_VOLUME_MAX, "Volume", h.volume(), 5, [](int v) { hal().set_volume(v); });
        toggle(LV_SYMBOL_MUTE, "Mute", h.muted(), [](bool on) { hal().set_muted(on); });
    }

    void battery_page() {
        auto& p = power();
        choice(LV_SYMBOL_CHARGE, "Power profile", {{"Saver", 0}, {"Balanced", 1}, {"Performance", 2}},
               int(p.profile()), [](int v) { power().set_profile(Profile(v)); });
        toggle(LV_SYMBOL_WIFI, "Wi-Fi off with screen", p.wifi_sleep(), [](bool on) { power().set_wifi_sleep(on); });
        toggle(LV_SYMBOL_BATTERY_2, "Time left in top bar", p.show_time_left(),
               [](bool on) { power().set_show_time_left(on); });
        if (app_find("battery")) list_.add(LV_SYMBOL_BATTERY_3, "Battery details", "", [] { shell_open_app("battery"); });
    }

    void remote_page() {
        int ts = tailscale_mode() == "always" ? 1 : tailscale_mode() == "off" ? 2 : 0;
        choice(LV_SYMBOL_SHUFFLE, "Tailscale", {{"When needed", 0}, {"Always on", 1}, {"Off", 2}}, ts, [](int v) {
            set_tailscale_mode(v == 1 ? "always" : v == 2 ? "off" : "needed");
            if (v == 1) hal().tailscale_start();
            if (v == 2) hal().tailscale_stop();
        });
        int sm = sync_mode() == "always" ? 1 : sync_mode() == "manual" ? 2 : 0;
        choice(LV_SYMBOL_LOOP, "Sync", {{"While charging", 0}, {"Always on", 1}, {"When asked", 2}}, sm,
               [](int v) { set_sync_mode(v == 1 ? "always" : v == 2 ? "manual" : "charging"); });
    }

    void power_page() {
        list_.add(LV_SYMBOL_REFRESH, "Restart", "", [] {
            shell_confirm("Restart the Cardputer?", "", "Restart", [] {
                shell_toast("Restarting...");
                hal().reboot();
            });
        });
        list_.add(LV_SYMBOL_POWER, "Shut down", "", [] {
            shell_confirm("Shut down the Cardputer?", "Press the power button to start it again.", "Shut down", [] {
                shell_toast("Shutting down...");
                hal().power_off();
            });
        });
    }

    // ---- Rows -----------------------------------------------------------

    static std::string secs(int s) {
        if (s <= 0) return "never";
        return s < 60 ? fmt("%d s", s) : fmt("%d min", s / 60);
    }

    void toggle(const char* icon, const char* text, bool on, std::function<void(bool)> set) {
        has_values_ = true;
        list_.add_choice(icon, text, {kOffOn[0], kOffOn[1]}, on ? 1 : 0,
                         [set = std::move(set)](int i) { set(i == 1); });
    }

    void choice(const char* icon, const char* text, std::vector<Choice> choices, int current,
                std::function<void(int)> set) {
        std::vector<std::string> texts;
        int at = 0;
        has_values_ = true;
        for (size_t i = 0; i < choices.size(); ++i) {
            texts.push_back(choices[i].text);
            if (choices[i].value == current) at = int(i);
        }
        list_.add_choice(icon, text, std::move(texts), at,
                         [choices, set = std::move(set)](int i) { set(choices[size_t(i)].value); });
    }

    // 0..100 in steps of `step` (Left/Right: one step).
    void percent(const char* icon, const char* text, int value, int step, std::function<void(int)> set) {
        has_values_ = true;
        std::vector<std::string> texts;
        for (int v = 0; v <= 100; v += step) texts.push_back(fmt("%d%%", v));
        list_.add_choice(icon, text, std::move(texts), (value + step / 2) / step,
                         [step, set = std::move(set)](int i) { set(i * step); });
    }

    lv_obj_t* root_ = nullptr;
    Page page_ = Page::Root;
    bool has_values_ = false;
    bool direct_ = false;       // opened straight at a section (from search)   // the page has value rows (the bar says Left/Right change them)
    ListView list_;
};
}  // namespace

std::unique_ptr<App> make_settings_app() { return std::make_unique<SettingsApp>(); }

}  // namespace zs
