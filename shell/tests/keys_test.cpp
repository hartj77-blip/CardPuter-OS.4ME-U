// Feeds the key translator the events M5's keyboard driver sends and checks
// what ZeroShell would type. Build and run: shell/tests/run.sh
#include <cstdio>
#include <string>
#include <vector>

#include "platform/keys.h"

using namespace zs;

namespace {
int g_failed = 0;

struct Ev { uint16_t code; int value; };

// Everything typed (key presses only), as text; navigation keys as <n>.
std::string typed(KeyTranslator& t, const std::vector<Ev>& evs) {
    std::string out;
    for (const Ev& e : evs) t.feed(e.code, e.value);
    KeyEvent k;
    bool down = false;
    while (t.next(k)) {
        if (k.pressed && down) { out += "<two keys down>"; }
        down = k.pressed;
        if (!k.pressed) continue;
        if (k.key >= 32 && k.key < 127) out += char(k.key);
        else out += "<" + std::to_string(k.key) + ">";
    }
    return out;
}

void check(const char* what, const std::string& got, const std::string& want) {
    if (got == want) return;
    std::printf("FAIL %s: got \"%s\", want \"%s\"\n", what, got.c_str(), want.c_str());
    ++g_failed;
}

std::vector<Ev> taps(const std::vector<uint16_t>& codes) {
    std::vector<Ev> v;
    for (uint16_t c : codes) { v.push_back({c, 1}); v.push_back({c, 0}); }
    return v;
}
}  // namespace

int main() {
    const uint16_t SHIFT = 42, CTRL = 29, SYM = 0x17f, FN = 0x185;
    {
        KeyTranslator t;
        check("letters", typed(t, taps({35, 18, 38, 38, 24})), "hello");
        check("every letter", typed(t, taps({16, 17, 18, 19, 20, 21, 22, 23, 24, 25, 30, 31, 32, 33, 34,
                                             35, 36, 37, 38, 44, 45, 46, 47, 48, 49, 50})),
              "qwertyuiopasdfghjklzxcvbnm");
        check("digits and space", typed(t, taps({2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 57})), "1234567890 ");
        check("enter esc tab backspace", typed(t, taps({28, 1, 15, 14})), "<10><27><9><8>");
    }
    {
        // Shift: the driver sends KEY_LEFTSHIFT down, the letter, then up.
        KeyTranslator t;
        check("shift", typed(t, {{SHIFT, 1}, {35, 1}, {35, 0}, {SHIFT, 0}, {23, 1}, {23, 0}}), "Hi");
        check("shift digit", typed(t, {{SHIFT, 1}, {2, 1}, {2, 0}, {SHIFT, 0}}), "!");
    }
    {
        // Sym layer: the driver sends KEY_CD for Sym, then stand-in codes.
        KeyTranslator t;
        check("sym digits row", typed(t, {{SYM, 1}, {26, 1}, {26, 0}, {27, 1}, {27, 0}, {39, 1}, {39, 0},
                                          {40, 1}, {40, 0}, {41, 1}, {41, 0}, {43, 1}, {43, 0}, {51, 1}, {51, 0},
                                          {52, 1}, {52, 0}, {53, 1}, {53, 0}, {94, 1}, {94, 0}, {SYM, 0}}),
              "!@#$%^&*()");
        check("sym q row", typed(t, taps({55, 69, 70, 71, 72, 73, 74, 75, 76, 77})), "~`_-+=[]{}");
        check("sym a row", typed(t, taps({79, 80, 81, 82, 83, 85, 86, 89})), ";:'\"<>\\|");
        check("sym z row", typed(t, taps({90, 91, 92, 93})), ",./?");
    }
    {
        // Fn layer: arrows on F Z X C, Delete on Backspace, Home and End.
        KeyTranslator t;
        check("fn arrows", typed(t, {{FN, 1}, {103, 1}, {103, 0}, {105, 1}, {105, 0}, {108, 1}, {108, 0},
                                     {106, 1}, {106, 0}, {111, 1}, {111, 0}, {102, 1}, {102, 0}, {107, 1},
                                     {107, 0}, {FN, 0}}),
              "<17><20><18><19><127><2><3>");
        check("fn keys we don't use", typed(t, taps({59, 113, 114, 115, 224, 225})), "");
    }
    {
        // Fast typing: the next key goes down before the last one is up.
        KeyTranslator t;
        check("rollover", typed(t, {{30, 1}, {31, 1}, {30, 0}, {32, 1}, {31, 0}, {32, 0}}), "asd");
        // Kernel repeat events are ignored.
        check("repeat", typed(t, {{30, 1}, {30, 2}, {30, 2}, {30, 0}}), "a");
        check("ctrl", typed(t, {{CTRL, 1}, {46, 1}, {46, 0}, {CTRL, 0}, {46, 1}, {46, 0}}), "c");
    }
    {
        // A held key is released once, and only by its own release.
        KeyTranslator t;
        t.feed(30, 1);
        KeyEvent k;
        bool ok = t.next(k) && k.pressed && k.key == 'a' && !t.pending();
        t.feed(31, 0);
        ok = ok && !t.pending();
        t.feed(30, 0);
        ok = ok && t.next(k) && !k.pressed && k.key == 'a' && !t.pending();
        t.feed(30, 1);
        t.next(k);
        t.reset();
        ok = ok && t.next(k) && !k.pressed && !t.pending();
        if (!ok) { std::printf("FAIL press/release order\n"); ++g_failed; }
    }
    {
        KeyTranslator t(KeyLayout::Us);
        check("usb keyboard", typed(t, taps({26, 27, 39, 40, 41, 43, 51, 52, 53, 12, 13})), "[];'`\\,./-=");
        check("usb keyboard shift", typed(t, {{SHIFT, 1}, {26, 1}, {26, 0}, {53, 1}, {53, 0}, {SHIFT, 0}}), "{?");
    }
    if (g_failed) return 1;
    std::printf("keys_test: all passed\n");
    return 0;
}
