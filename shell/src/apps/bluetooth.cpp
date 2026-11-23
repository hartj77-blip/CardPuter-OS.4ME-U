// Bluetooth: find, pair, connect and forget devices - keyboards, gamepads,
// speakers, headphones (BlueZ). Opened from Settings or the home screen.
//
//   Enter  pair and connect / disconnect.  Tab: S scan (8 s), D forget
//   Typing finds a device (ui/list_view.h).
//
// Pairing is "just works" (nothing to type), which keyboards, gamepads and
// speakers accept. A paired device is trusted, so it reconnects by itself
// when it's switched on. Scanning and connecting wait on bluetoothctl (8 and
// up to 40 s): a toast says so before the screen stops updating, as in Car.
#include <unistd.h>

#include "apps/common.h"
#include "ui/list_view.h"

namespace zs {
namespace {
using namespace theme;

class BluetoothApp : public App {
public:
    void build(lv_obj_t* root) override {
        root_ = root;
        devices_ = hal().bt_devices();
        show();
    }

    bool on_key(uint32_t key) override { return list_.key(key); }

    void forget() {
        int i = focused_device();
        if (i < 0 || !devices_[size_t(i)].paired) {
            shell_toast("Pick a paired device first");
            return;
        }
        BtDevice d = devices_[size_t(i)];
        hal().bt_forget(d.address);
        shell_toast("Forgot " + d.name);
        devices_ = hal().bt_devices();
        show();
    }

    // Connections change by themselves (a keyboard switched off): look every
    // few seconds while the list is open.
    void tick() override {
        if (++n_ < 30) return;   // tick() runs about every 100 ms
        n_ = 0;
        std::vector<BtDevice> now = hal().bt_devices();
        bool same = now.size() == devices_.size();
        for (size_t i = 0; same && i < now.size(); ++i)
            same = now[i].address == devices_[i].address && now[i].connected == devices_[i].connected &&
                   now[i].paired == devices_[i].paired;
        if (same) return;
        devices_ = std::move(now);
        show();
    }

private:
    bool bt_on() { return hal().net().bt_enabled; }

    void show() {
        int keep = list_.focused();
        lv_obj_clean(root_);
        lv_group_remove_all_objs(group);
        bool on = bt_on();

        Action forget_act{"Forget", 'd', nullptr, [this] { forget(); }, on};
        forget_act.danger = true;
        forget_act.confirm = "Forget this device?";
        shell_set_actions({{on ? "Connect" : "Turn on", 0, "Enter", nullptr},
                           {"Scan for devices", 's', nullptr, [this] { scan(); }},
                           forget_act});

        int connected = 0;
        for (auto& d : devices_) connected += d.connected;
        std::string head = !on ? "Bluetooth is off"
                           : connected ? fmt("%d connected", connected)
                                       : "Bluetooth is on";
        lv_obj_t* hdr = label(root_, head.c_str(), &zs_font_12, on ? kGood : kMuted);
        lv_obj_set_pos(hdr, 8, 4);

        list_.build(root_, 2, 20, kScreenW - 4, kBodyH - 21);
        rows_.clear();
        if (!on) {
            add_row(LV_SYMBOL_BLUETOOTH, "Turn on Bluetooth", "", kTurnOn);
        } else {
            for (size_t i = 0; i < devices_.size(); ++i) {
                const BtDevice& d = devices_[i];
                add_row(d.connected ? LV_SYMBOL_OK : LV_SYMBOL_BLUETOOTH, d.name,
                        d.connected ? "connected" : d.paired ? "paired" : "", int(i));
            }
            add_row(LV_SYMBOL_REFRESH, "Scan for devices", "", kScan);
        }
        list_.finish();
        if (devices_.empty() && on) {
            lv_obj_t* tip = label(root_, "Put the keyboard, gamepad or speaker in pairing mode, then scan.",
                                  &zs_font_12, kMuted);
            lv_obj_set_width(tip, kScreenW - 20);
            lv_label_set_long_mode(tip, LV_LABEL_LONG_WRAP);
            lv_obj_set_pos(tip, 10, 50);
        }
        if (keep >= 0) list_.focus(keep);
    }

    static constexpr int kTurnOn = -1, kScan = -2;

    void add_row(const char* icon, const std::string& text, const std::string& value, int what) {
        int row = int(rows_.size());
        list_.add(icon, text, value, [this, row] { activate(row); });
        rows_.push_back(what);
    }

    int focused_row() { return list_.focused(); }
    int focused_device() {
        int r = focused_row();
        if (r < 0 || r >= int(rows_.size())) return -1;
        int what = rows_[size_t(r)];
        return what >= 0 && what < int(devices_.size()) ? what : -1;
    }

    // These wait on Bluetooth; say so before the screen stops updating.
    void busy(const std::string& what) {
        shell_toast(what);
        lv_refr_now(nullptr);
    }

    void scan() {
        if (!bt_on()) hal().set_bt_enabled(true);
        busy("Scanning for 8 seconds...");
        hal().bt_scan();
        devices_ = hal().bt_devices();
        show();
    }

    void activate(int row) {
        if (row < 0 || row >= int(rows_.size())) return;
        int what = rows_[size_t(row)];
        if (what == kTurnOn) {
            hal().set_bt_enabled(true);
            busy("Turning on Bluetooth...");
            usleep(1500 * 1000);   // bluetoothd powers the adapter a moment after
        } else if (what == kScan) {
            scan();
            return;
        } else if (what >= 0 && what < int(devices_.size())) {
            BtDevice d = devices_[size_t(what)];
            if (d.connected) {
                busy("Disconnecting...");
                hal().bt_disconnect(d.address);
                shell_toast("Disconnected " + d.name);
            } else {
                busy(d.paired ? "Connecting..." : "Pairing...");
                std::string error;
                if (hal().bt_connect(d.address, error)) shell_toast("Connected " + d.name);
                else shell_toast(error);
            }
        }
        devices_ = hal().bt_devices();
        show();
    }

    lv_obj_t* root_ = nullptr;
    std::vector<BtDevice> devices_;
    std::vector<int> rows_;   // per list row: a device index, kTurnOn or kScan
    ListView list_;
    int n_ = 0;
};
}  // namespace

std::unique_ptr<App> make_bluetooth_app() { return std::make_unique<BluetoothApp>(); }

}  // namespace zs
