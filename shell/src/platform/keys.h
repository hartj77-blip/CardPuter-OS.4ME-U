// Turns Linux key events (evdev codes) into the keys ZeroShell understands:
// characters, and LVGL's navigation keys. No LVGL or kernel headers here, so
// it builds and is tested on any machine (shell/tests/keys_test.cpp).
#pragma once

#include <cstdint>
#include <deque>

namespace zs {

struct KeyEvent {
    uint32_t key;   // a character, or one of the kKey* values below
    bool pressed;
};

// Same values as LVGL's LV_KEY_* (checked in platform.cpp).
constexpr uint32_t kKeyUp = 17, kKeyDown = 18, kKeyRight = 19, kKeyLeft = 20, kKeyEsc = 27,
                   kKeyDel = 127, kKeyBackspace = 8, kKeyEnter = 10, kKeyNext = 9, kKeyPrev = 11,
                   kKeyHome = 2, kKeyEnd = 3;

// Keys LVGL has no name for: the Fn layer's media, volume and brightness
// keys, Page Up/Down and F1-F12. Unicode private use values, so they can't be
// mistaken for a character. The shell consumes them all (a text box would
// otherwise type them).
constexpr uint32_t kKeyPlayPause = 0xE000, kKeyPrevTrack = 0xE001, kKeyNextTrack = 0xE002,
                   kKeyMute = 0xE003, kKeyVolumeDown = 0xE004, kKeyVolumeUp = 0xE005,
                   kKeyBrightnessDown = 0xE006, kKeyBrightnessUp = 0xE007, kKeyPageUp = 0xE008,
                   kKeyPageDown = 0xE009, kKeyF1 = 0xE011;   // F1..F12 = kKeyF1 + 0..11
constexpr bool is_shell_key(uint32_t key) { return key >= kKeyPlayPause && key < kKeyF1 + 12; }

// The key the built-in keyboard's Fn layer puts on a letter (M5's
// cardputerzero overlay, keymap2), for screens that use the Fn layer without
// Fn held: f/z/x/c are arrows, q/w/e media, a/s/d mute and volume, u/i
// brightness, k/n Home/End, l/m Page Up/Down, o/p F11/F12. 0 = nothing there.
uint32_t fn_layer_key(uint32_t ch);

enum class KeyLayout {
    // The built-in keyboard, with M5's tca8418_keypad_m5stack driver. The
    // driver handles Sym and Fn itself and sends a different key code for
    // each layer; on the Sym layer those codes are stand-ins that M5's
    // console keymap (tca8418_keypad_m5stack_keymap.map) turns into symbols.
    Cardputer,
    // An ordinary (USB) keyboard, US layout.
    Us,
};

class KeyTranslator {
public:
    explicit KeyTranslator(KeyLayout layout = KeyLayout::Cardputer) : layout_(layout) {}

    // One EV_KEY event: value 1 = down, 0 = up, 2 = kernel repeat (ignored;
    // the shell repeats held keys itself).
    void feed(uint16_t code, int32_t value);
    // The kernel dropped events (SYN_DROPPED): forget what is held.
    void reset();

    bool pending() const { return !out_.empty(); }
    bool next(KeyEvent& e);

    // The key for a code with the modifiers as they are now; 0 = none.
    uint32_t translate(uint16_t code) const;

private:
    KeyLayout layout_;
    bool shift_l_ = false, shift_r_ = false, ctrl_l_ = false, ctrl_r_ = false, alt_l_ = false, alt_r_ = false;
    bool caps_ = false;
    // LVGL's keypad only notices a key going down after everything was up,
    // so only one key is "down" at a time: a second key pressed before the
    // first is let go (normal when typing fast) releases the first.
    uint16_t held_code_ = 0;
    uint32_t held_key_ = 0;
    std::deque<KeyEvent> out_;
};

}  // namespace zs
