// See smi_ws2812.h. Register layouts are from the BCM2835 ARM Peripherals
// manual (DMA, GPIO, clocks) and, for SMI, which the manual leaves out, from
// the kernel's bcm2835_smi driver and Jeremy Bentham's SMI write-ups
// (iosoft.blog), which drive WS2812 strips the same way.
#include "smi_ws2812.h"

#include <fcntl.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <unistd.h>

#include <algorithm>
#include <cerrno>
#include <chrono>
#include <cstdio>
#include <cstring>
#include <thread>

namespace zs::ledd {
namespace {

// Peripherals as the DMA engine sees them (bus addresses).
constexpr uint32_t kBusPeriph = 0x7E000000;
constexpr uint32_t kGpioOff = 0x200000, kSmiOff = 0x600000, kClkOff = 0x101000, kDmaOff = 0x007000;

// SMI registers (32-bit word index).
enum { SMI_CS = 0, SMI_L = 1, SMI_A = 2, SMI_D = 3, SMI_DSR0 = 4, SMI_DSW0 = 5, SMI_DMC = 12,
       SMI_DCS = 13, SMI_DCA = 14 };
constexpr uint32_t CS_ENABLE = 1u << 0, CS_DONE = 1u << 1, CS_ACTIVE = 1u << 2, CS_START = 1u << 3,
                   CS_CLEAR = 1u << 4, CS_WRITE = 1u << 5, CS_SETERR = 1u << 13, CS_PXLDAT = 1u << 14;
constexpr uint32_t DMC_DMAEN = 1u << 28;
// Device-0 timing: setup, strobe and hold in SMI clocks; width 0 = 8 bits.
constexpr uint32_t timing(uint32_t setup, uint32_t strobe, uint32_t hold) {
    return (setup << 24) | (hold << 16) | strobe;
}

// Clock manager: the SMI clock.
enum { CM_SMI_CTL = 0xb0 / 4, CM_SMI_DIV = 0xb4 / 4 };
constexpr uint32_t CM_PASSWD = 0x5Au << 24, CM_ENAB = 1u << 4, CM_KILL = 1u << 5, CM_BUSY = 1u << 7;
constexpr uint32_t CM_SRC_PLLD = 6;   // 500 MHz on the BCM2837

// DMA channel registers (word index, channel n at n * 0x100).
enum { DMA_CS = 0, DMA_CONBLK_AD = 1, DMA_DEBUG = 8 };
constexpr uint32_t DMA_ENABLE_REG = 0xff0 / 4;
constexpr uint32_t DMA_ACTIVE = 1u << 0, DMA_END = 1u << 1, DMA_ERROR = 1u << 8, DMA_ABORT = 1u << 30,
                   DMA_RESET = 1u << 31, DMA_WAIT_WRITES = 1u << 28;
constexpr uint32_t TI_WAIT_RESP = 1u << 3, TI_DEST_DREQ = 1u << 6, TI_SRC_INC = 1u << 8;
constexpr uint32_t DREQ_SMI = 4;

// 125 MHz SMI clock (PLLD / 4): 39 clocks = 312 ns per SMI cycle, four
// cycles per LED bit = 1.25 us. A 0 is high for 312 ns, a 1 for 625 ns:
// inside both WS2812B's and SK6812's limits.
constexpr uint32_t kClkDiv = 4, kSetup = 10, kStrobe = 19, kHold = 10;
// Low before and after the data: 300 us latches every WS2812 revision
// (the newest want 280 us; SK6812 80 us).
constexpr size_t kResetBytes = 960;

// VideoCore mailbox (/dev/vcio).
#define IOCTL_MBOX_PROPERTY _IOWR(100, 0, char*)
constexpr uint32_t kMemFlags = 0x0C;   // DIRECT | COHERENT: uncached, 0xC0000000 bus alias

bool mbox(int fd, uint32_t* buf) {
    return ioctl(fd, IOCTL_MBOX_PROPERTY, buf) >= 0 && buf[1] == 0x80000000u;
}

uint32_t be32(const unsigned char* p) {
    return (uint32_t(p[0]) << 24) | (uint32_t(p[1]) << 16) | (uint32_t(p[2]) << 8) | p[3];
}

// Where the peripherals are in the ARM's physical address space, from the
// device tree (0x3F000000 on the BCM2837).
uint32_t periph_base() {
    unsigned char b[16] = {};
    FILE* f = std::fopen("/proc/device-tree/soc/ranges", "rb");
    size_t n = f ? std::fread(b, 1, sizeof b, f) : 0;
    if (f) std::fclose(f);
    if (n >= 8) {
        uint32_t a = be32(b + 4);
        if (a == 0 && n >= 12) a = be32(b + 8);   // two-cell parent addresses (BCM2711)
        if (a) return a;
    }
    return 0x3F000000;
}

volatile uint32_t* map(int fd, uint32_t phys, size_t size) {
    void* p = mmap(nullptr, size, PROT_READ | PROT_WRITE, MAP_SHARED, fd, off_t(phys));
    return p == MAP_FAILED ? nullptr : static_cast<volatile uint32_t*>(p);
}

void sleep_us(int us) { std::this_thread::sleep_for(std::chrono::microseconds(us)); }

// The SMI byte bit for a GPIO: SD0 is GPIO 8, so GPIO 14 is bit 6, 15 bit 7.
uint8_t pin_bit(int gpio) { return uint8_t(1u << (gpio - 8)); }

}  // namespace

bool SmiStrip::fail(const std::string& e) {
    error_ = e;
    close();
    return false;
}

bool SmiStrip::open(const SmiOptions& o) {
    close();
    error_.clear();
    opt_ = o;
    if (opt_.pins.empty()) return fail("no pins");
    for (int p : opt_.pins)
        if (p != 14 && p != 15) return fail("pins must be GPIO 14 and/or 15 (the Grove port)");
    if (opt_.dma_channel < 0 || opt_.dma_channel > 14) return fail("DMA channel must be 0 to 14");

    mem_fd_ = ::open("/dev/mem", O_RDWR | O_SYNC | O_CLOEXEC);
    if (mem_fd_ < 0) return fail(std::string("can't open /dev/mem (run as root): ") + std::strerror(errno));
    uint32_t base = periph_base();
    gpio_ = map(mem_fd_, base + kGpioOff, 4096);
    smi_ = map(mem_fd_, base + kSmiOff, 4096);
    clk_ = map(mem_fd_, base + kClkOff, 4096);
    dma_ = map(mem_fd_, base + kDmaOff, 4096);
    if (!gpio_ || !smi_ || !clk_ || !dma_) return fail("can't map the SoC's registers");

    volatile uint32_t* ch = dma_ + opt_.dma_channel * 0x40;
    if (ch[DMA_CS] & DMA_ACTIVE) return fail("DMA channel " + std::to_string(opt_.dma_channel) + " is busy");

    size_t bytes = kResetBytes * 2 + size_t(opt_.max_leds) * 24 * 4;
    if (!alloc_vc(32 + bytes)) return false;

    // SMI: stop it, set up its clock, then 8-bit writes paced from DMA.
    smi_[SMI_CS] = 0;
    smi_[SMI_L] = 0;
    smi_[SMI_A] = 0;
    smi_[SMI_DSR0] = 0;
    smi_[SMI_DSW0] = 0;
    smi_[SMI_DCS] = 0;
    smi_[SMI_DCA] = 0;
    if (!setup_clock()) return false;
    if (smi_[SMI_CS] & CS_SETERR) smi_[SMI_CS] = CS_SETERR;   // write 1 to clear
    smi_[SMI_DSR0] = timing(kSetup, kStrobe, kHold);
    smi_[SMI_DSW0] = timing(kSetup, kStrobe, kHold);
    smi_[SMI_DMC] = DMC_DMAEN | (8u << 18) | (8u << 12) | (2u << 6) | 2u;   // panic 8, request 2

    dma_[DMA_ENABLE_REG] |= 1u << opt_.dma_channel;
    ch[DMA_CS] = DMA_RESET;
    sleep_us(10);

    // Pins last, low first: remember what they were (the UART) to give back.
    for (int p : opt_.pins) {
        int shift = (p % 10) * 3;
        saved_fsel_[p - 14] = (gpio_[1] >> shift) & 7;
    }
    open_ = true;
    // Drive the bus low before the pins switch over; this also checks that
    // SMI and DMA move data at all.
    if (!show(std::vector<uint8_t>{})) return fail(error_);
    for (int p : opt_.pins) {
        int shift = (p % 10) * 3;
        gpio_[1] = (gpio_[1] & ~(7u << shift)) | (5u << shift);   // ALT1: SMI SD6 / SD7
    }
    return true;
}

bool SmiStrip::setup_clock() {
    if (((clk_[CM_SMI_DIV] >> 12) & 0xfff) == kClkDiv && (clk_[CM_SMI_CTL] & 0xF) == CM_SRC_PLLD &&
        (clk_[CM_SMI_CTL] & CM_ENAB))
        return true;
    clk_[CM_SMI_CTL] = CM_PASSWD | CM_KILL;
    for (int i = 0; i < 1000 && (clk_[CM_SMI_CTL] & CM_BUSY); ++i) sleep_us(10);
    if (clk_[CM_SMI_CTL] & CM_BUSY) return fail("the SMI clock won't stop");
    clk_[CM_SMI_DIV] = CM_PASSWD | (kClkDiv << 12);
    sleep_us(10);
    clk_[CM_SMI_CTL] = CM_PASSWD | CM_SRC_PLLD;
    sleep_us(10);
    clk_[CM_SMI_CTL] = CM_PASSWD | CM_SRC_PLLD | CM_ENAB;
    for (int i = 0; i < 1000 && !(clk_[CM_SMI_CTL] & CM_BUSY); ++i) sleep_us(10);
    if (!(clk_[CM_SMI_CTL] & CM_BUSY)) return fail("the SMI clock won't start");
    return true;
}

bool SmiStrip::show(const std::vector<uint8_t>& grb) {
    if (!open_) return false;
    size_t n = std::min(grb.size(), size_t(opt_.max_leds) * 3);
    uint8_t hi = 0;
    for (int p : opt_.pins) hi |= pin_bit(p);

    // [control block, 32 bytes][low][4 bytes per bit][low], padded to words.
    auto* mem = static_cast<uint8_t*>(vc_virt_);
    uint8_t* data = mem + 32;
    size_t len = kResetBytes;
    std::memset(data, 0, kResetBytes);
    for (size_t i = 0; i < n; ++i) {
        for (int b = 7; b >= 0; --b) {
            data[len++] = hi;
            data[len++] = (grb[i] >> b) & 1 ? hi : 0;
            data[len++] = 0;
            data[len++] = 0;
        }
    }
    std::memset(data + len, 0, kResetBytes);
    len += kResetBytes;
    len = (len + 3) & ~size_t(3);

    auto* cb = reinterpret_cast<volatile uint32_t*>(mem);
    cb[0] = TI_WAIT_RESP | TI_DEST_DREQ | TI_SRC_INC | (DREQ_SMI << 16);
    cb[1] = vc_bus_ + 32;                         // source: our bytes
    cb[2] = kBusPeriph + kSmiOff + SMI_D * 4;     // destination: the SMI data FIFO
    cb[3] = uint32_t(len);
    cb[4] = 0;
    cb[5] = 0;                                    // no next block
    cb[6] = cb[7] = 0;
    __sync_synchronize();

    volatile uint32_t* ch = dma_ + opt_.dma_channel * 0x40;
    smi_[SMI_CS] = 0;
    smi_[SMI_CS] = CS_ENABLE | CS_CLEAR | CS_PXLDAT;
    smi_[SMI_L] = uint32_t(len);
    smi_[SMI_CS] = CS_ENABLE | CS_PXLDAT | CS_WRITE;

    ch[DMA_CS] = DMA_RESET;
    sleep_us(5);
    ch[DMA_CONBLK_AD] = vc_bus_;
    ch[DMA_CS] = DMA_END;      // clear END
    ch[DMA_DEBUG] = 7;         // clear errors
    __sync_synchronize();
    ch[DMA_CS] = DMA_WAIT_WRITES | (15u << 20) | (15u << 16) | DMA_ACTIVE;
    smi_[SMI_CS] = CS_ENABLE | CS_PXLDAT | CS_WRITE | CS_START;

    // 312 ns a byte: about 5 ms for 144 LEDs. Allow 50 ms.
    auto until = std::chrono::steady_clock::now() + std::chrono::milliseconds(50);
    while ((ch[DMA_CS] & DMA_ACTIVE) || (smi_[SMI_CS] & CS_ACTIVE)) {
        if (std::chrono::steady_clock::now() > until) {
            ch[DMA_CS] = DMA_ABORT;
            sleep_us(10);
            ch[DMA_CS] = DMA_RESET;
            smi_[SMI_CS] = 0;
            error_ = "the strip's transfer didn't finish (DMA " + std::to_string(opt_.dma_channel) + ")";
            return false;
        }
        sleep_us(200);
    }
    if (ch[DMA_CS] & DMA_ERROR) {
        error_ = "DMA error";
        return false;
    }
    error_.clear();
    return true;
}

bool SmiStrip::alloc_vc(size_t size) {
    mbox_fd_ = ::open("/dev/vcio", O_RDWR | O_CLOEXEC);
    if (mbox_fd_ < 0) return fail(std::string("can't open /dev/vcio: ") + std::strerror(errno));
    vc_size_ = (size + 4095) & ~size_t(4095);
    uint32_t a[9] = {9 * 4, 0, 0x3000c, 12, 12, uint32_t(vc_size_), 4096, kMemFlags, 0};
    if (!mbox(mbox_fd_, a) || !a[5]) return fail("the VideoCore has no memory to spare");
    vc_handle_ = a[5];
    uint32_t l[7] = {7 * 4, 0, 0x3000d, 4, 4, vc_handle_, 0};
    if (!mbox(mbox_fd_, l) || !l[5]) return fail("can't lock VideoCore memory");
    vc_bus_ = l[5];
    void* p = mmap(nullptr, vc_size_, PROT_READ | PROT_WRITE, MAP_SHARED, mem_fd_, off_t(vc_bus_ & ~0xC0000000u));
    if (p == MAP_FAILED) return fail(std::string("can't map VideoCore memory: ") + std::strerror(errno));
    vc_virt_ = p;
    return true;
}

void SmiStrip::free_vc() {
    if (vc_virt_) munmap(vc_virt_, vc_size_);
    vc_virt_ = nullptr;
    if (mbox_fd_ >= 0 && vc_handle_) {
        uint32_t u[7] = {7 * 4, 0, 0x3000e, 4, 4, vc_handle_, 0};
        mbox(mbox_fd_, u);
        uint32_t f[7] = {7 * 4, 0, 0x3000f, 4, 4, vc_handle_, 0};
        mbox(mbox_fd_, f);
    }
    vc_handle_ = vc_bus_ = 0;
    if (mbox_fd_ >= 0) ::close(mbox_fd_);
    mbox_fd_ = -1;
}

void SmiStrip::close() {
    if (open_) {
        // Give the pins back first (they were the UART), then stop the bus.
        for (int p : opt_.pins) {
            int shift = (p % 10) * 3;
            gpio_[1] = (gpio_[1] & ~(7u << shift)) | (saved_fsel_[p - 14] << shift);
        }
        volatile uint32_t* ch = dma_ + opt_.dma_channel * 0x40;
        ch[DMA_CS] = DMA_RESET;
        smi_[SMI_CS] = 0;
        smi_[SMI_DMC] = 0;
    }
    open_ = false;
    free_vc();
    for (auto* r : {&gpio_, &smi_, &clk_, &dma_}) {
        if (*r) munmap(const_cast<uint32_t*>(*r), 4096);
        *r = nullptr;
    }
    if (mem_fd_ >= 0) ::close(mem_fd_);
    mem_fd_ = -1;
}

}  // namespace zs::ledd
