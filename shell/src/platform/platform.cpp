// Display + keyboard backends: SDL simulator, Linux device, headless.
#include "platform/platform.h"

#include <dirent.h>
#include <fcntl.h>
#include <poll.h>
#include <sys/ioctl.h>
#include <sys/stat.h>
#include <unistd.h>
#if ZS_BACKEND_DEVICE
#include <linux/fb.h>
#include <linux/input.h>
#include <linux/kd.h>
#endif
#ifdef __EMSCRIPTEN__
#include <emscripten.h>
#endif

#include <cerrno>
#include <csignal>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <deque>
#include <fstream>
#include <sstream>
#include <vector>

#include "hal/hal.h"
#include "hal/lcd_fb.h"
#include "platform/keys.h"
#include "platform/watchdog.h"
#include "hal/play_clock.h"
#include "ui/shell.h"

namespace zs {

#if ZS_BACKEND_SDL
// ---------------------------------------------------------------- simulator
lv_indev_t* platform_init(int w, int h, int zoom) {
    lv_display_t* d = lv_sdl_window_create(w, h);
    lv_sdl_window_set_zoom(d, uint8_t(zoom));
    lv_sdl_window_set_title(d, "ZeroShell simulator (Cardputer Zero 320x170)");
    return lv_sdl_keyboard_create();
}

int platform_run() {
    for (;;) {
        uint32_t ms = lv_timer_handler();
        usleep((ms > 5 ? 5 : ms) * 1000);
    }
}

int platform_run_script(const std::string&) {
    std::fprintf(stderr, "--script needs the headless build (-DZS_BACKEND=headless)\n");
    return 2;
}

void platform_set_low_power(bool) {}
void platform_console_handoff(bool) {}
bool platform_ctrl_pending() { return false; }

// The simulator: a snapshot of the active screen (the window has no
// framebuffer to read back; toasts and the panel aren't in it).
bool platform_capture(std::vector<uint8_t>& rgb, int& w, int& h) {
    lv_draw_buf_t* b = lv_snapshot_take(lv_screen_active(), LV_COLOR_FORMAT_RGB888);
    if (!b) return false;
    w = int(b->header.w);
    h = int(b->header.h);
    rgb.resize(size_t(w) * size_t(h) * 3);
    for (int y = 0; y < h; ++y) {
        const uint8_t* row = b->data + size_t(y) * b->header.stride;
        for (int x = 0; x < w; ++x) {   // LVGL's RGB888 is stored blue, green, red
            uint8_t* o = &rgb[(size_t(y) * size_t(w) + size_t(x)) * 3];
            o[0] = row[x * 3 + 2];
            o[1] = row[x * 3 + 1];
            o[2] = row[x * 3];
        }
    }
    lv_draw_buf_destroy(b);
    return true;
}

#elif ZS_BACKEND_DEVICE
namespace {
lv_indev_t* g_keypad = nullptr;
bool g_low_power = false;
volatile std::sig_atomic_t g_quit = 0;
int g_saved_kbmode = -1;   // tty1's keyboard mode before we turned it off

// systemd stops us with SIGTERM (updates, restarts): leave the main loop
// so main() returns and the atexit handlers restore tty1. No SA_RESTART,
// so the poll() in the main loop wakes up at once.
void on_quit_signal(int) { g_quit = 1; }
void install_quit_handlers() {
    struct sigaction sa{};
    sa.sa_handler = on_quit_signal;
    sigemptyset(&sa.sa_mask);
    for (int sig : {SIGTERM, SIGINT, SIGHUP}) sigaction(sig, &sa, nullptr);
}
}  // namespace
// ---------------------------------------------------------------- device
namespace {
// The keyboard is a TCA8418 matrix exposed as an evdev device. Prefer it by
// name, fall back to the first device that reports letter keys.
bool is_builtin_name(const char* name) {
    return std::strstr(name, "tca8418") || std::strstr(name, "TCA8418") || std::strstr(name, "keypad");
}


// The built-in keyboard's Sym layer sends its own key codes (keys.h); any
// other keyboard is read as a US one. ZS_KEYMAP=us|cardputer overrides.
KeyLayout keyboard_layout(int fd) {
    if (const char* env = std::getenv("ZS_KEYMAP")) {
        if (!std::strcmp(env, "us")) return KeyLayout::Us;
        if (!std::strcmp(env, "cardputer")) return KeyLayout::Cardputer;
    }
    char name[128] = {0};
    if (ioctl(fd, EVIOCGNAME(sizeof name - 1), name) < 0) return KeyLayout::Cardputer;
    std::printf("zeroshell: keyboard name \"%s\"\n", name);
    return is_builtin_name(name) ? KeyLayout::Cardputer : KeyLayout::Us;
}

static_assert(kKeyUp == LV_KEY_UP && kKeyDown == LV_KEY_DOWN && kKeyRight == LV_KEY_RIGHT &&
              kKeyLeft == LV_KEY_LEFT && kKeyEsc == LV_KEY_ESC && kKeyDel == LV_KEY_DEL &&
              kKeyBackspace == LV_KEY_BACKSPACE && kKeyEnter == LV_KEY_ENTER && kKeyNext == LV_KEY_NEXT &&
              kKeyPrev == LV_KEY_PREV && kKeyHome == LV_KEY_HOME && kKeyEnd == LV_KEY_END,
              "keys.h must match LVGL's key values");

// We read the keyboard ourselves: LVGL's own evdev driver only passes on
// arrows, Enter, Esc and a few more, and drops every letter and digit.
// Every keyboard is read: the built-in one, and any USB or Bluetooth keyboard
// plugged in or connected later (looked for every couple of seconds). Each
// has its own translator, so modifiers held on one don't leak to another.
struct Keyboard {
    int fd = -1;
    std::string path;
    bool builtin = false;
    KeyTranslator tr;
};
std::vector<Keyboard> g_kbs;
uint32_t g_cur_key = 0;
lv_indev_state_t g_cur_state = LV_INDEV_STATE_RELEASED;
uint32_t g_last_scan = 0;
constexpr uint32_t kRescanMs = 2000;

bool has_key(const unsigned long* bits, int code) {
    return bits[code / (8 * sizeof(long))] & (1UL << (code % (8 * sizeof(long))));
}

bool open_keyboard_at(const std::string& path, bool forced) {
    for (auto& k : g_kbs)
        if (k.path == path) return true;
    int fd = open(path.c_str(), O_RDONLY | O_NONBLOCK | O_NOCTTY | O_CLOEXEC);
    if (fd < 0) return false;
    char name[128] = {0};
    ioctl(fd, EVIOCGNAME(sizeof name - 1), name);
    unsigned long keys[KEY_MAX / (8 * sizeof(long)) + 1] = {0};
    ioctl(fd, EVIOCGBIT(EV_KEY, sizeof keys), keys);
    bool builtin = is_builtin_name(name);
    // A keyboard has letters. Media remotes, power buttons and gamepads don't
    // (the HAL reads media keys; Games reads gamepads).
    if (!forced && !builtin && !(has_key(keys, KEY_A) && has_key(keys, KEY_Z))) {
        close(fd);
        return false;
    }
    Keyboard k;
    k.fd = fd;
    k.path = path;
    k.builtin = builtin || forced;
    k.tr = KeyTranslator(keyboard_layout(fd));
    std::printf("zeroshell: keyboard %s (%s)\n", path.c_str(), name);
    std::fflush(stdout);
    g_kbs.push_back(std::move(k));
    return true;
}

// Open keyboards that aren't open yet. True if at least one is open.
bool open_keyboards() {
    g_last_scan = lv_tick_get();
    if (const char* env = std::getenv("ZS_KEYBOARD")) return open_keyboard_at(env, true);
    for (int i = 0; i < 32; ++i) open_keyboard_at("/dev/input/event" + std::to_string(i), false);
    return !g_kbs.empty();
}

// A keyboard that went away (Bluetooth off, USB unplugged). False if it was
// the built-in one: then systemd restarts us to open it afresh.
bool close_keyboard(size_t i) {
    bool builtin = g_kbs[i].builtin;
    std::printf("zeroshell: keyboard %s gone\n", g_kbs[i].path.c_str());
    std::fflush(stdout);
    close(g_kbs[i].fd);
    g_kbs.erase(g_kbs.begin() + long(i));
    return !builtin;
}

void keyboard_read_cb(lv_indev_t*, lv_indev_data_t* data) {
    input_event ev;
    for (size_t i = 0; i < g_kbs.size();) {
        Keyboard& k = g_kbs[i];
        ssize_t n;
        while ((n = read(k.fd, &ev, sizeof ev)) == ssize_t(sizeof ev)) {
            if (ev.type == EV_KEY) {
                // ZS_KEYLOG=1 in zeroshell.service: every raw key in the journal.
                static const bool keylog = std::getenv("ZS_KEYLOG") != nullptr;
                if (keylog) std::printf("zeroshell: key %s code %d value %d\n", k.path.c_str(), ev.code, ev.value), std::fflush(stdout);
                k.tr.feed(ev.code, ev.value);
            }
            else if (ev.type == EV_SYN && ev.code == SYN_DROPPED) k.tr.reset();
        }
        if (n < 0 && errno == ENODEV && !k.builtin) {
            close_keyboard(i);
            g_cur_state = LV_INDEV_STATE_RELEASED;   // a key it held must not stay down
            continue;
        }
        ++i;
    }
    // One press or release per read; LVGL reads again while more wait, so
    // fast typing is not lost.
    KeyEvent e;
    for (auto& k : g_kbs) {
        if (k.tr.next(e)) {
            g_cur_key = e.key;
            g_cur_state = e.pressed ? LV_INDEV_STATE_PRESSED : LV_INDEV_STATE_RELEASED;
            break;
        }
    }
    bool more = false;
    for (auto& k : g_kbs) more = more || k.tr.pending();
    data->key = g_cur_key;
    data->state = g_cur_state;
    data->continue_reading = more;
}
}  // namespace

// zeroshell.service gives us tty1 as stdin. Graphics mode stops the console
// drawing over us (cursor, messages); keyboard off stops keys piling up in
// tty1 (or reaching a login prompt). Restored on exit.
void tty_graphics(bool on) {
    if (!isatty(0)) return;
    if (on && g_saved_kbmode < 0) {
        int mode;
        if (ioctl(0, KDGKBMODE, &mode) == 0 && mode != K_OFF) g_saved_kbmode = mode;
    }
    ioctl(0, KDSETMODE, on ? KD_GRAPHICS : KD_TEXT);
    ioctl(0, KDSKBMODE, on ? K_OFF : (g_saved_kbmode >= 0 ? g_saved_kbmode : K_UNICODE));
}

// The panel driver loads about 15 s into the boot (stock image log), which
// can be after we start. zeroshell.service already waits up to 30 s for it;
// wait a little more here, then check the framebuffer opens and switch its
// output on. If there is no screen to draw on, exit: systemd
// starts us again (Restart=always) and the reason is in the journal, instead
// of a shell that runs for hours drawing nowhere.
std::string open_lcd_or_exit() {
    const bool forced = std::getenv("ZS_FRAMEBUFFER") && std::getenv("ZS_FRAMEBUFFER")[0];
    for (int i = 0; !forced && i < 100 && lcd_framebuffer_find().empty(); ++i) {
        if (i == 0) std::printf("zeroshell: waiting for the LCD framebuffer\n");
        std::fflush(stdout);
        usleep(100 * 1000);
    }
    std::string fb = lcd_framebuffer();  // fb_lcd / the panel's fbN, not HDMI's
    if (!forced && lcd_framebuffer_find().empty())
        std::fprintf(stderr, "zeroshell: no LCD framebuffer (panel driver not loaded?), trying %s\n", fb.c_str());
    int fd = open(fb.c_str(), O_RDWR);
    fb_var_screeninfo v{};
    if (fd < 0 || ioctl(fd, FBIOGET_VSCREENINFO, &v) != 0) {
        std::fprintf(stderr, "zeroshell: cannot use display %s: %s\n", fb.c_str(), std::strerror(errno));
        std::exit(1);
    }
    // Setting the mode again makes DRM's framebuffer emulation enable the
    // panel if nothing has yet (no console on it, no boot logo); then unblank.
    v.activate = FB_ACTIVATE_NOW | FB_ACTIVATE_FORCE;
    if (ioctl(fd, FBIOPUT_VSCREENINFO, &v) != 0)
        std::fprintf(stderr, "zeroshell: FBIOPUT_VSCREENINFO on %s: %s\n", fb.c_str(), std::strerror(errno));
    if (ioctl(fd, FBIOBLANK, FB_BLANK_UNBLANK) != 0)
        std::fprintf(stderr, "zeroshell: unblank %s: %s\n", fb.c_str(), std::strerror(errno));
    close(fd);
    std::printf("zeroshell: display %s %ux%u %u bpp\n", fb.c_str(), v.xres, v.yres, v.bits_per_pixel);
    std::fflush(stdout);
    return fb;
}

std::string g_fb_path;   // the LCD's framebuffer, for platform_capture()

lv_indev_t* platform_init(int, int, int) {
    std::string fb = open_lcd_or_exit();
    g_fb_path = fb;
    tty_graphics(true);
    std::atexit([] { tty_graphics(false); });
    install_quit_handlers();
    lv_display_t* d = lv_linux_fbdev_create();
    lv_linux_fbdev_set_file(d, fb.c_str());
    if (!open_keyboards()) std::fprintf(stderr, "zeroshell: no keyboard yet, will keep looking\n");
    lv_indev_t* in = lv_indev_create();
    lv_indev_set_type(in, LV_INDEV_TYPE_KEYPAD);
    lv_indev_set_read_cb(in, keyboard_read_cb);
    // Keys would also reach the Linux console on tty1 behind us (a known
    // stock-image problem); the image masks getty@tty1 to prevent that.
    g_keypad = in;
    return in;
}

// Read the picture back from the LCD's framebuffer, whatever its pixel format.
bool platform_capture(std::vector<uint8_t>& rgb, int& w, int& h) {
    if (g_fb_path.empty()) return false;
    int fd = open(g_fb_path.c_str(), O_RDONLY | O_CLOEXEC);
    if (fd < 0) return false;
    fb_var_screeninfo v{};
    fb_fix_screeninfo f{};
    bool ok = ioctl(fd, FBIOGET_VSCREENINFO, &v) == 0 && ioctl(fd, FBIOGET_FSCREENINFO, &f) == 0;
    const unsigned bpp = v.bits_per_pixel;
    ok = ok && (bpp == 16 || bpp == 24 || bpp == 32) && v.xres > 0 && v.yres > 0 && v.xres <= 4096 && v.yres <= 4096;
    if (!ok) {
        close(fd);
        return false;
    }
    const size_t px = bpp / 8;
    const size_t stride = f.line_length ? f.line_length : size_t(v.xres_virtual ? v.xres_virtual : v.xres) * px;
    w = int(v.xres);
    h = int(v.yres);
    std::vector<uint8_t> raw(stride * size_t(h));
    off_t at = off_t(v.yoffset) * off_t(stride) + off_t(v.xoffset) * off_t(px);
    size_t got = 0;
    while (got < raw.size()) {
        ssize_t n = pread(fd, raw.data() + got, raw.size() - got, at + off_t(got));
        if (n <= 0) break;
        got += size_t(n);
    }
    close(fd);
    if (got < raw.size()) return false;
    // A colour from its bits in the pixel, scaled to 0..255.
    auto part = [](uint32_t p, const fb_bitfield& b) -> uint8_t {
        if (b.length == 0 || b.length > 16) return 0;
        uint32_t max = (1u << b.length) - 1;
        return uint8_t(((p >> b.offset) & max) * 255 / max);
    };
    rgb.resize(size_t(w) * size_t(h) * 3);
    for (int y = 0; y < h; ++y) {
        const uint8_t* row = raw.data() + size_t(y) * stride;
        for (int x = 0; x < w; ++x) {
            const uint8_t* q = row + size_t(x) * px;
            uint32_t p = 0;
            for (size_t i = 0; i < px; ++i) p |= uint32_t(q[i]) << (8 * i);   // little-endian
            uint8_t* o = &rgb[(size_t(y) * size_t(w) + size_t(x)) * 3];
            o[0] = part(p, v.red);
            o[1] = part(p, v.green);
            o[2] = part(p, v.blue);
        }
    }
    return true;
}

bool platform_ctrl_pending() {
    for (auto& k : g_kbs)
        if (k.tr.ctrl_pending()) return true;
    return false;
}

void platform_console_handoff(bool away) {
    tty_graphics(!away);
    // In text mode the console would show tty1's text (the boot log) for a
    // moment when the Terminal hands the screen back: clear it, hide the cursor.
    if (away && isatty(0)) {
        static const char kClear[] = "\033[H\033[2J\033[?25l";
        ssize_t n = write(0, kClear, sizeof kClear - 1);
        (void)n;
    }
}

void platform_set_low_power(bool on) {
    // Screen off: stop the 30 Hz keyboard and redraw timers so the CPU can
    // stay idle; the main loop sleeps in poll() until a key arrives.
    if (on == g_low_power) return;
    g_low_power = on;
    lv_timer_t* timers[] = {g_keypad ? lv_indev_get_read_timer(g_keypad) : nullptr,
                            lv_display_get_refr_timer(lv_display_get_default())};
    for (lv_timer_t* t : timers) {
        if (!t) continue;
        if (on) lv_timer_pause(t);
        else lv_timer_resume(t);
    }
}

// Other files the main loop waits on with the keyboards (platform_watch_fd).
struct Watched {
    int fd;
    void (*fn)(void*);
    void* arg;
};
std::vector<Watched> g_watched;

void platform_watch_fd(int fd, void (*fn)(void*), void* arg) { g_watched.push_back({fd, fn, arg}); }

int platform_run() {
    while (!g_quit) {
        // Each pass round the loop tells systemd the UI is alive
        // (platform/watchdog.h). A stuck timer or event handler stops it.
        watchdog_kick();
        uint32_t ms = lv_timer_handler();
        // Sleep until the next LVGL timer is due or a key arrives, instead of
        // waking every few ms. Awake, keys are also read on LVGL's own timer.
        uint32_t cap = g_low_power ? 500 : 33;   // 500: pickup wake reads the IMU while off
        if (ms == LV_NO_TIMER_READY || ms > cap) ms = cap;
        std::vector<pollfd> ps;
        for (auto& k : g_kbs) ps.push_back({k.fd, POLLIN, 0});
        const size_t nkbs = ps.size();
        for (auto& w : g_watched) ps.push_back({w.fd, POLLIN, 0});
        if (!ps.empty()) {
            if (poll(ps.data(), ps.size(), int(ms)) > 0) {
                // The watched files first: close_keyboard() below changes g_kbs.
                for (size_t i = nkbs; i < ps.size(); ++i)
                    if (ps[i].revents & POLLIN) g_watched[i - nkbs].fn(g_watched[i - nkbs].arg);
                bool keys = false;
                for (size_t i = nkbs; i-- > 0;) {
                    if (ps[i].revents & (POLLERR | POLLHUP | POLLNVAL)) {
                        // Gone (driver unbound, Bluetooth disconnected):
                        // poll() would return at once forever. Another
                        // keyboard just goes; the built-in one is opened
                        // afresh by restarting (systemd).
                        if (!close_keyboard(i)) {
                            std::fprintf(stderr, "zeroshell: keyboard lost, restarting\n");
                            return 1;
                        }
                        g_cur_state = LV_INDEV_STATE_RELEASED;
                    } else if (ps[i].revents & POLLIN) {
                        keys = true;
                    }
                }
                if (keys && g_keypad) lv_indev_read(g_keypad);
            }
        } else {
            usleep(ms * 1000);
        }
        // A keyboard connected since (Bluetooth, USB), or the built-in one's
        // driver loading after we started. Not while the screen is off.
        if (!g_low_power && lv_tick_elaps(g_last_scan) >= (g_kbs.empty() ? 1000u : kRescanMs)) open_keyboards();
    }
    std::printf("zeroshell: stopping\n");
    return 0;
}

int platform_run_script(const std::string&) {
    std::fprintf(stderr, "--script needs the headless build\n");
    return 2;
}

#elif defined(__EMSCRIPTEN__)
// ---------------------------------------------------------------- web
// Browser demo: LVGL renders into an RGBA frame that the page copies to a
// canvas; the page feeds keys in through zs_web_evdev(). Simulated hardware.
namespace {
std::vector<uint8_t> g_rgba;
int g_w = 0, g_h = 0;
// Keys arrive as Linux key events, as on the device: the page runs a copy of
// M5's keyboard driver for the keys drawn on screen (built-in keyboard), and
// the computer's own keyboard counts as a USB keyboard plugged in. Each goes
// through the same KeyTranslator the device uses, so Shift/Fn/Sym, the Sym
// symbols, sticky Ctrl and held keys (hold Esc, hold Tab) all behave the same.
KeyTranslator g_builtin{KeyLayout::Cardputer};
KeyTranslator g_usb{KeyLayout::Us};
uint32_t g_cur = 0;
lv_indev_state_t g_state = LV_INDEV_STATE_RELEASED;
int g_dirty = 0;

void flush_cb(lv_display_t* d, const lv_area_t* a, uint8_t* px) {
    int w = lv_area_get_width(a);
    auto* src = reinterpret_cast<uint16_t*>(px);
    for (int y = a->y1; y <= a->y2; ++y) {
        for (int x = a->x1; x <= a->x2; ++x) {
            uint16_t c = src[(y - a->y1) * w + (x - a->x1)];
            uint8_t* o = &g_rgba[(y * g_w + x) * 4];
            o[0] = uint8_t(((c >> 11) & 0x1f) * 255 / 31);
            o[1] = uint8_t(((c >> 5) & 0x3f) * 255 / 63);
            o[2] = uint8_t((c & 0x1f) * 255 / 31);
            o[3] = 255;
        }
    }
    g_dirty = 1;
    lv_display_flush_ready(d);
}

// One press or release per read, as keyboard_read_cb on the device.
void read_cb(lv_indev_t*, lv_indev_data_t* data) {
    KeyEvent e;
    if (g_builtin.next(e) || g_usb.next(e)) {
        g_cur = e.key;
        g_state = e.pressed ? LV_INDEV_STATE_PRESSED : LV_INDEV_STATE_RELEASED;
    }
    data->key = g_cur;
    data->state = g_state;
    data->continue_reading = g_builtin.pending() || g_usb.pending();
}

uint32_t tick_cb() { return uint32_t(emscripten_get_now()); }

void write_file(const std::string& path, const std::string& text) {
    if (FILE* f = std::fopen(path.c_str(), "w")) {
        std::fputs(text.c_str(), f);
        std::fclose(f);
    }
}

// Demo files so Files and Notes have something to show.
void seed_home() {
    const char* home = "/home/pi";
    mkdir("/home", 0755);
    mkdir(home, 0755);
    for (const char* d : {"Documents", "Music", "Pictures", "Projects"})
        mkdir((std::string(home) + "/" + d).c_str(), 0755);
    write_file("/home/pi/notes.txt",
               "Shopping: solder, heat shrink, 18650s\n\nIdeas:\n- LoRa chat cap\n- IR macro for the TV\n");
    write_file("/home/pi/Projects/README.md", "# my project\n\nNotes about the build.\n");
    write_file("/home/pi/Documents/todo.txt", "- flash ZeroOS\n- learn TV remote\n- calibrate compass\n");
    write_file("/home/pi/Music/playlist.m3u", "#EXTM3U\n");
    setenv("HOME", home, 1);
}
}  // namespace

extern "C" {
// A Linux key event (value 1 down, 0 up) from the built-in keyboard (kbd 0)
// or a USB keyboard (kbd 1).
EMSCRIPTEN_KEEPALIVE void zs_web_evdev(int kbd, int code, int value) {
    (kbd == 0 ? g_builtin : g_usb).feed(uint16_t(code), value);
}
// The power switch: off stops ZeroShell where it is (the page blanks the screen).
EMSCRIPTEN_KEEPALIVE void zs_web_power(int on) {
    if (on) emscripten_resume_main_loop();
    else emscripten_pause_main_loop();
}
EMSCRIPTEN_KEEPALIVE uint8_t* zs_web_frame() { return g_rgba.data(); }
// Pretend HDMI monitor for trying Dock mode in the browser.
EMSCRIPTEN_KEEPALIVE void zs_web_hdmi(int connected) { mock_set_hdmi(connected != 0); }
EMSCRIPTEN_KEEPALIVE void zs_web_pose(int pose) { mock_set_pose(pose); }
// The apps built into this simulator, so the page's app guide can say which
// ones can be tried here.
EMSCRIPTEN_KEEPALIVE int zs_web_app_count() { return int(app_registry().size()); }
EMSCRIPTEN_KEEPALIVE const char* zs_web_app_id(int i) {
    const auto& apps = app_registry();
    return i >= 0 && i < int(apps.size()) ? apps[i].id.c_str() : "";
}
EMSCRIPTEN_KEEPALIVE const char* zs_web_app_name(int i) {
    const auto& apps = app_registry();
    return i >= 0 && i < int(apps.size()) ? apps[i].name.c_str() : "";
}
// For bug reports: which app is open ("" = home).
EMSCRIPTEN_KEEPALIVE const char* zs_web_current_app() { return Shell::get().current_app().c_str(); }
EMSCRIPTEN_KEEPALIVE int zs_web_take_dirty() {
    int d = g_dirty;
    g_dirty = 0;
    return d;
}
}

lv_indev_t* platform_init(int w, int h, int) {
    seed_home();
    g_w = w;
    g_h = h;
    g_rgba.assign(size_t(w) * h * 4, 255);
    lv_tick_set_cb(tick_cb);
    lv_display_t* d = lv_display_create(w, h);
    lv_display_set_color_format(d, LV_COLOR_FORMAT_RGB565);
    static std::vector<uint8_t> buf(size_t(w) * h * 2);
    lv_display_set_buffers(d, buf.data(), nullptr, buf.size(), LV_DISPLAY_RENDER_MODE_PARTIAL);
    lv_display_set_flush_cb(d, flush_cb);
    lv_indev_t* in = lv_indev_create();
    lv_indev_set_type(in, LV_INDEV_TYPE_KEYPAD);
    lv_indev_set_read_cb(in, read_cb);
    lv_indev_set_long_press_time(in, 60000);  // each tap is one press
    return in;
}

int platform_run() {
    emscripten_set_main_loop([] { lv_timer_handler(); }, 0, false);
    return 0;
}

int platform_run_script(const std::string&) { return 2; }

bool platform_capture(std::vector<uint8_t>& rgb, int& w, int& h) {
    if (g_rgba.empty()) return false;
    w = g_w;
    h = g_h;
    rgb.resize(size_t(w) * size_t(h) * 3);
    for (size_t i = 0, n = size_t(w) * size_t(h); i < n; ++i) {
        rgb[i * 3] = g_rgba[i * 4];
        rgb[i * 3 + 1] = g_rgba[i * 4 + 1];
        rgb[i * 3 + 2] = g_rgba[i * 4 + 2];
    }
    return true;
}
void platform_set_low_power(bool) {}
void platform_console_handoff(bool) {}
bool platform_ctrl_pending() { return g_builtin.ctrl_pending() || g_usb.ctrl_pending(); }

#else
// ---------------------------------------------------------------- headless
namespace {
std::vector<uint8_t> g_fb;
int g_w = 0, g_h = 0;
std::deque<uint32_t> g_keys;
bool g_pressed = false;
uint32_t g_cur = 0;

void flush_cb(lv_display_t* d, const lv_area_t* a, uint8_t* px) {
    // RGB565 partial buffer -> full RGB888 frame.
    int w = lv_area_get_width(a);
    auto* src = reinterpret_cast<uint16_t*>(px);
    for (int y = a->y1; y <= a->y2; ++y) {
        for (int x = a->x1; x <= a->x2; ++x) {
            uint16_t c = src[(y - a->y1) * w + (x - a->x1)];
            uint8_t* o = &g_fb[(y * g_w + x) * 3];
            o[0] = uint8_t(((c >> 11) & 0x1f) * 255 / 31);
            o[1] = uint8_t(((c >> 5) & 0x3f) * 255 / 63);
            o[2] = uint8_t((c & 0x1f) * 255 / 31);
        }
    }
    lv_display_flush_ready(d);
}

uint32_t g_now = 0;
// "hold:<ms>" holds the next key down that long (hold Esc goes home).
uint32_t g_next_hold = 0, g_release_at = 0;

// Each queued key becomes one press read followed by one release read.
void read_cb(lv_indev_t*, lv_indev_data_t* data) {
    if (g_pressed && g_now < g_release_at) {
        data->key = g_cur;
        data->state = LV_INDEV_STATE_PRESSED;
    } else if (g_pressed) {
        data->key = g_cur;
        data->state = LV_INDEV_STATE_RELEASED;
        g_pressed = false;
    } else if (!g_keys.empty()) {
        g_cur = g_keys.front();
        g_keys.pop_front();
        data->key = g_cur;
        data->state = LV_INDEV_STATE_PRESSED;
        g_pressed = true;
        g_release_at = g_now + g_next_hold;
        g_next_hold = 0;
    } else {
        data->key = g_cur;
        data->state = LV_INDEV_STATE_RELEASED;
    }
    data->continue_reading = false;
}

uint32_t tick_cb() { return g_now; }

void run_for(uint32_t ms) {
    for (uint32_t t = 0; t < ms; t += 5) {
        g_now += 5;
        lv_timer_handler();
    }
}

void drain_keys() {
    while (!g_keys.empty() || g_pressed) run_for(40);
    run_for(100);
}

// Draw everything again so g_fb is current.
void redraw_all() {
    lv_obj_invalidate(lv_screen_active());
    lv_obj_invalidate(lv_layer_top());
    lv_refr_now(nullptr);
}

bool save_ppm(const std::string& path) {
    redraw_all();
    FILE* f = std::fopen(path.c_str(), "wb");
    if (!f) return false;
    std::fprintf(f, "P6\n%d %d\n255\n", g_w, g_h);
    std::fwrite(g_fb.data(), 1, g_fb.size(), f);
    std::fclose(f);
    std::printf("saved %s\n", path.c_str());
    return true;
}
}  // namespace

lv_indev_t* platform_init(int w, int h, int) {
    g_w = w;
    g_h = h;
    g_fb.assign(size_t(w) * h * 3, 0);
    lv_tick_set_cb(tick_cb);
    play_clock_source() = [] { return long(g_now); };
    lv_display_t* d = lv_display_create(w, h);
    lv_display_set_color_format(d, LV_COLOR_FORMAT_RGB565);
    static std::vector<uint8_t> buf(size_t(w) * h * 2);
    lv_display_set_buffers(d, buf.data(), nullptr, buf.size(), LV_DISPLAY_RENDER_MODE_PARTIAL);
    lv_display_set_flush_cb(d, flush_cb);
    lv_indev_t* in = lv_indev_create();
    lv_indev_set_type(in, LV_INDEV_TYPE_KEYPAD);
    lv_indev_set_read_cb(in, read_cb);
    return in;
}

int platform_run() {
    std::fprintf(stderr, "headless build: use --script\n");
    return 2;
}

bool platform_capture(std::vector<uint8_t>& rgb, int& w, int& h) {
    if (g_fb.empty()) return false;
    redraw_all();
    rgb = g_fb;
    w = g_w;
    h = g_h;
    return true;
}

int platform_run_script(const std::string& script) {
    run_for(300);
    std::stringstream ss(script);
    std::string step;
    int failures = 0;
    while (std::getline(ss, step, ',')) {
        if (step.empty()) continue;
        std::string arg;
        auto colon = step.find(':');
        std::string cmd = step;
        if (colon != std::string::npos) {
            cmd = step.substr(0, colon);
            arg = step.substr(colon + 1);
        }
        if (cmd == "right") g_keys.push_back(LV_KEY_RIGHT);
        else if (cmd == "left") g_keys.push_back(LV_KEY_LEFT);
        else if (cmd == "up") g_keys.push_back(LV_KEY_UP);
        else if (cmd == "down") g_keys.push_back(LV_KEY_DOWN);
        else if (cmd == "enter") g_keys.push_back(LV_KEY_ENTER);
        else if (cmd == "esc") g_keys.push_back(LV_KEY_ESC);
        else if (cmd == "bksp") g_keys.push_back(LV_KEY_BACKSPACE);
        else if (cmd == "tab") g_keys.push_back(9);
        else if (cmd == "prtsc") g_keys.push_back(kKeyPrintScreen);
        else if (cmd == "help") g_keys.push_back(kKeyHelp);
        else if (cmd == "ctrl" && !arg.empty()) g_keys.push_back(ctrl_key(arg == "space" ? ' ' : arg[0]));
        else if (cmd == "hold") g_next_hold = uint32_t(std::atoi(arg.c_str()));
        else if (cmd == "type") for (char ch : arg) g_keys.push_back(uint32_t((unsigned char)ch));
        else if (cmd == "key" && !arg.empty()) g_keys.push_back(uint32_t((unsigned char)arg[0]));
        else if (cmd == "open") { drain_keys(); shell_open_app(arg); run_for(100); }
        else if (cmd == "home") { drain_keys(); shell_go_home(); run_for(100); }
        else if (cmd == "hdmi") { mock_set_hdmi(arg == "1"); run_for(100); }
        else if (cmd == "pose") { mock_set_pose(std::atoi(arg.c_str())); run_for(100); }
        else if (cmd == "wait") run_for(uint32_t(std::atoi(arg.c_str())));
        else if (cmd == "live") {   // wait in real time: for real child work (--real-hal video)
            for (int t = 0; t < std::atoi(arg.c_str()); t += 5) {
                usleep(5000);
                run_for(5);
            }
        }
        else if (cmd == "shot") { drain_keys(); run_for(400); if (!save_ppm(arg)) ++failures; }
        else { std::fprintf(stderr, "unknown script step: %s\n", step.c_str()); ++failures; }
        drain_keys();
    }
    return failures ? 1 : 0;
}
void platform_set_low_power(bool) {}
void platform_console_handoff(bool) {}
bool platform_ctrl_pending() { return false; }

#endif

#if !ZS_BACKEND_DEVICE
// Elsewhere (the simulator, the headless and web builds) there is no poll()
// in the main loop to add the file to: look once a second instead. fn must
// cope with nothing to read (the file is non-blocking).
void platform_watch_fd(int fd, void (*fn)(void*), void* arg) {
    struct Ctx {
        void (*fn)(void*);
        void* arg;
    };
    lv_timer_create([](lv_timer_t* t) {
        auto* c = static_cast<Ctx*>(lv_timer_get_user_data(t));
        c->fn(c->arg);
    }, 1000, new Ctx{fn, arg});
    (void)fd;
}
#endif

}  // namespace zs
