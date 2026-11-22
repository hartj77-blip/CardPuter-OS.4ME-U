// Tests for the Grove app's LED effects and settings line (grove/leds.h):
// cmake ... && ctest, or run build/<dir>/leds_test.
#include <cstdio>
#include <string>
#include <vector>

#include "grove/leds.h"

using namespace zs::leds;

static int fails = 0;
#define CHECK(cond)                                                       \
    do {                                                                  \
        if (!(cond)) {                                                    \
            std::printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond);   \
            ++fails;                                                      \
        }                                                                 \
    } while (0)

int main() {
    // The settings line goes there and back.
    Settings s;
    s.effect = Effect::Rainbow;
    s.color = {1, 2, 255};
    s.brightness = 40;
    s.speed = 7;
    s.count = 20;
    Settings t;
    CHECK(apply_line(to_line(s), &t).empty());
    CHECK(t.effect == Effect::Rainbow && t.color == s.color && t.brightness == 40 && t.speed == 7 && t.count == 20);
    CHECK(to_line(s) == "effect=rainbow color=0102ff brightness=40 speed=7 count=20");

    // Only the keys given change; a bad line changes nothing.
    CHECK(apply_line("speed=2", &t).empty() && t.speed == 2 && t.count == 20);
    CHECK(apply_line("colour=#00ff00", &t).empty() && t.color == (Rgb{0, 255, 0}));
    CHECK(!apply_line("speed=3 count=0", &t).empty() && t.speed == 2 && t.count == 20);
    CHECK(!apply_line("effect=disco", &t).empty());
    CHECK(!apply_line("brightness=101", &t).empty());
    CHECK(!apply_line("color=12345", &t).empty());
    CHECK(!apply_line("bogus=1", &t).empty());
    CHECK(!apply_line("speed", &t).empty());
    CHECK(apply_line("", &t).empty());

    // Pixels, one by one.
    CHECK(apply_line("effect=pixels count=4 pixels=ff0000,00ff00,0000ff", &t).empty());
    CHECK(t.pixels.size() == 3);
    std::vector<Rgb> px;
    t.brightness = 100;
    render(t, 0, &px);
    CHECK(px.size() == 4 && px[0] == (Rgb{255, 0, 0}) && px[2] == (Rgb{0, 0, 255}) && px[3] == Rgb{});
    CHECK(to_line(t).find("pixels=ff0000,00ff00,0000ff") != std::string::npos);
    CHECK(!apply_line("pixels=ff0000,zz", &t).empty());

    // Off is dark; Solid is the colour at the brightness.
    s = Settings{};
    s.count = 10;
    render(s, 1234, &px);
    CHECK(px.size() == 10);
    for (auto& p : px) CHECK(p == Rgb{});
    s.effect = Effect::Solid;
    s.color = {200, 100, 0};
    s.brightness = 50;
    render(s, 0, &px);
    for (auto& p : px) CHECK(p == (Rgb{100, 50, 0}));

    // The current budget holds a full-white strip down: 60 LEDs at full
    // white would be 3.6 A.
    s.color = {255, 255, 255};
    s.brightness = 100;
    s.count = 60;
    render(s, 0, &px, 500);
    float ma = 0;
    for (auto& p : px) ma += float(p.r + p.g + p.b) / 255.0f * 20.0f;
    CHECK(ma <= 505.0f && ma > 400.0f);

    // Animations move, and stay in range.
    for (Effect e : {Effect::Breathe, Effect::Rainbow, Effect::Chase, Effect::Sparkle}) {
        s.effect = e;
        s.count = 15;
        s.speed = 5;
        std::vector<Rgb> a, b;
        bool moved = false;
        render(s, 0, &a);
        for (uint32_t ms = 50; ms < 4000; ms += 50) {
            render(s, ms, &b);
            CHECK(b.size() == 15);
            if (b != a) moved = true;
        }
        CHECK(moved);
        CHECK(animated(e));
    }
    CHECK(!animated(Effect::Solid) && !animated(Effect::Off));

    // The colour wheel goes all the way round.
    CHECK(wheel(0) == (Rgb{255, 0, 0}));
    CHECK(wheel(85).g > 200 && wheel(170).b > 200);

    if (fails) {
        std::printf("%d failures\n", fails);
        return 1;
    }
    std::printf("ok   leds_test\n");
    return 0;
}
