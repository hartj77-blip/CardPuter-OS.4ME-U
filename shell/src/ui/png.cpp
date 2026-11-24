#include "ui/png.h"

#include <cstdio>

namespace zs {
namespace {

uint32_t crc32(const uint8_t* p, size_t n, uint32_t crc = 0) {
    static uint32_t table[256];
    if (!table[1]) {
        for (uint32_t i = 0; i < 256; ++i) {
            uint32_t c = i;
            for (int k = 0; k < 8; ++k) c = (c & 1) ? 0xEDB88320u ^ (c >> 1) : c >> 1;
            table[i] = c;
        }
    }
    crc = ~crc;
    for (size_t i = 0; i < n; ++i) crc = table[(crc ^ p[i]) & 0xff] ^ (crc >> 8);
    return ~crc;
}

void put32(std::vector<uint8_t>& out, uint32_t v) {
    out.push_back(uint8_t(v >> 24));
    out.push_back(uint8_t(v >> 16));
    out.push_back(uint8_t(v >> 8));
    out.push_back(uint8_t(v));
}

void chunk(std::vector<uint8_t>& out, const char* type, const std::vector<uint8_t>& data) {
    put32(out, uint32_t(data.size()));
    size_t start = out.size();
    out.insert(out.end(), type, type + 4);
    out.insert(out.end(), data.begin(), data.end());
    put32(out, crc32(&out[start], out.size() - start));
}

}  // namespace

std::vector<uint8_t> png_encode_rgb(const uint8_t* rgb, int w, int h) {
    std::vector<uint8_t> out = {0x89, 'P', 'N', 'G', '\r', '\n', 0x1a, '\n'};
    std::vector<uint8_t> ihdr;
    put32(ihdr, uint32_t(w));
    put32(ihdr, uint32_t(h));
    ihdr.insert(ihdr.end(), {8, 2, 0, 0, 0});   // 8 bits, RGB, deflate, no filter, no interlace
    chunk(out, "IHDR", ihdr);

    // The image data: each row is a filter byte (0, none) and its pixels.
    std::vector<uint8_t> raw;
    raw.reserve(size_t(h) * (size_t(w) * 3 + 1));
    for (int y = 0; y < h; ++y) {
        raw.push_back(0);
        const uint8_t* row = rgb + size_t(y) * size_t(w) * 3;
        raw.insert(raw.end(), row, row + size_t(w) * 3);
    }
    // zlib stream of stored (uncompressed) deflate blocks, at most 65535
    // bytes each, then the Adler-32 of the raw data.
    std::vector<uint8_t> z = {0x78, 0x01};
    size_t pos = 0;
    do {
        size_t n = raw.size() - pos;
        if (n > 65535) n = 65535;
        bool last = pos + n == raw.size();
        z.push_back(last ? 1 : 0);
        z.push_back(uint8_t(n));
        z.push_back(uint8_t(n >> 8));
        z.push_back(uint8_t(~n));
        z.push_back(uint8_t(~n >> 8));
        z.insert(z.end(), raw.begin() + long(pos), raw.begin() + long(pos + n));
        pos += n;
    } while (pos < raw.size());
    uint32_t a = 1, b = 0;
    for (uint8_t c : raw) {
        a = (a + c) % 65521;
        b = (b + a) % 65521;
    }
    put32(z, (b << 16) | a);
    chunk(out, "IDAT", z);
    chunk(out, "IEND", {});
    return out;
}

bool png_write_rgb(const std::string& path, const uint8_t* rgb, int w, int h) {
    std::vector<uint8_t> png = png_encode_rgb(rgb, w, h);
    FILE* f = std::fopen(path.c_str(), "wb");
    if (!f) return false;
    bool ok = std::fwrite(png.data(), 1, png.size(), f) == png.size();
    ok = std::fclose(f) == 0 && ok;
    if (!ok) std::remove(path.c_str());
    return ok;
}

}  // namespace zs
