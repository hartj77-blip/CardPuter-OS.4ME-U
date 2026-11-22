// Serial: a serial monitor for boards on the USB port (Arduino, ESP32,
// Raspberry Pi Pico...) and the Grove/GPIO UART. Pick the port, set the speed,
// watch what the board prints and type lines to send it.
//
//   Ports     Enter open      Left/Right speed      E line ending
//   Monitor   type + Enter send      Tab reset the board (RTS)      Esc close
#include <cerrno>
#include <cstring>
#include <string>
#include <vector>

#ifndef __EMSCRIPTEN__
#include <fcntl.h>
#include <sys/ioctl.h>
#include <termios.h>
#include <unistd.h>
#endif

#include "apps/common.h"
#include "apps/kit/run.h"
#include "apps/kit/ui.h"

namespace zs {
namespace {
using namespace theme;

const int kBauds[] = {9600, 19200, 38400, 57600, 115200, 230400, 460800, 921600};
const char* kEndings[] = {"\\r\\n", "\\n", "none"};

struct Port {
    std::string dev;    // /dev/ttyUSB0
    std::string about;  // "CP2102 USB to UART", "GPIO UART"
};

std::string trim(std::string s) {
    while (!s.empty() && (s.back() == '\n' || s.back() == '\r' || s.back() == ' ')) s.pop_back();
    return s;
}

// The USB product name of a tty, from sysfs (walks up to the USB device).
std::string product_of(const std::string& tty) {
    std::string base = "/sys/class/tty/" + tty + "/device";
    for (int up = 0; up < 4; ++up) {
        std::string p = trim(kit::read_file(base + "/product", 256));
        if (!p.empty()) return p;
        base += "/..";
    }
    return "";
}

std::vector<Port> find_ports() {
    if (kit::simulated())
        return {{"/dev/ttyUSB0", "CP2102 USB to UART Bridge (ESP32)"},
                {"/dev/ttyACM0", "Raspberry Pi Pico"},
                {"/dev/serial0", "GPIO / Grove UART"}};
    std::vector<Port> v;
    for (auto& n : kit::list_files("/dev")) {
        bool usb = n.rfind("ttyUSB", 0) == 0 || n.rfind("ttyACM", 0) == 0;
        if (!usb) continue;
        std::string p = product_of(n);
        v.push_back({"/dev/" + n, p.empty() ? "USB serial" : p});
    }
    if (kit::exists("/dev/serial0")) v.push_back({"/dev/serial0", "GPIO / Grove UART"});
    return v;
}

#ifndef __EMSCRIPTEN__
speed_t speed_of(int baud) {
    switch (baud) {
        case 9600: return B9600;
        case 19200: return B19200;
        case 38400: return B38400;
        case 57600: return B57600;
        case 230400: return B230400;
        case 460800: return B460800;
        case 921600: return B921600;
        default: return B115200;
    }
}
#endif

// What a simulated ESP32 prints: its boot log, then a reading a second.
std::vector<std::string> sim_boot() {
    return {"ets Jul 29 2019 12:21:46", "rst:0x1 (POWERON_RESET),boot:0x13 (SPI_FAST_FLASH_BOOT)",
            "load:0x3fff0030,len:1184", "entry 0x400805f0", "", "weather-station v0.3 ready",
            "wifi: connecting to home-net...", "wifi: connected, 192.168.1.57"};
}

class SerialApp : public App {
public:
    ~SerialApp() override { close_port(); }

    void build(lv_obj_t* root) override {
        root_ = root;
        show_ports();
    }

    void tick() override {
        if (pending_row_ >= 0) {
            int i = pending_row_;
            pending_row_ = -1;
            if (size_t(i) < ports_.size()) open_port(ports_[size_t(i)]);
            else show_ports();   // "Look again"
            return;
        }
        if (pending_send_) {
            pending_send_ = false;
            send(typed_);
            return;
        }
        if (page_ == Page::Monitor) poll();
    }

    bool on_key(uint32_t key) override {
        if (page_ == Page::Ports) {
            if (key == LV_KEY_LEFT || key == LV_KEY_RIGHT) {
                int n = int(std::size(kBauds));
                baud_i_ = (baud_i_ + (key == LV_KEY_RIGHT ? 1 : n - 1)) % n;
                update_baud();
                return true;
            }
            if (key == 'e' || key == 'E') {
                end_i_ = (end_i_ + 1) % int(std::size(kEndings));
                update_baud();
                return true;
            }
            return false;
        }
        if (key == LV_KEY_NEXT) {   // Tab: reset the board (letters are for typing)
            reset_board();
            return true;
        }
        return false;
    }

    bool on_back() override {
        if (page_ == Page::Monitor) {
            close_port();
            show_ports();
            return true;
        }
        return false;
    }

private:
    enum class Page { Ports, Monitor };

    void clear() {
        lv_obj_clean(root_);
        if (group) lv_group_remove_all_objs(group);
        ta_ = title_ = baud_ = nullptr;
    }

    static void on_click(lv_event_t* e) {
        auto* self = static_cast<SerialApp*>(lv_event_get_user_data(e));
        self->pending_row_ = int(lv_obj_get_index(lv_event_get_target_obj(e)));
    }

    // ---- Ports
    void show_ports() {
        page_ = Page::Ports;
        clear();
        kit::title_row(root_, "Serial ports", "");
        baud_ = label(root_, "", &zs_font_12, kMuted);
        lv_obj_align(baud_, LV_ALIGN_TOP_RIGHT, -4, 3);
        update_baud();
        ports_ = find_ports();
        lv_obj_t* list = make_list(root_, 0, 18, kScreenW, kContentH - 18);
        for (auto& p : ports_) {
            std::string text = p.dev.substr(5) + "   " + p.about;
            list_button(list, LV_SYMBOL_USB, text.c_str(), on_click, this);
        }
        list_button(list, LV_SYMBOL_REFRESH,
                    ports_.empty() ? "No boards found. Plug one in, then look again" : "Look again",
                    on_click, this);
        finish_list(list);
    }

    void update_baud() {
        if (baud_)
            lv_label_set_text(baud_, fmt(LV_SYMBOL_LEFT " %d " LV_SYMBOL_RIGHT "   E ends %s", kBauds[baud_i_],
                                         kEndings[end_i_]).c_str());
    }

    // ---- Monitor
    void open_port(const Port& p) {
        close_port();
        port_ = p;
        page_ = Page::Monitor;
        clear();
        title_ = kit::title_row(root_, "", "Tab reset  Esc close");
        update_title();
        out_.build(root_, 18, 30);

        ta_ = lv_textarea_create(root_);
        lv_textarea_set_one_line(ta_, true);
        lv_textarea_set_placeholder_text(ta_, "type, Enter sends");
        lv_obj_set_size(ta_, kScreenW - 8, 26);
        lv_obj_align(ta_, LV_ALIGN_BOTTOM_LEFT, 4, -2);
        lv_obj_set_style_bg_color(ta_, c(kSurface), 0);
        lv_obj_set_style_border_color(ta_, c(kAccent), 0);
        lv_obj_set_style_border_width(ta_, 1, 0);
        lv_obj_set_style_pad_ver(ta_, 4, 0);
        lv_obj_add_event_cb(ta_, [](lv_event_t* e) {
            auto* self = static_cast<SerialApp*>(lv_event_get_user_data(e));
            self->typed_ = lv_textarea_get_text(self->ta_);
            lv_textarea_set_text(self->ta_, "");
            self->pending_send_ = true;
        }, LV_EVENT_READY, this);
        shell_add_focus(ta_);
        lv_group_focus_obj(ta_);
        if (group) lv_group_set_editing(group, true);

        if (kit::simulated()) {
            sim_.reset();
            sim_.start({}, {"", {}, sim_boot()});
            sim_next_ = lv_tick_get() + 2500;
            out_.set_placeholder("Opening " + port_.dev + "...");
            return;
        }
#ifndef __EMSCRIPTEN__
        fd_ = open(port_.dev.c_str(), O_RDWR | O_NOCTTY | O_NONBLOCK | O_CLOEXEC);
        if (fd_ < 0) {
            out_.append("Could not open " + port_.dev + ": " + std::strerror(errno));
            if (errno == EACCES) out_.append("Your user needs to be in the dialout group.");
            return;
        }
        termios t{};
        tcgetattr(fd_, &t);
        cfmakeraw(&t);
        t.c_cflag |= CLOCAL | CREAD;
        t.c_cflag &= ~(CRTSCTS | CSTOPB | PARENB);
        cfsetispeed(&t, speed_of(kBauds[baud_i_]));
        cfsetospeed(&t, speed_of(kBauds[baud_i_]));
        tcsetattr(fd_, TCSANOW, &t);
        tcflush(fd_, TCIFLUSH);
        out_.set_placeholder("Listening on " + port_.dev + fmt(" at %d baud", kBauds[baud_i_]));
#endif
    }

    void update_title() {
        if (!title_) return;
        lv_label_set_text(title_, (port_.dev.substr(5) + fmt("  %d  ", kBauds[baud_i_]) + kEndings[end_i_]).c_str());
    }

    void close_port() {
#ifndef __EMSCRIPTEN__
        if (fd_ >= 0) close(fd_);
#endif
        fd_ = -1;
        partial_.clear();
        sim_.reset();
    }

    void line_in(const std::string& raw) {
        std::string s;
        for (char ch : raw) {
            if (ch == '\r') continue;
            s += (static_cast<unsigned char>(ch) < 0x20 && ch != '\t') ? '.' : ch;
        }
        out_.append(s);
    }

    void poll() {
        if (kit::simulated()) {
            std::vector<std::string> lines;
            if (sim_.drain(lines)) for (auto& l : lines) line_in(l);
            if (!sim_.running() && lv_tick_get() >= sim_next_) {   // a reading a second
                sim_next_ = lv_tick_get() + 1000;
                ++sim_n_;
                line_in(fmt("t=%d temp=%.1fC hum=%d%% pres=%d.%dhPa", sim_n_, 21.4 + (sim_n_ % 5) * 0.1,
                            48 + sim_n_ % 3, 1013, sim_n_ % 10));
            }
            return;
        }
#ifndef __EMSCRIPTEN__
        if (fd_ < 0) return;
        char buf[4096];
        for (int rounds = 0; rounds < 8; ++rounds) {
            ssize_t n = read(fd_, buf, sizeof buf);
            if (n <= 0) {
                if (n == 0 || (errno != EAGAIN && errno != EINTR)) {
                    out_.append("-- " + port_.dev + " went away (unplugged?) --");
                    close(fd_);
                    fd_ = -1;
                }
                break;
            }
            partial_.append(buf, size_t(n));
            size_t nl;
            while ((nl = partial_.find('\n')) != std::string::npos) {
                line_in(partial_.substr(0, nl));
                partial_.erase(0, nl + 1);
            }
            last_rx_ = lv_tick_get();
        }
        // A prompt with no newline ("> "): show it after a pause.
        if (!partial_.empty() && lv_tick_elaps(last_rx_) > 300) {
            line_in(partial_);
            partial_.clear();
        }
#endif
    }

    void send(const std::string& text) {
        static const char* kBytes[] = {"\r\n", "\n", ""};
        std::string data = text + kBytes[end_i_];
        out_.append("> " + text);
        if (kit::simulated()) {
            line_in("ok: " + text);
            return;
        }
#ifndef __EMSCRIPTEN__
        if (fd_ < 0) return;
        size_t off = 0;
        for (int tries = 0; off < data.size() && tries < 50; ++tries) {
            ssize_t n = write(fd_, data.data() + off, data.size() - off);
            if (n > 0) off += size_t(n);
            else usleep(2000);
        }
#endif
    }

    // ESP32/Arduino auto-reset: EN/RESET is wired to RTS (and DTR) on most boards.
    void reset_board() {
        out_.append("-- reset --");
        if (kit::simulated()) {
            sim_.reset();
            sim_.start({}, {"", {}, sim_boot()});
            sim_next_ = lv_tick_get() + 2500;
            return;
        }
#ifndef __EMSCRIPTEN__
        if (fd_ < 0) return;
        // As esptool does: DTR off (GPIO0 high: run, not flash), RTS on
        // (EN low: in reset) for 100 ms, then RTS off.
        int dtr = TIOCM_DTR, rts = TIOCM_RTS;
        ioctl(fd_, TIOCMBIC, &dtr);
        ioctl(fd_, TIOCMBIS, &rts);
        usleep(100 * 1000);
        ioctl(fd_, TIOCMBIC, &rts);
#endif
    }

    lv_obj_t* root_ = nullptr;
    lv_obj_t* title_ = nullptr;
    lv_obj_t* baud_ = nullptr;
    lv_obj_t* ta_ = nullptr;
    Page page_ = Page::Ports;
    std::vector<Port> ports_;
    Port port_;
    kit::OutputView out_;
    kit::Job sim_;          // the simulated board's boot log
    uint32_t sim_next_ = 0;
    int sim_n_ = 0;
    int fd_ = -1;
    int baud_i_ = 4;        // 115200
    int end_i_ = 0;
    std::string partial_, typed_;
    uint32_t last_rx_ = 0;
    int pending_row_ = -1;
    bool pending_send_ = false;
};
}  // namespace

std::unique_ptr<App> make_serial_app() { return std::make_unique<SerialApp>(); }

}  // namespace zs
