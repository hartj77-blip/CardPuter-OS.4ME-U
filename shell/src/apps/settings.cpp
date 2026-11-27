// Settings: one numbered list of sections (docs/UX.md), each a list of its
// own. Values change with Left/Right (or Enter). Wi-Fi, Bluetooth and Date
// & time are sections with a file of their own (apps/settings_sections.h); Battery &
// power also shows the fuel gauge's readings; the sections that are apps
// (Update, About) open them, and Esc there comes back here.
#include <algorithm>
#include <ctime>
#include <functional>
#include <string>
#include <vector>

#include "apps/common.h"
#include "apps/settings_sections.h"
#include "ui/list_view.h"
#include "ui/power.h"
#include "ui/remote.h"
#include "ui/suggest_bar.h"
#include "ui/sync.h"

namespace zs {
namespace {
using namespace theme;

enum class Page { Root, Wifi, Bluetooth, Time, Display, Sound, Battery, Remote, Running, Power };

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
            {"wifi", Page::Wifi},     {"bluetooth", Page::Bluetooth}, {"time", Page::Time},
            {"display", Page::Display}, {"sound", Page::Sound},       {"battery", Page::Battery},
            {"remote", Page::Remote},   {"running", Page::Running},   {"power", Page::Power}};
        std::string arg = shell_app_arg();
        for (auto& [name, page] : kSections)
            if (arg == name) {
                direct_ = true;   // before show(): the bar says Esc goes Back
                show(page);
                return;
            }
        show(Page::Root);
    }

    bool on_key(uint32_t key) override { return section_ ? section_->on_key(key) : list_.key(key); }

    void tick() override {
        if (section_) section_->tick();
        ++ticks_;
        if (page_ == Page::Running && ticks_ % 20 == 0) refresh_running();   // every ~2 s
        if (page_ == Page::Battery && ticks_ % 10 == 0) refresh_battery();   // every second
    }

    bool on_back() override {
        if (section_ && section_->on_back()) return true;   // Wi-Fi: from the password to the networks
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
        free_ = nullptr;
        battery_state_ = nullptr;
        section_.reset();   // while its widgets still exist, as the shell closes an app
        list_.reset();
        lv_obj_clean(root_);
        lv_group_remove_all_objs(group);
        if (p == Page::Wifi || p == Page::Bluetooth || p == Page::Time) {
            // The section draws the screen and sets its own actions.
            const char* back = direct_ ? "Back" : "Settings";
            section_ = p == Page::Wifi ? make_wifi_section(back)
                       : p == Page::Bluetooth ? make_bluetooth_section(back)
                                              : make_datetime_section(back);
            section_->group = group;
            section_->build(root_);
            return;
        }
        static const char* const kNames[] = {"", "", "", "", "Display", "Sound", "Battery & power",
                                             "Remote & sync", "Running programs", "Restart or shut down"};
        if (p == Page::Root) {
            list_.build(root_, 2, 2, kScreenW - 4, kBodyH - 4);
        } else {
            uint32_t color = kMuted;
            std::string state = p == Page::Battery ? battery_state(color) : "";
            lv_obj_t* s = settings_heading(root_, kNames[int(p)], state, color);
            if (p == Page::Battery) battery_state_ = s;
            list_.build(root_, 2, 18, kScreenW - 4, kBodyH - 20);
        }
        switch (p) {
            case Page::Root: root_page(); break;
            case Page::Wifi:
            case Page::Bluetooth:
            case Page::Time: break;   // sections, above
            case Page::Display: display_page(); break;
            case Page::Sound: sound_page(); break;
            case Page::Battery: battery_page(); break;
            case Page::Remote: remote_page(); break;
            case Page::Running: running_page(); break;
            case Page::Power: power_page(); break;
        }
        list_.finish();
        if (p == Page::Running) {
            set_running_actions();
        } else if (p == Page::Root) {
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
        // Rows on the root page, as root_page() adds them.
        static const std::pair<Page, int> kRows[] = {{Page::Wifi, 0},    {Page::Bluetooth, 1}, {Page::Display, 2},
                                                     {Page::Sound, 3},   {Page::Battery, 4},   {Page::Time, 5},
                                                     {Page::Remote, 6},  {Page::Power, 8},     {Page::Running, 10}};
        int row = 0;
        for (auto& [page, r] : kRows)
            if (page == from) row = r;
        list_.focus(row);
    }

    // ---- Root -----------------------------------------------------------

    void root_page() {
        auto& h = hal();
        NetStatus n = h.net();
        auto& p = power();
        std::string wifi = !n.wifi_enabled ? "Off" : n.wifi_connected ? n.wifi_ssid : "Not connected";
        static const char* const kProfiles[] = {"Saver", "Balanced", "Performance"};
        std::string version = h.update().current;

        list_.add(LV_SYMBOL_WIFI, "Wi-Fi", wifi, [this] { open(Page::Wifi); });
        list_.add(LV_SYMBOL_BLUETOOTH, "Bluetooth", n.bt_enabled ? "On" : "Off", [this] { open(Page::Bluetooth); });
        list_.add(LV_SYMBOL_EYE_OPEN, "Display", fmt("%d%%, dim %s", p.user_brightness(), secs(p.dim_after_s()).c_str()),
                  [this] { open(Page::Display); });
        list_.add(LV_SYMBOL_VOLUME_MAX, "Sound", h.muted() ? "Muted" : fmt("%d%%", h.volume()),
                  [this] { open(Page::Sound); });
        int charge = h.battery().percent;
        std::string profile = kProfiles[std::clamp(int(p.profile()), 0, 2)];
        list_.add(LV_SYMBOL_BATTERY_3, "Battery & power", charge < 0 ? profile : fmt("%d%%, ", charge) + profile,
                  [this] { open(Page::Battery); });
        time_t now = time(nullptr);
        tm lt{};
        localtime_r(&now, &lt);
        char date[24];
        strftime(date, sizeof date, "%a %-d %b", &lt);
        list_.add(ZS_ICON_CLOCK, "Date & time", date, [this] { open(Page::Time); });
        list_.add(LV_SYMBOL_SHUFFLE, "Remote & sync", "", [this] { open(Page::Remote); });
        list_.add(LV_SYMBOL_DOWNLOAD, "Software update", version.empty() ? "" : version,
                  [] { shell_open_app("update"); });
        list_.add(LV_SYMBOL_POWER, "Restart or shut down", "", [this] { open(Page::Power); });
        // After it, so Restart or shut down stays on 9.
        list_.add(LV_SYMBOL_LIST, "About this device", "", [] { shell_open_app("sysmon"); });
        size_t running = h.processes().size();
        list_.add(LV_SYMBOL_PLAY, "Running programs", running ? std::to_string(running) : "None",
                  [this] { open(Page::Running); });
        list_.set_empty_text("No setting by that name");
    }

    // ---- Sections -------------------------------------------------------

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

    // ---- Battery & power ------------------------------------------------
    // The settings first, then what the fuel gauge (BQ27220) reads. The
    // charge and the time left are in the heading. All of it follows every
    // second without rebuilding the list, so the focus doesn't jump.

    // "78%, 2h 32m left", "78%, full in 1h 05m", "100%, plugged in".
    static std::string battery_state(uint32_t& color) {
        BatteryInfo b = hal().battery();
        color = b.charging ? kGood : b.percent < 0 ? kMuted : b.percent < 15 ? kBad : b.percent < 30 ? kWarn : kMuted;
        if (b.percent < 0) return "No battery gauge";
        int left = power().minutes_left();
        std::string s = fmt("%d%%, ", b.percent);
        if (left >= 0) return s + (b.charging ? fmt("full in %dh %02dm", left / 60, left % 60)
                                              : fmt("%dh %02dm left", left / 60, left % 60));
        return s + (b.charging ? "charging" : b.external_power ? "plugged in" : "on battery");
    }

    // The readings, in the order battery_page() lists them.
    static std::vector<std::string> battery_readings() {
        BatteryInfo b = hal().battery();
        int mw = power().avg_power_mw();
        std::string health = b.health_percent < 0 ? "--"
                             : b.charge_full_mah > 0 ? fmt("%d%% (%d mAh)", b.health_percent, b.charge_full_mah)
                                                     : fmt("%d%%", b.health_percent);
        return {mw > 0 ? fmt("%.2f W", mw / 1000.0) : "--",
                fmt("%.2f V", b.voltage_mv / 1000.0),
                fmt("%d mA", b.current_ma),
                fmt("%.1f C", b.temp_c10 / 10.0),
                health,
                b.cycle_count < 0 ? "--" : fmt("%d", b.cycle_count)};
    }

    void battery_page() {
        auto& p = power();
        choice(LV_SYMBOL_CHARGE, "Power profile", {{"Saver", 0}, {"Balanced", 1}, {"Performance", 2}},
               int(p.profile()), [](int v) { power().set_profile(Profile(v)); });
        toggle(LV_SYMBOL_WIFI, "Wi-Fi off with screen", p.wifi_sleep(), [](bool on) { power().set_wifi_sleep(on); });
        toggle(LV_SYMBOL_BATTERY_2, "Time left in top bar", p.show_time_left(),
               [](bool on) { power().set_show_time_left(on); });
        // The low-battery saver overrides the profile above while it is on.
        low_saver_ = p.effective_profile() != p.profile();
        if (low_saver_) list_.add(LV_SYMBOL_WARNING, "Battery low: running as Saver", "", nullptr);
        static const char* const kReadings[] = {"Power draw", "Voltage", "Current", "Temperature", "Health", "Cycles"};
        std::vector<std::string> now = battery_readings();
        for (size_t i = 0; i < now.size(); ++i) {
            int row = list_.add(nullptr, kReadings[i], now[i], nullptr);
            if (i == 0) readings_row_ = row;
        }
    }

    void refresh_battery() {
        auto& p = power();
        if ((p.effective_profile() != p.profile()) != low_saver_) {   // its row comes or goes
            int keep = list_.focused();
            show(Page::Battery);
            if (keep > 0 && keep < list_.size()) list_.focus(keep);
            return;
        }
        std::vector<std::string> now = battery_readings();
        for (size_t i = 0; i < now.size(); ++i) list_.set_value(readings_row_ + int(i), now[i]);
        if (battery_state_) {
            uint32_t color = kMuted;
            lv_label_set_text(battery_state_, battery_state(color).c_str());
            lv_obj_set_style_text_color(battery_state_, c(color), 0);
        }
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

    // ---- Running programs -----------------------------------------------
    // Everything running as the user (hal().processes()): programs started
    // from Terminal, M5Stack apps that kept going in the background, a
    // player. Enter stops one after asking. The values follow every ~2 s
    // without rebuilding the list, so the focus doesn't jump.

    static std::string usage(const ProcInfo& p) {
        std::string s = p.rss_kb >= 10240 ? fmt("%d MB", p.rss_kb / 1024) : fmt("%.1f MB", p.rss_kb / 1024.0);
        if (p.cpu_percent > 0) s += fmt("  %d%%", p.cpu_percent);
        return s;
    }

    void running_page() {
        procs_ = hal().processes();
        SystemInfo si = hal().system();
        lv_obj_t* free = label(root_, fmt("%d MB free", si.mem_avail_mb).c_str(), &zs_font_12, kMuted);
        lv_obj_align(free, LV_ALIGN_TOP_RIGHT, -8, 2);
        free_ = free;
        for (const ProcInfo& p : procs_) {
            int pid = p.pid;
            std::string name = p.name;
            list_.add(LV_SYMBOL_PLAY, p.name, usage(p), [this, pid, name] { ask_stop(pid, name); });
        }
        list_.set_empty_text("Nothing else is running");
    }

    void set_running_actions() {
        bool any = !procs_.empty();
        std::vector<Action> a;
        a.push_back({"Stop", 0, "Enter", nullptr, any});
        a.push_back({"Stop all", 'k', nullptr, [this] { stop_all(); }, any, true,
                     fmt("Stop all %d programs?", int(procs_.size()))});
        a.push_back({"Refresh", 'r', nullptr, [this] { show(Page::Running); }});
        a.push_back({direct_ ? "Back" : "Settings", 0, "Esc", nullptr});
        shell_set_actions(std::move(a));
    }

    void refresh_running() {
        std::vector<ProcInfo> now = hal().processes();
        for (size_t i = 0; i < procs_.size(); ++i) {
            auto it = std::find_if(now.begin(), now.end(), [&](const ProcInfo& p) { return p.pid == procs_[i].pid; });
            if (it == now.end()) {
                list_.set_value(int(i), "Ended");
                list_.set_value_color(int(i), kMuted);
            } else {
                list_.set_value(int(i), usage(*it));
            }
        }
        if (free_) lv_label_set_text(free_, fmt("%d MB free", hal().system().mem_avail_mb).c_str());
    }

    void ask_stop(int pid, const std::string& name) {
        std::string cmd;
        for (auto& p : procs_)
            if (p.pid == pid) cmd = p.command;
        if (cmd.size() > 44) cmd = cmd.substr(0, 43) + "~";
        shell_confirm("Stop " + name + "?", cmd + "\nAnything it hasn't saved is lost.", "Stop", [this, pid, name] {
            bool ok = hal().process_stop(pid);
            shell_toast(ok ? "Stopped " + name : "Couldn't stop " + name);
            show(Page::Running);
        });
    }

    // Stop all (Ctrl + K): the shell asks first, as for every danger action.
    void stop_all() {
        int stopped = 0;
        for (auto& p : procs_)
            if (hal().process_stop(p.pid)) ++stopped;
        shell_toast(stopped == 1 ? "Stopped 1 program" : fmt("Stopped %d programs", stopped));
        show(Page::Running);
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
    bool has_values_ = false;   // the page has value rows (the bar says Left/Right change them)
    std::vector<ProcInfo> procs_;   // Running programs, in list order
    lv_obj_t* free_ = nullptr;      // its "N MB free" label
    lv_obj_t* battery_state_ = nullptr;   // Battery & power: the charge and time left, in the heading
    int readings_row_ = 0;                // ... its first reading's row
    bool low_saver_ = false;              // ... the low-battery row is showing
    int ticks_ = 0;
    bool direct_ = false;            // opened straight at a section (from search)
    std::unique_ptr<App> section_;   // Wi-Fi or Bluetooth, while it has the screen
    ListView list_;
};
}  // namespace

std::unique_ptr<App> make_settings_app() { return std::make_unique<SettingsApp>(); }

lv_obj_t* settings_heading(lv_obj_t* root, const char* section, const std::string& state, uint32_t color) {
    using namespace theme;
    lv_obj_t* h = label(root, (std::string("Settings  " LV_SYMBOL_RIGHT "  ") + section).c_str(), &zs_font_12, kMuted);
    lv_obj_set_pos(h, 8, 2);
    if (state.empty()) return nullptr;
    // What is left of the line, so a long network name ends in dots.
    lv_obj_update_layout(h);
    lv_obj_t* s = label(root, state.c_str(), &zs_font_12, color);
    lv_obj_set_size(s, kScreenW - 26 - lv_obj_get_width(h), 14);
    lv_label_set_long_mode(s, LV_LABEL_LONG_DOT);
    lv_obj_set_style_text_align(s, LV_TEXT_ALIGN_RIGHT, 0);
    lv_obj_align(s, LV_ALIGN_TOP_RIGHT, -8, 2);
    return s;
}

}  // namespace zs
