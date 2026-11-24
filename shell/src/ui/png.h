// A small PNG writer for screenshots. No zlib and no LVGL: the picture is
// stored uncompressed inside the PNG (a 320x170 screen is about 160 KB), which
// every viewer opens. Tested by shell/tests/run.sh.
#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace zs {

// rgb: w * h pixels, 3 bytes each (red, green, blue), rows top to bottom.
std::vector<uint8_t> png_encode_rgb(const uint8_t* rgb, int w, int h);
bool png_write_rgb(const std::string& path, const uint8_t* rgb, int w, int h);

}  // namespace zs
