// Tests for the IR file format, encoders and database reader (no LVGL, no
// hardware): cmake ... && ctest, or run build/<dir>/ir_test.
#include <sys/stat.h>
#include <unistd.h>

#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <string>
#include <vector>

#include "ir/db.h"
#include "ir/ir.h"

using namespace zs::ir;

static int fails = 0;
#define CHECK(cond)                                                       \
    do {                                                                  \
        if (!(cond)) {                                                    \
            std::printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond);   \
            ++fails;                                                      \
        }                                                                 \
    } while (0)

static Signal parsed(const char* proto, uint32_t a, uint32_t c) {
    Signal s;
    s.name = "X";
    s.protocol = proto;
    s.address = a;
    s.command = c;
    return s;
}

static bool near(uint32_t v, uint32_t ref) { return v + ref / 10 >= ref && v <= ref + ref / 10; }

// Pulse-distance bits (LSB first) back out of a wave: marks at even indexes.
static std::vector<int> pd_bits(const Wave& w, size_t first, int n, uint32_t one_space) {
    std::vector<int> bits;
    for (int i = 0; i < n; ++i) bits.push_back(near(w.timings[first + 2 * size_t(i) + 1], one_space) ? 1 : 0);
    return bits;
}

// Bi-phase: expand to half-bit levels (1 = mark) of `half` us each.
static std::vector<int> halves(const Wave& w, uint32_t half) {
    std::vector<int> lv;
    for (size_t i = 0; i < w.timings.size(); ++i) {
        int n = int((w.timings[i] + half / 2) / half);
        for (int k = 0; k < n; ++k) lv.push_back(i % 2 == 0 ? 1 : 0);
    }
    return lv;
}

static void test_parse_roundtrip() {
    const char* text =
        "Filetype: IR signals file\nVersion: 1\n# comment\n#\nname: Power\ntype: parsed\nprotocol: nec\n"
        "address: 04 00 00 00\ncommand: 08 00 00 00\n#\nname: Off\ntype: raw\nfrequency: 38000\n"
        "duty_cycle: 0.330000\ndata: 9000 4500 560 560\ndata: 560\n#\nname: Weird\ntype: parsed\n"
        "protocol: Martian\naddress: 01 00 00 00\ncommand: 02 00 00 00\n#\n# src: TVs/LG/LG.ir\nname: Mute\n"
        "type: parsed\nprotocol: NECext\naddress: 00 7F 00 00\ncommand: 15 EA 00 00\n";
    std::vector<Signal> v;
    CHECK(parse(text, v));
    CHECK(v.size() == 3);   // the unknown protocol is skipped
    if (v.size() != 3) return;
    CHECK(v[0].name == "Power" && v[0].protocol == "NEC" && v[0].address == 4 && v[0].command == 8);
    CHECK(v[1].raw && v[1].timings.size() == 5 && v[1].timings[4] == 560);   // two data lines join
    CHECK(v[2].protocol == "NECext" && v[2].address == 0x7f00 && v[2].command == 0xea15);
    CHECK(v[2].source == "TVs/LG/LG.ir");
    std::vector<Signal> again;
    CHECK(parse(to_text(v, "test"), again));
    CHECK(again.size() == 3);
    for (size_t i = 0; i < again.size() && i < v.size(); ++i) {
        CHECK(same_code(v[i], again[i]));
        CHECK(v[i].name == again[i].name && v[i].source == again[i].source);
    }
    std::vector<Signal> none;
    CHECK(!parse("hello\nworld\n", none));
}

static void test_nec() {
    Wave w;
    CHECK(encode(parsed("NEC", 0x04, 0x08), w));
    CHECK(w.carrier == 38000);
    CHECK(w.timings.size() == 67);
    CHECK(w.timings[0] == 9000 && w.timings[1] == 4500 && w.timings[66] == 560);
    auto bits = pd_bits(w, 2, 32, 1690);
    uint32_t v = 0;
    for (int i = 0; i < 32; ++i) v |= uint32_t(bits[size_t(i)]) << i;
    CHECK(v == 0xF708FB04);   // 04, ~04, 08, ~08 sent LSB first
    Signal back = from_capture(w.timings);
    CHECK(!back.raw && back.protocol == "NEC" && back.address == 4 && back.command == 8);

    CHECK(encode(parsed("NECext", 0x7f00, 0xea15), w));
    back = from_capture(w.timings);
    CHECK(!back.raw && back.protocol == "NECext" && back.address == 0x7f00 && back.command == 0xea15);

    CHECK(encode(parsed("NEC42", 0x1abc, 0x5a), w));
    CHECK(w.timings.size() == 2 + 42 * 2 + 1);
}

static void test_samsung() {
    Wave w;
    CHECK(encode(parsed("Samsung32", 0x07, 0x02), w));
    CHECK(w.timings.size() == 67 && w.timings[0] == 4500 && w.timings[1] == 4500);
    Signal back = from_capture(w.timings);
    CHECK(!back.raw && back.protocol == "Samsung32" && back.address == 7 && back.command == 2);
    // A repeat after a long gap is dropped from a capture.
    std::vector<uint32_t> t = w.timings;
    t.push_back(46000);
    t.insert(t.end(), w.timings.begin(), w.timings.end());
    back = from_capture(t);
    CHECK(!back.raw && back.protocol == "Samsung32");
}

static void test_kaseikyo() {
    // Panasonic TV power: the well-known frame 02 20 80 00 3D BD.
    Wave w;
    CHECK(encode(parsed("Kaseikyo", 0x00200280, 0x03D0), w));
    CHECK(w.timings.size() == 2 + 48 * 2 + 1);
    auto bits = pd_bits(w, 2, 48, 1296);
    const uint8_t want[6] = {0x02, 0x20, 0x80, 0x00, 0x3D, 0xBD};
    for (int b = 0; b < 6; ++b) {
        uint8_t got = 0;
        for (int i = 0; i < 8; ++i) got |= uint8_t(bits[size_t(b * 8 + i)] << i);
        CHECK(got == want[b]);
    }
}

static void test_sirc() {
    Wave w;
    CHECK(encode(parsed("SIRC", 0x01, 0x15), w));   // Sony TV power
    CHECK(w.carrier == 40000);
    CHECK(w.timings.size() == 3 * 25 + 2);          // 3 frames of 25, 2 gaps
    CHECK(w.timings[0] == 2400);
    // Command 0x15 then address 1, LSB first: marks of 1200 are ones.
    const int want[12] = {1, 0, 1, 0, 1, 0, 0, 1, 0, 0, 0, 0};
    for (int i = 0; i < 12; ++i) CHECK((w.timings[2 + 2 * size_t(i)] == 1200) == (want[i] == 1));
    uint32_t frame = 0;
    for (int i = 0; i < 25; ++i) frame += w.timings[size_t(i)];
    CHECK(frame + w.timings[25] == 45000);   // 45 ms frame period
}

static void test_rc5() {
    Wave w;
    CHECK(encode(parsed("RC5", 0x00, 0x0C), w));   // Philips standby
    CHECK(w.carrier == 36000);
    // 14 bits: 1 1 T 00000 001100; a leading space half is not sent.
    auto lv = halves(w, 889);
    lv.insert(lv.begin(), 0);
    if (lv.size() % 2) lv.push_back(0);   // trailing space half
    CHECK(lv.size() == 28);
    std::vector<int> bits;
    for (size_t i = 0; i + 1 < lv.size(); i += 2) bits.push_back(lv[i] == 0 && lv[i + 1] == 1 ? 1 : 0);
    const int want[14] = {1, 1, -1, 0, 0, 0, 0, 0, 0, 0, 1, 1, 0, 0};
    for (int i = 0; i < 14 && i < int(bits.size()); ++i)
        if (want[i] >= 0) CHECK(bits[size_t(i)] == want[i]);
    int toggle1 = bits.size() > 2 ? bits[2] : -1;
    CHECK(encode(parsed("RC5", 0x00, 0x0C), w));
    lv = halves(w, 889);
    lv.insert(lv.begin(), 0);
    int toggle2 = lv.size() > 5 && lv[4] == 0 && lv[5] == 1 ? 1 : 0;
    CHECK(toggle1 != toggle2);   // flips on every press

    CHECK(encode(parsed("RC5X", 0x05, 0x01), w));   // field bit 0: starts 1 0
    lv = halves(w, 889);
    lv.insert(lv.begin(), 0);
    CHECK(lv[2] == 1 && lv[3] == 0);
}

static void test_rc6() {
    Wave w;
    CHECK(encode(parsed("RC6", 0x00, 0x0C), w));
    CHECK(w.timings[0] == 2666 && w.timings[1] >= 889);
    // After the leader: start 1, mode 000, toggle (2t), 8 address bits, 8
    // command bits, as 444 us halves. The leader's 889 us space comes first.
    std::vector<int> lv;
    for (size_t i = 1; i < w.timings.size(); ++i) {
        uint32_t d = i == 1 ? w.timings[1] - 889 : w.timings[i];
        int n = int((d + 222) / 444);
        for (int k = 0; k < n; ++k) lv.push_back(i % 2 == 0 ? 1 : 0);
    }
    if (lv.size() % 2) lv.push_back(0);
    // start(2) + mode(6) + toggle(4) + 16 bits(32) = 44 halves
    CHECK(lv.size() == 44);
    if (lv.size() != 44) return;
    CHECK(lv[0] == 1 && lv[1] == 0);   // start bit 1
    for (int i = 0; i < 3; ++i) CHECK(lv[2 + 2 * size_t(i)] == 0 && lv[3 + 2 * size_t(i)] == 1);
    uint32_t v = 0;
    for (int i = 0; i < 16; ++i) v = v << 1 | uint32_t(lv[12 + 2 * size_t(i)]);
    CHECK(v == 0x000C);
}

static void test_others() {
    Wave w;
    CHECK(encode(parsed("RCA", 0x0F, 0x54), w));
    CHECK(w.carrier == 56000 && w.timings.size() == 2 + 24 * 2 + 1 && w.timings[0] == 4000);
    CHECK(encode(parsed("Pioneer", 0xAA, 0x1C), w));
    CHECK(w.carrier == 40000 && w.timings.size() == 67 * 2 + 1);
    CHECK(encode(parsed("SIRC20", 0x1A3A, 0x15), w));
    CHECK(w.timings.size() == 3 * 41 + 2);
    Signal raw;
    raw.raw = true;
    raw.frequency = 36000;
    raw.timings = {100, 200, 300, 400};
    CHECK(encode(raw, w) && w.carrier == 36000 && w.timings.size() == 3);   // ends on a mark
    CHECK(!encode(parsed("Nope", 1, 2), w));
    Signal odd = from_capture({500, 500, 500});
    CHECK(odd.raw && odd.timings.size() == 3);
    CHECK(pretty_name("Vol_up") == "Vol +" && pretty_name("Ch_prev") == "Ch -" && pretty_name("Bass_up") == "Bass up");
}

static void test_db() {
    char tmpl[] = "/tmp/ir_test_XXXXXX";
    std::string root = mkdtemp(tmpl);
    mkdir((root + "/universal").c_str(), 0755);
    std::ofstream(root + "/index.tsv") << "# header\nTVs\tLG\t42LB\tTVs/LG/LG_42LB.ir\t12\nbad line\n";
    std::ofstream(root + "/universal/TVs.ir")
        << "Filetype: IR library file\nVersion: 1\n#\n# src: TVs/LG/LG.ir\nname: Power\ntype: parsed\n"
           "protocol: NEC\naddress: 04 00 00 00\ncommand: 08 00 00 00\n#\n# src: TVs/Samsung/S.ir\nname: Mute\n"
           "type: parsed\nprotocol: Samsung32\naddress: 07 00 00 00\ncommand: 0F 00 00 00\n#\n"
           "# src: TVs/Samsung/S.ir\nname: Power\ntype: parsed\nprotocol: Samsung32\naddress: 07 00 00 00\n"
           "command: 02 00 00 00\n";
    std::ofstream(root + "/universal/Fans.ir") << "Filetype: IR library file\nVersion: 1\n";
    auto idx = load_index(root);
    CHECK(idx.size() == 1 && idx[0].brand == "LG" && idx[0].buttons == 12);
    auto names = universal_names(root);
    CHECK(names.size() == 2 && names[0] == "TVs");
    Universal u;
    CHECK(load_universal(root, "TVs", u));
    CHECK(u.buttons.size() == 2 && u.buttons[0] == "Power" && u.codes[0].size() == 2 && u.codes[1].size() == 1);
    CHECK(source_brand(u.codes[0][1].source) == "Samsung");
    CHECK(!load_universal(root, "Fans", u));

    setenv("HOME", root.c_str(), 1);
    std::string p = user_remote_path("Living/room");
    CHECK(p == root + "/IR/Living room.ir");
    CHECK(save(p, {parsed("NEC", 1, 2)}));
    CHECK(user_remote_path("Living room") == root + "/IR/Living room 2.ir");
    auto mine = user_remotes();
    CHECK(mine.size() == 1 && stem(mine[0]) == "Living room");
    CHECK(display("LED_Lighting") == "LED Lighting" && contains("Samsung", "sung"));
    std::string cmd = "rm -rf '" + root + "'";
    if (std::system(cmd.c_str())) {}
}

int main() {
    test_parse_roundtrip();
    test_nec();
    test_samsung();
    test_kaseikyo();
    test_sirc();
    test_rc5();
    test_rc6();
    test_others();
    test_db();
    if (fails) {
        std::printf("%d check(s) failed\n", fails);
        return 1;
    }
    std::printf("ir_test: all passed\n");
    return 0;
}
