// Bluetooth: find, pair, connect and forget devices - keyboards, gamepads,
// speakers, headphones (BlueZ). Opened from Settings or the home screen.
//
//   Enter  pair and connect / disconnect     S  scan (8 s)     X  forget
//
// Pairing is "just works" (nothing to type), which keyboards, gamepads and
// speakers accept. A paired device is trusted, so it reconnects by itself
// when it's switched on. Scanning and connecting wait on bluetoothctl (8 and
// up to 40 s): a toast says so before the screen stops updating, as in Car.
#include <unistd.h>

#include "apps/common.h"

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

    ~BluetoothApp() override { lv_async_call_cancel(activate_cb, this); }

    bool on_key(uint32_t key) override {
        if (key == 's' || key == 'S') {
            scan();
            return true;
        }
        if (key == 'x' || key == 'X' || key == LV_KEY_DEL) {
            int i = focused_device();
            if (i >= 0 && devices_[size_t(i)].paired) {
                BtDevice d = devices_[size_t(i)];
                hal().bt_forget(d.address);
                shell_toast("Forgot " + d.name);
                devices_ = hal().bt_devices();
                show();
            }
            return true;
        }
        return false;
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
        int keep = focused_row();
        lv_obj_clean(root_);
        lv_group_remove_all_objs(group);
        bool on = bt_on();

        int connected = 0;
        for (auto& d : devices_) connected += d.connected;
        std::string head = !on ? "Bluetooth is off"
                           : connected ? fmt("%d connected", connected)
                                       : "Bluetooth is on";
        lv_obj_t* hdr = label(root_, head.c_str(), &zs_font_12, on ? kGood : kMuted);
        lv_obj_set_pos(hdr, 8, 4);
        lv_obj_t* hint = label(root_, on ? "S scan   X forget" : "", &zs_font_12, kMuted);
        lv_obj_align(hint, LV_ALIGN_TOP_RIGHT, -8, 4);

        lv_obj_t* list = make_list(root_, 4, 22, kScreenW - 8, kContentH - 26);
        rows_.clear();
        buttons_.clear();
        if (!on) {
            add_row(list, LV_SYMBOL_BLUETOOTH, "Turn on Bluetooth", kTurnOn);
        } else {
            for (size_t i = 0; i < devices_.size(); ++i) {
                const BtDevice& d = devices_[i];
                std::string text = d.name + (d.connected ? "   connected" : d.paired ? "   paired" : "");
                add_row(list, d.connected ? LV_SYMBOL_OK : LV_SYMBOL_BLUETOOTH, text, int(i));
            }
            add_row(list, LV_SYMBOL_REFRESH, "Scan for devices   S", kScan);
            if (devices_.empty())
                lv_list_add_text(list, "Put the keyboard, gamepad or speaker in pairing mode, then scan.");
        }
        finish_list(list);
        if (keep >= 0 && keep < int(buttons_.size())) lv_group_focus_obj(buttons_[size_t(keep)]);
    }

    static constexpr int kTurnOn = -1, kScan = -2;

    void add_row(lv_obj_t* list, const char* icon, const std::string& text, int what) {
        lv_obj_t* b = list_button(list, icon, text.c_str(), on_click, this);
        lv_obj_set_user_data(b, (void*)(intptr_t)rows_.size());
        rows_.push_back(what);
        buttons_.push_back(b);
    }

    int focused_row() {
        lv_obj_t* f = lv_group_get_focused(group);
        for (size_t i = 0; i < buttons_.size(); ++i)
            if (buttons_[i] == f) return int(i);
        return -1;
    }
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

    static void on_click(lv_event_t* e) {
        auto* self = static_cast<BluetoothApp*>(lv_event_get_user_data(e));
        self->pending_ = int((intptr_t)lv_obj_get_user_data(lv_event_get_target_obj(e)));
        // Deferred: activating rebuilds the list, which deletes the clicked button.
        lv_async_call(activate_cb, self);
    }
    static void activate_cb(void* p) {
        auto* s = static_cast<BluetoothApp*>(p);
        s->activate(s->pending_);
    }

    lv_obj_t* root_ = nullptr;
    std::vector<BtDevice> devices_;
    std::vector<int> rows_;            // per list row: a device index, kTurnOn or kScan
    std::vector<lv_obj_t*> buttons_;   // per list row
    int pending_ = -1;
    int n_ = 0;
};
}  // namespace

std::unique_ptr<App> make_bluetooth_app() { return std::make_unique<BluetoothApp>(); }

}  // namespace zs
