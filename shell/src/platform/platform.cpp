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

#elif ZS_BACKEND_DEVICE
namespace {
lv_indev_t* g_keypad = nullptr;
int g_kb_fd = -1;
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

std::string find_keyboard() {
    if (const char* env = std::getenv("ZS_KEYBOARD")) return env;
    std::string fallback;
    for (int i = 0; i < 16; ++i) {
        std::string path = "/dev/input/event" + std::to_string(i);
        int fd = open(path.c_str(), O_RDONLY | O_NONBLOCK);
        if (fd < 0) continue;
        char name[128] = {0};
        ioctl(fd, EVIOCGNAME(sizeof name - 1), name);
        unsigned long keys[KEY_MAX / (8 * sizeof(long)) + 1] = {0};
        ioctl(fd, EVIOCGBIT(EV_KEY, sizeof keys), keys);
        close(fd);
        bool has_a = keys[KEY_A / (8 * sizeof(long))] & (1UL << (KEY_A % (8 * sizeof(long))));
        if (is_builtin_name(name)) return path;
        if (has_a && fallback.empty()) fallback = path;
    }
    return fallback;   // empty: no keyboard yet (its driver can load after we start)
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
KeyTranslator g_translator;
uint32_t g_cur_key = 0;
lv_indev_state_t g_cur_state = LV_INDEV_STATE_RELEASED;

bool open_keyboard() {
    std::string kb = find_keyboard();
    if (kb.empty()) return false;
    g_kb_fd = open(kb.c_str(), O_RDONLY | O_NONBLOCK | O_NOCTTY | O_CLOEXEC);
    if (g_kb_fd < 0) {
        std::fprintf(stderr, "zeroshell: cannot open keyboard %s: %s\n", kb.c_str(), std::strerror(errno));
        return false;
    }
    std::printf("zeroshell: keyboard %s\n", kb.c_str());
    g_translator = KeyTranslator(keyboard_layout(g_kb_fd));
    std::fflush(stdout);
    return true;
}

void keyboard_read_cb(lv_indev_t*, lv_indev_data_t* data) {
    input_event ev;
    while (g_kb_fd >= 0 && read(g_kb_fd, &ev, sizeof ev) == ssize_t(sizeof ev)) {
        if (ev.type == EV_KEY) g_translator.feed(ev.code, ev.value);
        else if (ev.type == EV_SYN && ev.code == SYN_DROPPED) g_translator.reset();
    }
    // One press or release per read; LVGL reads again while more wait, so
    // fast typing is not lost.
    KeyEvent e;
    if (g_translator.next(e)) {
        g_cur_key = e.key;
        g_cur_state = e.pressed ? LV_INDEV_STATE_PRESSED : LV_INDEV_STATE_RELEASED;
    }
    data->key = g_cur_key;
    data->state = g_cur_state;
    data->continue_reading = g_translator.pending();
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

lv_indev_t* platform_init(int, int, int) {
    std::string fb = open_lcd_or_exit();
    tty_graphics(true);
    std::atexit([] { tty_graphics(false); });
    install_quit_handlers();
    lv_display_t* d = lv_linux_fbdev_create();
    lv_linux_fbdev_set_file(d, fb.c_str());
    if (!open_keyboard()) std::fprintf(stderr, "zeroshell: no keyboard yet, will keep looking\n");
    lv_indev_t* in = lv_indev_create();
    lv_indev_set_type(in, LV_INDEV_TYPE_KEYPAD);
    lv_indev_set_read_cb(in, keyboard_read_cb);
    // Keys would also reach the Linux console on tty1 behind us (a known
    // stock-image problem); the image masks getty@tty1 to prevent that.
    g_keypad = in;
    return in;
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

int platform_run() {
    while (!g_quit) {
        uint32_t ms = lv_timer_handler();
        // Sleep until the next LVGL timer is due or a key arrives, instead of
        // waking every few ms. Awake, keys are also read on LVGL's own timer.
        uint32_t cap = g_low_power ? 500 : 33;   // 500: pickup wake reads the IMU while off
        if (ms == LV_NO_TIMER_READY || ms > cap) ms = cap;
        if (g_kb_fd >= 0) {
            pollfd p{g_kb_fd, POLLIN, 0};
            if (poll(&p, 1, int(ms)) > 0) {
                if (p.revents & (POLLERR | POLLHUP | POLLNVAL)) {
                    // The keyboard went away (driver unbound): poll() would
                    // return at once forever. Exit; systemd restarts us and
                    // we open the keyboard afresh.
                    std::fprintf(stderr, "zeroshell: keyboard lost, restarting\n");
                    return 1;
                }
                if (g_keypad) lv_indev_read(g_keypad);
            }
        } else {
            // No keyboard at start (driver not loaded yet): look again
            // about once a second.
            usleep(ms * 1000);
            static uint32_t last_try = 0;
            if (lv_tick_elaps(last_try) >= 1000) {
                last_try = lv_tick_get();
                open_keyboard();
            }
        }
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
// canvas; the page feeds keys in through zs_web_key(). Simulated hardware.
namespace {
std::vector<uint8_t> g_rgba;
int g_w = 0, g_h = 0;
std::deque<uint32_t> g_keys;
bool g_pressed = false;
uint32_t g_cur = 0;
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

void read_cb(lv_indev_t*, lv_indev_data_t* data) {
    if (g_pressed) {
        data->key = g_cur;
        data->state = LV_INDEV_STATE_RELEASED;
        g_pressed = false;
    } else if (!g_keys.empty()) {
        g_cur = g_keys.front();
        g_keys.pop_front();
        data->key = g_cur;
        data->state = LV_INDEV_STATE_PRESSED;
        g_pressed = true;
    } else {
        data->key = g_cur;
        data->state = LV_INDEV_STATE_RELEASED;
    }
    // Drain queued keys within one LVGL cycle so fast typing keeps up.
    data->continue_reading = g_pressed || !g_keys.empty();
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
EMSCRIPTEN_KEEPALIVE void zs_web_key(uint32_t key) { g_keys.push_back(key); }
EMSCRIPTEN_KEEPALIVE uint8_t* zs_web_frame() { return g_rgba.data(); }
// Open an app by its position in the launcher (-1 = home).
EMSCRIPTEN_KEEPALIVE void zs_web_open(int index) {
    const auto& apps = app_registry();
    if (index >= 0 && index < int(apps.size())) shell_open_app(apps[index].id);
    else shell_go_home();
}
// Pretend HDMI monitor for trying Dock mode in the browser.
EMSCRIPTEN_KEEPALIVE void zs_web_hdmi(int connected) { mock_set_hdmi(connected != 0); }
EMSCRIPTEN_KEEPALIVE void zs_web_pose(int pose) { mock_set_pose(pose); }
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
void platform_set_low_power(bool) {}

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

// Each queued key becomes one press read followed by one release read.
void read_cb(lv_indev_t*, lv_indev_data_t* data) {
    if (g_pressed) {
        data->key = g_cur;
        data->state = LV_INDEV_STATE_RELEASED;
        g_pressed = false;
    } else if (!g_keys.empty()) {
        g_cur = g_keys.front();
        g_keys.pop_front();
        data->key = g_cur;
        data->state = LV_INDEV_STATE_PRESSED;
        g_pressed = true;
    } else {
        data->key = g_cur;
        data->state = LV_INDEV_STATE_RELEASED;
    }
    data->continue_reading = false;
}

uint32_t g_now = 0;
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

bool save_ppm(const std::string& path) {
    lv_obj_invalidate(lv_screen_active());
    lv_refr_now(nullptr);
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
        else if (cmd == "type") for (char ch : arg) g_keys.push_back(uint32_t((unsigned char)ch));
        else if (cmd == "key" && !arg.empty()) g_keys.push_back(uint32_t((unsigned char)arg[0]));
        else if (cmd == "open") { drain_keys(); shell_open_app(arg); run_for(100); }
        else if (cmd == "home") { drain_keys(); shell_go_home(); run_for(100); }
        else if (cmd == "hdmi") { mock_set_hdmi(arg == "1"); run_for(100); }
        else if (cmd == "pose") { mock_set_pose(std::atoi(arg.c_str())); run_for(100); }
        else if (cmd == "wait") run_for(uint32_t(std::atoi(arg.c_str())));
        else if (cmd == "shot") { drain_keys(); run_for(400); if (!save_ppm(arg)) ++failures; }
        else { std::fprintf(stderr, "unknown script step: %s\n", step.c_str()); ++failures; }
        drain_keys();
    }
    return failures ? 1 : 0;
}
void platform_set_low_power(bool) {}

#endif

}  // namespace zs
