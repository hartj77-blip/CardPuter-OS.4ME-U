// Game controls: which buttons are held right now, from the keyboard and any
// gamepads (USB or Bluetooth), as a libretro joypad bitmask.
//
// ZeroShell's own key handling sees one key at a time and only presses, which
// is right for apps and wrong for games (hold Right and press Jump). So this
// reads the input devices itself, alongside ZeroShell (nothing is grabbed).
// In the simulator there are no devices: press_key() holds a key for a moment.
//
//   Keyboard: W A S D or the arrows move, K = A, J = B, I = X, U = Y,
//             Q = L, E = R, Enter = Start, Space = Select (Tab too)
//             (X and Z also work as A and B on a full-size keyboard)
//   Gamepad:  as labelled; the Home/Guide button opens the menu
#pragma once

#include <atomic>
#include <cstdint>
#include <string>
#include <vector>

namespace zs::games {

class Pad {
public:
    ~Pad();
    // Open the keyboards and gamepads there are now. False if there are none
    // (the simulator): use press_key().
    bool open();
    void close();
    // Read what happened since the last call; look for new devices now and then.
    void poll();
    // libretro joypad bits (1 << retro::kA ...) held now.
    uint16_t buttons();
    // A gamepad's Home/Guide button was pressed since the last call.
    bool take_menu() { return menu_.exchange(false); }
    // Any input since the last call (keeps the screen awake for gamepads).
    bool take_activity() { return activity_.exchange(false); }
    bool has_devices() const { return !devs_.empty(); }
    int gamepads() const;

    // ZeroShell key (character or LV_KEY_*) when there are no input devices
    // (the simulator): held for 150 ms of LVGL's clock.
    void press_key(uint32_t key);

private:
    struct Dev {
        int fd = -1;
        std::string path;
        bool gamepad = false;
        int abs_min[2] = {0, 0}, abs_max[2] = {0, 0};   // left stick X, Y
    };
    void scan();
    void handle(Dev& d, uint16_t type, uint16_t code, int32_t value);

    std::vector<Dev> devs_;
    std::vector<uint8_t> keys_ = std::vector<uint8_t>(0x300, 0);   // evdev KEY_/BTN_ codes held
    int hat_x_ = 0, hat_y_ = 0, stick_x_ = 0, stick_y_ = 0;        // -1, 0, 1
    uint32_t last_scan_ = 0;
    std::atomic<bool> menu_{false}, activity_{false};
    // No devices: held until this lv_tick time (per joypad bit).
    std::atomic<uint32_t> sim_until_[16] = {};
};

}  // namespace zs::games
