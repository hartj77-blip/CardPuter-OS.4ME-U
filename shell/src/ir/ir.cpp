#include "ir/ir.h"

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <sstream>

namespace zs::ir {
namespace {

std::string trim(const std::string& s) {
    size_t a = 0, b = s.size();
    while (a < b && std::isspace((unsigned char)s[a])) ++a;
    while (b > a && std::isspace((unsigned char)s[b - 1])) --b;
    return s.substr(a, b - a);
}

std::string lower(std::string s) {
    for (auto& ch : s) ch = char(std::tolower((unsigned char)ch));
    return s;
}

// "04 00 00 00" (little-endian bytes) -> 0x00000004
bool parse_bytes(const std::string& v, uint32_t& out) {
    std::istringstream in(v);
    std::string tok;
    uint32_t value = 0;
    int i = 0;
    while (in >> tok) {
        if (i >= 4 || tok.size() > 2) return false;
        char* end = nullptr;
        unsigned long b = std::strtoul(tok.c_str(), &end, 16);
        if (*end) return false;
        value |= uint32_t(b) << (8 * i++);
    }
    if (i == 0) return false;
    out = value;
    return true;
}

std::string bytes_text(uint32_t v) {
    char buf[16];
    std::snprintf(buf, sizeof buf, "%02X %02X %02X %02X", v & 0xff, (v >> 8) & 0xff, (v >> 16) & 0xff, v >> 24);
    return buf;
}

// Canonical spelling of a protocol name, or "" if unknown.
std::string canonical(const std::string& p) {
    static const char* names[] = {"NEC", "NECext", "NEC42", "NEC42ext", "Samsung32", "RC5", "RC5X",
                                  "RC6", "SIRC", "SIRC15", "SIRC20", "Kaseikyo", "RCA", "Pioneer"};
    std::string l = lower(p);
    for (const char* n : names)
        if (lower(n) == l) return n;
    return "";
}

// Builds mark/space timings; even indexes are marks. Adjacent marks (or
// spaces) merge, a leading space is dropped and finish() drops a trailing one.
struct Builder {
    std::vector<uint32_t> t;
    void mark(uint32_t d) {
        if (t.size() % 2) t.back() += d;
        else t.push_back(d);
    }
    void space(uint32_t d) {
        if (t.empty()) return;
        if (t.size() % 2) t.push_back(d);
        else t.back() += d;
    }
    uint32_t duration() const {
        uint32_t s = 0;
        for (auto v : t) s += v;
        return s;
    }
    void finish() {
        if (!t.empty() && t.size() % 2 == 0) t.pop_back();
    }
};

// Pulse-distance frame: header, bits LSB first, stop mark.
void pulse_distance(Builder& b, uint32_t hdr_mark, uint32_t hdr_space, uint64_t bits, int nbits, uint32_t m,
                    uint32_t s0, uint32_t s1) {
    b.mark(hdr_mark);
    b.space(hdr_space);
    for (int i = 0; i < nbits; ++i) {
        b.mark(m);
        b.space((bits >> i) & 1 ? s1 : s0);
    }
    b.mark(m);
}

// Toggle bits flip on every press so a receiver can tell a new press from a held key.
bool rc5_toggle = false;
bool rc6_toggle = false;

}  // namespace

bool known_protocol(const std::string& protocol) { return !canonical(protocol).empty(); }

bool parse(const std::string& text, std::vector<Signal>& out) {
    std::istringstream in(text);
    std::string line, source;
    Signal cur;
    bool have = false, ok = true, is_ir = false;
    auto flush = [&]() {
        if (!have) return;
        bool good = ok && !cur.name.empty();
        if (cur.raw) {
            if (cur.timings.size() % 2 == 0 && !cur.timings.empty()) cur.timings.pop_back();
            good = good && !cur.timings.empty() && cur.frequency >= 10000 && cur.frequency <= 100000;
        } else {
            cur.protocol = canonical(cur.protocol);
            good = good && !cur.protocol.empty();
        }
        if (good) out.push_back(cur);
        have = false;
    };
    while (std::getline(in, line)) {
        line = trim(line);
        if (line.empty()) continue;
        if (line[0] == '#') {
            std::string c = trim(line.substr(1));
            if (c.rfind("src:", 0) == 0) source = trim(c.substr(4));
            continue;
        }
        auto colon = line.find(':');
        if (colon == std::string::npos) continue;
        std::string key = lower(trim(line.substr(0, colon)));
        std::string val = trim(line.substr(colon + 1));
        if (key == "filetype") {
            is_ir = lower(val).find("ir ") == 0;
        } else if (key == "name") {
            flush();
            cur = Signal{};
            cur.name = val;
            cur.source = source;
            source.clear();
            have = true;
            ok = true;
        } else if (!have) {
            continue;
        } else if (key == "type") {
            std::string t = lower(val);
            if (t == "raw") cur.raw = true;
            else if (t != "parsed") ok = false;
        } else if (key == "protocol") {
            cur.protocol = val;
        } else if (key == "address") {
            ok = ok && parse_bytes(val, cur.address);
        } else if (key == "command") {
            ok = ok && parse_bytes(val, cur.command);
        } else if (key == "frequency") {
            cur.frequency = uint32_t(std::strtoul(val.c_str(), nullptr, 10));
        } else if (key == "duty_cycle") {
            cur.duty_cycle = std::strtof(val.c_str(), nullptr);
        } else if (key == "data") {
            std::istringstream d(val);
            long v;
            while (d >> v) {
                if (v <= 0 || v > 1000000) {
                    ok = false;
                    break;
                }
                cur.timings.push_back(uint32_t(v));
            }
        }
    }
    flush();
    return is_ir || !out.empty();
}

bool load(const std::string& path, std::vector<Signal>& out) {
    std::ifstream f(path);
    if (!f) return false;
    std::stringstream ss;
    ss << f.rdbuf();
    return parse(ss.str(), out);
}

std::string to_text(const std::vector<Signal>& signals, const std::string& comment) {
    std::ostringstream o;
    o << "Filetype: IR signals file\nVersion: 1\n";
    if (!comment.empty()) o << "# " << comment << "\n";
    for (auto& s : signals) {
        o << "#\n";
        if (!s.source.empty()) o << "# src: " << s.source << "\n";
        o << "name: " << s.name << "\n";
        if (s.raw) {
            char duty[32];
            std::snprintf(duty, sizeof duty, "%.6f", double(s.duty_cycle));
            o << "type: raw\nfrequency: " << s.frequency << "\nduty_cycle: " << duty << "\ndata:";
            for (auto v : s.timings) o << ' ' << v;
            o << "\n";
        } else {
            o << "type: parsed\nprotocol: " << s.protocol << "\naddress: " << bytes_text(s.address)
              << "\ncommand: " << bytes_text(s.command) << "\n";
        }
    }
    return o.str();
}

bool save(const std::string& path, const std::vector<Signal>& signals, const std::string& comment) {
    std::string tmp = path + ".tmp";
    {
        std::ofstream f(tmp);
        if (!f) return false;
        f << to_text(signals, comment);
        if (!f.good()) return false;
    }
    return std::rename(tmp.c_str(), path.c_str()) == 0;
}

bool encode(const Signal& s, Wave& out) {
    Builder b;
    out.carrier = 38000;
    if (s.raw) {
        if (s.timings.empty()) return false;
        out.carrier = s.frequency;
        out.timings = s.timings;
        if (out.timings.size() % 2 == 0) out.timings.pop_back();
        return !out.timings.empty();
    }
    const std::string p = canonical(s.protocol);
    const uint32_t a = s.address, c = s.command;
    if (p == "NEC" || p == "NECext" || p == "NEC42" || p == "NEC42ext") {
        uint64_t bits;
        int n = 32;
        if (p == "NEC") {
            bits = (a & 0xff) | (~a & 0xff) << 8 | (c & 0xff) << 16 | uint64_t(~c & 0xff) << 24;
        } else if (p == "NECext") {
            bits = (a & 0xffff) | uint64_t(c & 0xffff) << 16;
        } else if (p == "NEC42") {   // 13 address, 13 inverted address, 8 command, 8 inverted command
            bits = uint64_t(a & 0x1fff) | uint64_t(~a & 0x1fff) << 13 | uint64_t(c & 0xff) << 26 |
                   uint64_t(~c & 0xff) << 34;
            n = 42;
        } else {                     // 26 address, 16 command
            bits = uint64_t(a & 0x3ffffff) | uint64_t(c & 0xffff) << 26;
            n = 42;
        }
        pulse_distance(b, 9000, 4500, bits, n, 560, 560, 1690);
    } else if (p == "Samsung32") {
        uint64_t bits = (a & 0xff) | (a & 0xff) << 8 | (c & 0xff) << 16 | uint64_t(~c & 0xff) << 24;
        pulse_distance(b, 4500, 4500, bits, 32, 550, 550, 1650);
    } else if (p == "Pioneer") {     // NEC framing at 40 kHz, sent twice
        out.carrier = 40000;
        uint64_t bits = (a & 0xff) | (~a & 0xff) << 8 | (c & 0xff) << 16 | uint64_t(~c & 0xff) << 24;
        pulse_distance(b, 8500, 4225, bits, 32, 500, 500, 1500);
        b.space(26000);
        pulse_distance(b, 8500, 4225, bits, 32, 500, 500, 1500);
    } else if (p == "RCA") {         // 4 address, 8 command, then both inverted; 56 kHz carrier
        out.carrier = 56000;
        uint64_t bits = (a & 0xf) | (c & 0xff) << 4 | (~a & 0xf) << 12 | uint64_t(~c & 0xff) << 16;
        pulse_distance(b, 4000, 4000, bits, 24, 500, 1000, 2000);
    } else if (p == "Kaseikyo") {    // Panasonic and friends: 48 bits with parity
        out.carrier = 37000;
        uint8_t id = (a >> 24) & 3;
        uint16_t vendor = (a >> 8) & 0xffff;
        uint8_t genre1 = (a >> 4) & 0xf, genre2 = a & 0xf;
        uint8_t d[6];
        d[0] = vendor & 0xff;
        d[1] = vendor >> 8;
        uint8_t vp = d[0] ^ d[1];
        vp = (vp & 0xf) ^ (vp >> 4);
        d[2] = uint8_t((vp & 0xf) | genre1 << 4);
        d[3] = uint8_t((genre2 & 0xf) | (c & 0xf) << 4);
        d[4] = uint8_t(id << 6 | ((c >> 4) & 0x3f));
        d[5] = d[2] ^ d[3] ^ d[4];
        uint64_t bits = 0;
        for (int i = 0; i < 6; ++i) bits |= uint64_t(d[i]) << (8 * i);
        pulse_distance(b, 3456, 1728, bits, 48, 432, 432, 1296);
    } else if (p == "SIRC" || p == "SIRC15" || p == "SIRC20") {   // Sony: 3 frames, 45 ms apart
        out.carrier = 40000;
        int n = p == "SIRC" ? 12 : p == "SIRC15" ? 15 : 20;
        uint32_t amask = p == "SIRC" ? 0x1f : p == "SIRC15" ? 0xff : 0x1fff;
        uint32_t bits = (c & 0x7f) | (a & amask) << 7;
        for (int rep = 0; rep < 3; ++rep) {
            Builder f;
            f.mark(2400);
            f.space(600);
            for (int i = 0; i < n; ++i) {
                f.mark((bits >> i) & 1 ? 1200 : 600);
                if (i + 1 < n) f.space(600);
            }
            uint32_t dur = f.duration();
            for (size_t j = 0; j < f.t.size(); ++j) {
                if (j % 2) b.space(f.t[j]);
                else b.mark(f.t[j]);
            }
            if (rep < 2) b.space(45000 > dur ? 45000 - dur : 10000);
        }
    } else if (p == "RC5" || p == "RC5X") {   // Philips bi-phase, 889 us half bits, 36 kHz
        out.carrier = 36000;
        const uint32_t h = 889;
        // start, field (0 on RC5X: command + 64), toggle, 5 address, 6 command; MSB first
        uint32_t bits = 1u << 13 | (p == "RC5" ? 1u : 0u) << 12 | (rc5_toggle ? 1u : 0u) << 11 |
                        (a & 0x1f) << 6 | (c & 0x3f);
        rc5_toggle = !rc5_toggle;
        for (int i = 13; i >= 0; --i) {
            if ((bits >> i) & 1) {
                b.space(h);
                b.mark(h);
            } else {
                b.mark(h);
                b.space(h);
            }
        }
    } else if (p == "RC6") {         // mode 0: leader, start, 3 mode bits, double-length toggle, 8+8 bits
        out.carrier = 36000;
        const uint32_t t = 444;
        auto bit = [&](bool one, uint32_t len) {
            if (one) {
                b.mark(len);
                b.space(len);
            } else {
                b.space(len);
                b.mark(len);
            }
        };
        b.mark(2666);
        b.space(889);
        bit(true, t);
        for (int i = 0; i < 3; ++i) bit(false, t);
        bit(rc6_toggle, 2 * t);
        rc6_toggle = !rc6_toggle;
        for (int i = 7; i >= 0; --i) bit((a >> i) & 1, t);
        for (int i = 7; i >= 0; --i) bit((c >> i) & 1, t);
    } else {
        return false;
    }
    b.finish();
    out.timings = std::move(b.t);
    return !out.timings.empty();
}

Signal from_capture(const std::vector<uint32_t>& timings, uint32_t carrier) {
    Signal s;
    s.name = "Learned";
    // One frame: stop at the first long gap.
    std::vector<uint32_t> t;
    for (size_t i = 0; i < timings.size(); ++i) {
        if (i % 2 == 1 && timings[i] > 20000) break;
        t.push_back(timings[i]);
    }
    if (t.size() % 2 == 0 && !t.empty()) t.pop_back();

    auto near = [](uint32_t v, uint32_t ref) { return v * 10 >= ref * 7 && v * 10 <= ref * 13; };
    if (t.size() == 67) {   // header, 32 bits, stop
        bool nec = near(t[0], 9000) && near(t[1], 4500);
        bool samsung = near(t[0], 4500) && near(t[1], 4500);
        uint32_t v = 0;
        bool bits_ok = nec || samsung;
        for (int i = 0; i < 32 && bits_ok; ++i) {
            uint32_t m = t[2 + 2 * i], sp = t[3 + 2 * i];
            if (!near(m, 560)) bits_ok = false;
            else if (near(sp, 1690)) v |= 1u << i;
            else if (!near(sp, 560)) bits_ok = false;
        }
        uint8_t b0 = v & 0xff, b1 = (v >> 8) & 0xff, b2 = (v >> 16) & 0xff, b3 = v >> 24;
        if (bits_ok && samsung && b0 == b1 && uint8_t(~b2) == b3) {
            s.protocol = "Samsung32";
            s.address = b0;
            s.command = b2;
            return s;
        }
        if (bits_ok && nec) {
            if (uint8_t(~b0) == b1 && uint8_t(~b2) == b3) {
                s.protocol = "NEC";
                s.address = b0;
                s.command = b2;
            } else {
                s.protocol = "NECext";
                s.address = v & 0xffff;
                s.command = v >> 16;
            }
            return s;
        }
    }
    s.raw = true;
    s.frequency = carrier ? carrier : 38000;
    s.timings = t;
    return s;
}

bool same_code(const Signal& a, const Signal& b) {
    if (a.raw != b.raw) return false;
    if (!a.raw) return canonical(a.protocol) == canonical(b.protocol) && a.address == b.address && a.command == b.command;
    if (a.timings.size() != b.timings.size()) return false;
    for (size_t i = 0; i < a.timings.size(); ++i) {
        uint32_t x = a.timings[i], y = b.timings[i];
        uint32_t d = x > y ? x - y : y - x;
        if (d * 10 > std::max(x, y) && d > 100) return false;
    }
    return true;
}

std::string describe(const Signal& s) {
    char buf[96];
    if (s.raw) {
        std::snprintf(buf, sizeof buf, "Raw, %zu timings\n%u kHz", s.timings.size(), s.frequency / 1000);
    } else {
        int aw = s.address > 0xffffff ? 8 : s.address > 0xffff ? 6 : s.address > 0xff ? 4 : 2;
        int cw = s.command > 0xff ? 4 : 2;
        std::snprintf(buf, sizeof buf, "%s\nA %0*X  C %0*X", s.protocol.c_str(), aw, s.address, cw, s.command);
    }
    return buf;
}

std::string pretty_name(const std::string& name) {
    std::string l = lower(name);
    struct Alias { const char* key; const char* text; };
    static const Alias aliases[] = {
        {"vol_up", "Vol +"}, {"vol_dn", "Vol -"}, {"vol_down", "Vol -"}, {"ch_next", "Ch +"},
        {"ch_prev", "Ch -"}, {"ch_up", "Ch +"}, {"ch_down", "Ch -"},
    };
    for (auto& al : aliases)
        if (l == al.key) return al.text;
    std::string out = name;
    std::replace(out.begin(), out.end(), '_', ' ');
    return out;
}

}  // namespace zs::ir
