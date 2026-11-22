// LED strip effects for the Grove app (docs/GROVE.md).
//
// Header only, with no LVGL in it: the Grove app draws its on-screen preview
// with it, and zeroos-grove-ledd (shell/ledd/) lights the real strip with the
// same code, so the two always match. The settings travel between them as one
// text line ("effect=rainbow color=ff0000 brightness=25 speed=5 count=15"),
// which is also what scripts send to the service's socket.
#pragma once

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <string>
#include <vector>

namespace zs::leds {

constexpr int kMaxLeds = 144;
constexpr int kMaxSpeed = 10;

enum class Effect { Off, Solid, Breathe, Rainbow, Chase, Sparkle, Pixels };

struct Rgb {
    uint8_t r = 0, g = 0, b = 0;
    bool operator==(const Rgb& o) const { return r == o.r && g == o.g && b == o.b; }
};

struct Settings {
    Effect effect = Effect::Off;
    Rgb color{255, 80, 0};       // Ubuntu orange-ish: what Solid, Breathe, Chase and Sparkle use
    int brightness = 25;         // percent, 1..100
    int speed = 5;               // 1..10
    int count = 15;              // LEDs on the strip (M5's Grove strips have 10, 15 or 20)
    std::vector<Rgb> pixels;     // Effect::Pixels: set one by one by a script
};

inline const char* effect_name(Effect e) {
    switch (e) {
    case Effect::Off: return "off";
    case Effect::Solid: return "solid";
    case Effect::Breathe: return "breathe";
    case Effect::Rainbow: return "rainbow";
    case Effect::Chase: return "chase";
    case Effect::Sparkle: return "sparkle";
    case Effect::Pixels: return "pixels";
    }
    return "off";
}

inline bool effect_from_name(const std::string& s, Effect* out) {
    for (Effect e : {Effect::Off, Effect::Solid, Effect::Breathe, Effect::Rainbow, Effect::Chase,
                     Effect::Sparkle, Effect::Pixels}) {
        if (s == effect_name(e)) {
            *out = e;
            return true;
        }
    }
    return false;
}

// Does the strip change over time (the service redraws ~50 times a second)?
inline bool animated(Effect e) {
    return e == Effect::Breathe || e == Effect::Rainbow || e == Effect::Chase || e == Effect::Sparkle;
}

inline std::string hex(Rgb c) {
    char b[8];
    std::snprintf(b, sizeof b, "%02x%02x%02x", c.r, c.g, c.b);
    return b;
}

// "ff8000" or "#ff8000".
inline bool parse_hex(std::string s, Rgb* out) {
    if (!s.empty() && s[0] == '#') s.erase(0, 1);
    if (s.size() != 6) return false;
    for (char ch : s)
        if (!std::isxdigit(static_cast<unsigned char>(ch))) return false;
    unsigned long v = std::strtoul(s.c_str(), nullptr, 16);
    *out = {uint8_t(v >> 16), uint8_t(v >> 8), uint8_t(v)};
    return true;
}

// The whole state as one line of key=value words.
inline std::string to_line(const Settings& s) {
    std::string l = std::string("effect=") + effect_name(s.effect) + " color=" + hex(s.color) +
                    " brightness=" + std::to_string(s.brightness) + " speed=" + std::to_string(s.speed) +
                    " count=" + std::to_string(s.count);
    if (s.effect == Effect::Pixels && !s.pixels.empty()) {
        l += " pixels=";
        for (size_t i = 0; i < s.pixels.size(); ++i) l += (i ? "," : "") + hex(s.pixels[i]);
    }
    return l;
}

// Apply the key=value words in a line to s; keys left out keep their value.
// Returns "" on success, else what was wrong (and s is unchanged).
inline std::string apply_line(const std::string& line, Settings* s) {
    Settings n = *s;
    size_t i = 0;
    while (i < line.size()) {
        while (i < line.size() && std::isspace(static_cast<unsigned char>(line[i]))) ++i;
        size_t j = i;
        while (j < line.size() && !std::isspace(static_cast<unsigned char>(line[j]))) ++j;
        if (j == i) break;
        std::string word = line.substr(i, j - i);
        i = j;
        size_t eq = word.find('=');
        if (eq == std::string::npos) return "expected key=value, got '" + word + "'";
        std::string k = word.substr(0, eq), v = word.substr(eq + 1);
        auto number = [&](int lo, int hi, int* out) {
            char* end = nullptr;
            long x = std::strtol(v.c_str(), &end, 10);
            if (v.empty() || *end || x < lo || x > hi) return false;
            *out = int(x);
            return true;
        };
        if (k == "effect") {
            if (!effect_from_name(v, &n.effect)) return "unknown effect '" + v + "'";
        } else if (k == "color" || k == "colour") {
            if (!parse_hex(v, &n.color)) return "color must be 6 hex digits, like ff8000";
        } else if (k == "brightness") {
            if (!number(1, 100, &n.brightness)) return "brightness must be 1 to 100";
        } else if (k == "speed") {
            if (!number(1, kMaxSpeed, &n.speed)) return "speed must be 1 to 10";
        } else if (k == "count") {
            if (!number(1, kMaxLeds, &n.count)) return "count must be 1 to " + std::to_string(kMaxLeds);
        } else if (k == "pixels") {
            n.pixels.clear();
            size_t p = 0;
            while (p <= v.size()) {
                size_t q = v.find(',', p);
                if (q == std::string::npos) q = v.size();
                Rgb c;
                if (!parse_hex(v.substr(p, q - p), &c)) return "pixels must be hex colours separated by commas";
                if (int(n.pixels.size()) >= kMaxLeds) return "too many pixels";
                n.pixels.push_back(c);
                p = q + 1;
            }
        } else {
            return "unknown setting '" + k + "'";
        }
    }
    *s = n;
    return "";
}

// Hue 0..255 around the colour wheel, full saturation and value.
inline Rgb wheel(int h) {
    h &= 255;
    int region = h / 43, rem = (h - region * 43) * 6;
    uint8_t up = uint8_t(std::min(255, rem)), down = uint8_t(255 - std::min(255, rem));
    switch (region) {
    case 0: return {255, up, 0};
    case 1: return {down, 255, 0};
    case 2: return {0, 255, up};
    case 3: return {0, down, 255};
    case 4: return {up, 0, 255};
    default: return {255, 0, down};
    }
}

inline Rgb scale(Rgb c, float f) {
    f = std::clamp(f, 0.0f, 1.0f);
    return {uint8_t(std::lround(c.r * f)), uint8_t(std::lround(c.g * f)), uint8_t(std::lround(c.b * f))};
}

// A small fixed hash, so Sparkle looks random but needs no state.
inline uint32_t mix(uint32_t x) {
    x ^= x >> 16;
    x *= 0x7feb352dU;
    x ^= x >> 15;
    x *= 0x846ca68bU;
    x ^= x >> 16;
    return x;
}

// The colours at time t (milliseconds), before brightness. out gets s.count entries.
inline void render_full(const Settings& s, uint32_t t, std::vector<Rgb>* out) {
    int n = std::clamp(s.count, 1, kMaxLeds);
    out->assign(size_t(n), Rgb{});
    float sp = float(std::clamp(s.speed, 1, kMaxSpeed));
    switch (s.effect) {
    case Effect::Off: break;
    case Effect::Solid:
        for (auto& p : *out) p = s.color;
        break;
    case Effect::Breathe: {
        float period = 6000.0f / sp;   // 6 s slowest, 0.6 s fastest
        float ph = std::fmod(float(t), period) / period;
        float level = 0.08f + 0.92f * (0.5f - 0.5f * std::cos(ph * 6.2831853f));
        for (auto& p : *out) p = scale(s.color, level);
        break;
    }
    case Effect::Rainbow: {
        int shift = int(float(t) * sp * 0.03f);   // one turn in ~8.5 s at speed 1
        for (int i = 0; i < n; ++i) (*out)[size_t(i)] = wheel(i * 256 / n + shift);
        break;
    }
    case Effect::Chase: {
        float pos = std::fmod(float(t) * sp * 0.004f, float(n));   // LEDs per ms
        for (int i = 0; i < n; ++i) {
            float d = pos - float(i);
            if (d < 0) d += float(n);   // how far behind the head this LED is
            float level = d < 1.0f ? 1.0f : std::max(0.0f, 1.0f - (d - 1.0f) / 4.0f) * 0.6f;
            (*out)[size_t(i)] = scale(s.color, level);
        }
        break;
    }
    case Effect::Sparkle: {
        uint32_t period = uint32_t(3000.0f / sp);
        for (int i = 0; i < n; ++i) {
            uint32_t phase = mix(uint32_t(i) * 2654435761U) % period;
            uint32_t cycle = (t + phase) / period, at = (t + phase) % period;
            bool lit = mix(uint32_t(i) ^ (cycle * 40503U)) % 3 == 0;   // a third flash each cycle
            float level = lit ? 1.0f - float(at) / float(period) : 0.0f;
            (*out)[size_t(i)] = scale(s.color, level * level);
        }
        break;
    }
    case Effect::Pixels:
        for (int i = 0; i < n && i < int(s.pixels.size()); ++i) (*out)[size_t(i)] = s.pixels[size_t(i)];
        break;
    }
}

// What the strip shows: render_full at the brightness, then held under a
// current budget (max_ma for the whole strip, about 20 mA per colour channel
// at full) so a long white strip can't overload the expansion port's 5 V.
inline void render(const Settings& s, uint32_t t, std::vector<Rgb>* out, int max_ma = 0) {
    render_full(s, t, out);
    float f = float(std::clamp(s.brightness, 1, 100)) / 100.0f;
    float sum = 0;
    for (auto& p : *out) {
        p = scale(p, f);
        sum += float(p.r) + float(p.g) + float(p.b);
    }
    float ma = sum / 255.0f * 20.0f;
    if (max_ma > 0 && ma > float(max_ma)) {
        float g = float(max_ma) / ma;
        for (auto& p : *out) p = scale(p, g);
    }
}

}  // namespace zs::leds
