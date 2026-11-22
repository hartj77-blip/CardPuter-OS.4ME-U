// WS2812 / SK6812 ("NeoPixel") output on the Grove port through the
// BCM2837's SMI parallel bus and DMA (docs/GROVE.md, "How it works").
//
// The Grove port's pins are GPIO 14 and 15 (in its default UART mode). The
// usual Raspberry Pi NeoPixel library needs a PWM, PCM or SPI pin, and none
// of those reach the port. But GPIO 14 and 15 are also SMI data lines SD6
// and SD7 (ALT1), and SMI clocks bytes out at a fixed rate from DMA, so the
// timing is exact whatever Linux is doing: each LED bit is four SMI cycles of
// 312.5 ns, high-low-low-low for a 0 and high-high-low-low for a 1.
//
// Needs root: it maps the SMI, DMA, clock and GPIO registers through
// /dev/mem and takes uncached memory from the VideoCore through /dev/vcio.
#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace zs::ledd {

struct SmiOptions {
    std::vector<int> pins{14, 15};   // GPIO 14 and/or 15: both by default, so either Grove signal wire works
    int dma_channel = 10;            // as rpi_ws281x: the kernel rarely uses it
    int max_leds = 144;
};

class SmiStrip {
public:
    SmiStrip() = default;
    ~SmiStrip() { close(); }
    SmiStrip(const SmiStrip&) = delete;
    SmiStrip& operator=(const SmiStrip&) = delete;

    // Map the registers, take the pins and the DMA memory. False with
    // error() set if something is missing (not a Raspberry Pi, not root).
    bool open(const SmiOptions& o);
    bool is_open() const { return open_; }
    // Send GRB bytes, three per LED, and wait for them to go out (under 5 ms
    // for 144 LEDs). False with error() set if the transfer didn't finish.
    bool show(const std::vector<uint8_t>& grb);
    // Give the pins back to the UART (as they were before open), free the
    // memory and stop the bus.
    void close();
    const std::string& error() const { return error_; }

private:
    bool fail(const std::string& e);
    bool alloc_vc(size_t size);
    void free_vc();
    bool setup_clock();

    bool open_ = false;
    std::string error_;
    SmiOptions opt_;
    int mem_fd_ = -1, mbox_fd_ = -1;
    volatile uint32_t *gpio_ = nullptr, *smi_ = nullptr, *clk_ = nullptr, *dma_ = nullptr;
    uint32_t saved_fsel_[2] = {0, 0};   // GPIO 14, 15 functions before we took them
    // VideoCore memory: one DMA control block, then the bytes to send.
    uint32_t vc_handle_ = 0, vc_bus_ = 0;
    size_t vc_size_ = 0;
    void* vc_virt_ = nullptr;
};

}  // namespace zs::ledd
