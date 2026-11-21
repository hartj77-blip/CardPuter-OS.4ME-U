// Infrared codes in the Flipper Zero ".ir" file format, and the encoders that
// turn them into the pulse and space timings an IR LED sends.
//
// The format is the one Flipper-IRDB (github.com/Lucaslhm/Flipper-IRDB) and
// the Flipper Zero use, so the same files work on both:
//
//   Filetype: IR signals file
//   Version: 1
//   #
//   name: Power
//   type: parsed
//   protocol: NEC
//   address: 04 00 00 00        little-endian bytes
//   command: 08 00 00 00
//   #
//   name: Off
//   type: raw
//   frequency: 38000
//   duty_cycle: 0.330000
//   data: 9024 4512 579 552 ...  microseconds: mark, space, mark, ...
//
// Encoders are written here from the protocols' published timings and pack
// the address and command bits the way the Flipper does, so files recorded
// by a Flipper replay the same. No LVGL here: tests build this on its own.
#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace zs::ir {

struct Signal {
    std::string name;
    bool raw = false;
    // Parsed codes: NEC, NECext, NEC42, NEC42ext, Samsung32, RC5, RC5X, RC6,
    // SIRC, SIRC15, SIRC20, Kaseikyo, RCA, Pioneer.
    std::string protocol;
    uint32_t address = 0;
    uint32_t command = 0;
    // Raw codes.
    uint32_t frequency = 38000;
    float duty_cycle = 0.33f;
    std::vector<uint32_t> timings;   // mark, space, mark, ... in microseconds
    // Where a code in a universal list came from ("# src: TVs/LG/LG_TV.ir").
    std::string source;
};

// What the IR LED does: a carrier, then on/off times starting and ending
// with "on" (a mark). Gaps between repeated frames are spaces inside it.
struct Wave {
    uint32_t carrier = 38000;
    std::vector<uint32_t> timings;
};

// Parse a whole file's text. Signals with an unknown type or a malformed
// field are skipped; returns false if the text isn't an IR signals file.
bool parse(const std::string& text, std::vector<Signal>& out);
bool load(const std::string& path, std::vector<Signal>& out);

std::string to_text(const std::vector<Signal>& signals, const std::string& comment = "");
bool save(const std::string& path, const std::vector<Signal>& signals, const std::string& comment = "");

bool known_protocol(const std::string& protocol);

// Timings for one button press. False for an unknown protocol or empty raw data.
bool encode(const Signal& s, Wave& out);

// A captured frame (from the receiver) as a signal: NEC, NECext and
// Samsung32 are recognised and stored as parsed codes, anything else is
// kept raw. Timings after the first long gap (a repeat) are dropped.
Signal from_capture(const std::vector<uint32_t>& timings, uint32_t carrier = 38000);

// Same code (name and source ignored). Raw codes compare within 10%.
bool same_code(const Signal& a, const Signal& b);

// "NEC 04/08", "Raw 67 timings", for the info panel.
std::string describe(const Signal& s);

// Names as people read them: "Vol_up" -> "Vol +", "Ch_next" -> "Ch +".
std::string pretty_name(const std::string& name);

}  // namespace zs::ir
