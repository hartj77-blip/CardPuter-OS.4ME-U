#include "platform/keys.h"

namespace zs {
namespace {
// Linux input event codes (linux/input-event-codes.h), kept here so this
// file builds for the browser too.
enum : uint16_t {
    K_ESC = 1, K_1 = 2, K_0 = 11, K_MINUS = 12, K_EQUAL = 13, K_BACKSPACE = 14, K_TAB = 15,
    K_Q = 16, K_P = 25, K_LEFTBRACE = 26, K_RIGHTBRACE = 27, K_ENTER = 28, K_LEFTCTRL = 29,
    K_A = 30, K_L = 38, K_SEMICOLON = 39, K_APOSTROPHE = 40, K_GRAVE = 41, K_LEFTSHIFT = 42,
    K_BACKSLASH = 43, K_Z = 44, K_M = 50, K_COMMA = 51, K_DOT = 52, K_SLASH = 53,
    K_RIGHTSHIFT = 54, K_LEFTALT = 56, K_SPACE = 57, K_CAPSLOCK = 58, K_KPENTER = 96,
    K_RIGHTCTRL = 97, K_RIGHTALT = 100, K_HOME = 102, K_UP = 103, K_LEFT = 105, K_RIGHT = 106,
    K_END = 107, K_DOWN = 108, K_DELETE = 111, K_NEXT = 0x197, K_PREVIOUS = 0x19c,
    K_F1 = 59, K_F10 = 68, K_F11 = 87, K_F12 = 88, K_PAGEUP = 104, K_PAGEDOWN = 109, K_MUTE = 113,
    K_VOLUMEDOWN = 114, K_VOLUMEUP = 115, K_NEXTSONG = 163, K_PLAYPAUSE = 164, K_PREVIOUSSONG = 165,
    K_BRIGHTNESSDOWN = 224, K_BRIGHTNESSUP = 225, K_SYSRQ = 99, K_INSERT = 110, K_HELP = 138,
    K_PRINT = 210,
};

const char kRowQ[] = "qwertyuiop", kRowA[] = "asdfghjkl", kRowZ[] = "zxcvbnm";
const char kDigits[] = "1234567890", kDigitsShift[] = "!@#$%^&*()";

// Sym layer of the built-in keyboard: the code the driver sends (keymap1 in
// M5's cardputerzero overlay) and the symbol M5's console keymap gives it.
struct SymKey { uint16_t code; char ch; };
const SymKey kCardputerSym[] = {
    {26, '!'}, {27, '@'}, {39, '#'}, {40, '$'}, {41, '%'}, {43, '^'}, {51, '&'}, {52, '*'},
    {53, '('}, {94, ')'},                                                   // Sym + 1..0
    {55, '~'}, {69, '`'}, {70, '_'}, {71, '-'}, {72, '+'}, {73, '='}, {74, '['}, {75, ']'},
    {76, '{'}, {77, '}'},                                                   // Sym + Q..P
    {78, '+'},                                                              // Sym + Shift (keypad plus)
    {79, ';'}, {80, ':'}, {81, '\''}, {82, '"'}, {83, '<'}, {85, '>'}, {86, '\\'}, {89, '|'},  // Sym + A..L
    {90, ','}, {91, '.'}, {92, '/'}, {93, '?'},                             // Sym + V B N M
};

struct UsKey { uint16_t code; char plain, shifted; };
const UsKey kUsPunct[] = {
    {K_MINUS, '-', '_'}, {K_EQUAL, '=', '+'}, {K_LEFTBRACE, '[', '{'}, {K_RIGHTBRACE, ']', '}'},
    {K_SEMICOLON, ';', ':'}, {K_APOSTROPHE, '\'', '"'}, {K_GRAVE, '`', '~'}, {K_BACKSLASH, '\\', '|'},
    {K_COMMA, ',', '<'}, {K_DOT, '.', '>'}, {K_SLASH, '/', '?'},
};

uint32_t navigation(uint16_t code) {
    switch (code) {
        case K_UP: return kKeyUp;
        case K_DOWN: return kKeyDown;
        case K_LEFT: return kKeyLeft;
        case K_RIGHT: return kKeyRight;
        case K_ESC: return kKeyEsc;
        case K_DELETE: return kKeyDel;
        case K_BACKSPACE: return kKeyBackspace;
        case K_ENTER:
        case K_KPENTER: return kKeyEnter;
        case K_TAB:
        case K_NEXT: return kKeyNext;
        case K_PREVIOUS: return kKeyPrev;
        case K_HOME: return kKeyHome;
        case K_END: return kKeyEnd;
        case K_PAGEUP: return kKeyPageUp;
        case K_PAGEDOWN: return kKeyPageDown;
        case K_PLAYPAUSE: return kKeyPlayPause;
        case K_PREVIOUSSONG: return kKeyPrevTrack;
        case K_NEXTSONG: return kKeyNextTrack;
        case K_MUTE: return kKeyMute;
        case K_VOLUMEDOWN: return kKeyVolumeDown;
        case K_VOLUMEUP: return kKeyVolumeUp;
        case K_BRIGHTNESSDOWN: return kKeyBrightnessDown;
        case K_BRIGHTNESSUP: return kKeyBrightnessUp;
        // Fn + J (PRSC on the key) sends SysRq, as does a USB keyboard's
        // PrintScreen key; some keyboards send KEY_PRINT instead.
        case K_SYSRQ:
        case K_PRINT: return kKeyPrintScreen;
        case K_HELP: return kKeyHelp;
        case K_INSERT: return kKeyInsert;
        case K_F11: return kKeyF1 + 10;
        case K_F12: return kKeyF1 + 11;
        default:
            if (code >= K_F1 && code <= K_F10) return kKeyF1 + (code - K_F1);
            return 0;
    }
}

char letter(uint16_t code) {
    if (code >= K_Q && code <= K_P) return kRowQ[code - K_Q];
    if (code >= K_A && code <= K_L) return kRowA[code - K_A];
    if (code >= K_Z && code <= K_M) return kRowZ[code - K_Z];
    return 0;
}
}  // namespace

uint32_t fn_layer_key(uint32_t ch) {
    switch (ch) {
        case 'q': return kKeyPlayPause;
        case 'w': return kKeyPrevTrack;
        case 'e': return kKeyNextTrack;
        case 'u': return kKeyBrightnessDown;
        case 'i': return kKeyBrightnessUp;
        case 'o': return kKeyF1 + 10;
        case 'p': return kKeyF1 + 11;
        case 'a': return kKeyMute;
        case 's': return kKeyVolumeDown;
        case 'd': return kKeyVolumeUp;
        case 'f': return kKeyUp;
        case 'h': return kKeyHelp;
        case 'j': return kKeyPrintScreen;
        case 'b': return kKeyInsert;
        case 'k': return kKeyHome;
        case 'l': return kKeyPageUp;
        case 'z': return kKeyLeft;
        case 'x': return kKeyDown;
        case 'c': return kKeyRight;
        case 'n': return kKeyEnd;
        case 'm': return kKeyPageDown;
        default: return 0;
    }
}

uint32_t KeyTranslator::translate(uint16_t code) const {
    if (uint32_t nav = navigation(code)) return nav;
    // Ctrl + a letter or Space is a command key (kKeyCtrl). Other Ctrl and
    // Alt combinations are not text, and nothing uses them.
    if (alt_l_ || alt_r_) return 0;
    if (ctrl_l_ || ctrl_r_ || ctrl_once_) {
        if (char c = letter(code)) return ctrl_key(c);
        if (code == K_SPACE) return ctrl_key(' ');
        return 0;
    }
    bool shift = shift_l_ || shift_r_;
    if (char c = letter(code)) return uint32_t((shift != caps_) ? c - 'a' + 'A' : c);
    if (code >= K_1 && code <= K_0) return uint32_t(shift ? kDigitsShift[code - K_1] : kDigits[code - K_1]);
    if (code == K_SPACE) return ' ';
    if (layout_ == KeyLayout::Cardputer) {
        for (const SymKey& k : kCardputerSym)
            if (k.code == code) return uint32_t(k.ch);
        return 0;
    }
    for (const UsKey& k : kUsPunct)
        if (k.code == code) return uint32_t(shift ? k.shifted : k.plain);
    return 0;
}

void KeyTranslator::feed(uint16_t code, int32_t value) {
    if (value != 0 && value != 1) return;
    bool down = value == 1;
    switch (code) {
        case K_LEFTSHIFT: shift_l_ = down; return;
        case K_RIGHTSHIFT: shift_r_ = down; return;
        case K_LEFTCTRL:
        case K_RIGHTCTRL:
            // M5's driver makes Shift, Fn and Sym sticky but sends Ctrl as
            // a plain key; make it sticky here too, so tap Ctrl then Space
            // works as well as holding it.
            if (down && !ctrl_l_ && !ctrl_r_) ctrl_used_ = false;
            (code == K_LEFTCTRL ? ctrl_l_ : ctrl_r_) = down;
            if (!down && !ctrl_l_ && !ctrl_r_ && !ctrl_used_) ctrl_once_ = !ctrl_once_;
            return;
        case K_LEFTALT: alt_l_ = down; return;
        case K_RIGHTALT: alt_r_ = down; return;
        case K_CAPSLOCK: if (down) caps_ = !caps_; return;
        default: break;
    }
    if (down) {
        uint32_t key = translate(code);
        if (ctrl_l_ || ctrl_r_) ctrl_used_ = true;
        ctrl_once_ = false;   // a tapped Ctrl is for this key only
        if (!key) return;
        if (held_code_) out_.push_back({held_key_, false});
        out_.push_back({key, true});
        held_code_ = code;
        held_key_ = key;
    } else if (code == held_code_) {
        out_.push_back({held_key_, false});
        held_code_ = 0;
    }
}

void KeyTranslator::reset() {
    if (held_code_) out_.push_back({held_key_, false});
    held_code_ = 0;
    shift_l_ = shift_r_ = ctrl_l_ = ctrl_r_ = alt_l_ = alt_r_ = false;
    ctrl_once_ = ctrl_used_ = false;
}

bool KeyTranslator::next(KeyEvent& e) {
    if (out_.empty()) return false;
    e = out_.front();
    out_.pop_front();
    return true;
}

}  // namespace zs
