#include "apps/games/emu.h"

#include <algorithm>
#include <cmath>
#include <cstdarg>
#include <cstdio>
#include <cstring>
#include <fstream>

#ifndef __EMSCRIPTEN__
#include <dlfcn.h>
#include <fcntl.h>
#include <pthread.h>
#include <signal.h>
#include <sys/wait.h>
#include <unistd.h>
#endif

#include <chrono>

#include "apps/games/systems.h"
#include "apps/kit/run.h"
#include "lvgl.h"

namespace zs::games {
namespace {
using namespace retro;
using Clock = std::chrono::steady_clock;

Emulator* g_emu = nullptr;   // the running one: libretro callbacks have no user pointer

bool cb_env(unsigned cmd, void* data) { return g_emu && g_emu->env(cmd, data); }
void cb_video(const void* d, unsigned w, unsigned h, size_t pitch) {
    if (g_emu) g_emu->video(d, w, h, pitch);
}
void cb_sample(int16_t l, int16_t r) {
    int16_t s[2] = {l, r};
    if (g_emu) g_emu->audio(s, 1);
}
size_t cb_batch(const int16_t* d, size_t frames) {
    if (g_emu) g_emu->audio(d, frames);
    return frames;
}
void cb_poll() {}
int16_t cb_input(unsigned port, unsigned device, unsigned index, unsigned id) {
    return g_emu ? g_emu->input(port, device, index, id) : 0;
}
void cb_log(LogLevel level, const char* fmt, ...) {
    if (level < kLogWarn) return;
    va_list ap;
    va_start(ap, fmt);
    std::fprintf(stderr, "games: core: ");
    std::vfprintf(stderr, fmt, ap);
    va_end(ap);
}

std::vector<uint8_t> read_all(const std::string& path, size_t max) {
    std::ifstream f(path, std::ios::binary);
    if (!f) return {};
    f.seekg(0, std::ios::end);
    auto n = f.tellg();
    if (n <= 0 || size_t(n) > max) return {};
    f.seekg(0);
    std::vector<uint8_t> v(static_cast<size_t>(n));
    f.read(reinterpret_cast<char*>(v.data()), n);
    return v;
}

bool write_all(const std::string& path, const void* data, size_t n) {
    std::string tmp = path + ".tmp";
    {
        std::ofstream f(tmp, std::ios::binary | std::ios::trunc);
        if (!f) return false;
        f.write(static_cast<const char*>(data), std::streamsize(n));
        if (!f) return false;
    }
    return std::rename(tmp.c_str(), path.c_str()) == 0;   // never half a save
}
}  // namespace

Emulator::~Emulator() { stop(); }

std::string Emulator::base_path(const std::string& dir, const char* ext) const {
    return dir + "/" + game_title(game_) + ext;
}

bool Emulator::load(const std::string& core_path, const std::string& game, int max_w, int max_h,
                    std::string& error) {
#ifdef __EMSCRIPTEN__
    (void)core_path; (void)game; (void)max_w; (void)max_h;
    error = "Libretro can't run in the browser simulator.";
    return false;
#else
    stop();
    core_path_ = core_path;
    game_ = game;
    max_w_ = max_w;
    max_h_ = max_h;
    lib_ = dlopen(core_path.c_str(), RTLD_NOW | RTLD_LOCAL);
    if (!lib_) {
        const char* e = dlerror();
        error = std::string("Could not load the core: ") + (e ? e : "?");
        return false;
    }
    bool ok = true;
    auto sym = [&](const char* name) {
        void* p = dlsym(lib_, name);
        if (!p) ok = false;
        return p;
    };
#define ZS_SYM(field, name) api_.field = reinterpret_cast<decltype(api_.field)>(sym(name))
    ZS_SYM(set_environment, "retro_set_environment");
    ZS_SYM(set_video_refresh, "retro_set_video_refresh");
    ZS_SYM(set_audio_sample, "retro_set_audio_sample");
    ZS_SYM(set_audio_sample_batch, "retro_set_audio_sample_batch");
    ZS_SYM(set_input_poll, "retro_set_input_poll");
    ZS_SYM(set_input_state, "retro_set_input_state");
    ZS_SYM(init, "retro_init");
    ZS_SYM(deinit, "retro_deinit");
    ZS_SYM(api_version, "retro_api_version");
    ZS_SYM(get_system_info, "retro_get_system_info");
    ZS_SYM(get_system_av_info, "retro_get_system_av_info");
    ZS_SYM(set_controller_port_device, "retro_set_controller_port_device");
    ZS_SYM(reset, "retro_reset");
    ZS_SYM(run, "retro_run");
    ZS_SYM(serialize_size, "retro_serialize_size");
    ZS_SYM(serialize, "retro_serialize");
    ZS_SYM(unserialize, "retro_unserialize");
    ZS_SYM(load_game, "retro_load_game");
    ZS_SYM(unload_game, "retro_unload_game");
    ZS_SYM(get_memory_data, "retro_get_memory_data");
    ZS_SYM(get_memory_size, "retro_get_memory_size");
#undef ZS_SYM
    if (!ok || api_.api_version() != kApiVersion) {
        error = "That file is not a libretro core this app can use.";
        dlclose(lib_);
        lib_ = nullptr;
        return false;
    }

    sys_dir_ = system_dir();
    save_dir_ = saves_dir();
    kit::make_dirs(sys_dir_);
    kit::make_dirs(save_dir_);
    kit::make_dirs(states_dir());
    fmt_ = k0RGB1555;
    quit_req_ = false;
    g_emu = this;
    api_.set_environment(cb_env);
    api_.init();
    api_.set_video_refresh(cb_video);
    api_.set_audio_sample(cb_sample);
    api_.set_audio_sample_batch(cb_batch);
    api_.set_input_poll(cb_poll);
    api_.set_input_state(cb_input);

    SystemInfo si{};
    api_.get_system_info(&si);
    core_name_ = si.library_name ? si.library_name : "core";
    GameInfo gi{game_.c_str(), nullptr, 0, nullptr};
    if (!si.need_fullpath) {
        rom_ = read_all(game_, size_t(128) << 20);
        if (rom_.empty()) {
            error = "Could not read the game file.";
            api_.deinit();
            dlclose(lib_);
            lib_ = nullptr;
            g_emu = nullptr;
            return false;
        }
        gi.data = rom_.data();
        gi.size = rom_.size();
    }
    if (!api_.load_game(&gi)) {
        error = core_name_ + " could not start this game. Is it the right file for this system?";
        api_.deinit();
        dlclose(lib_);
        lib_ = nullptr;
        g_emu = nullptr;
        return false;
    }
    game_loaded_ = true;
    api_.get_system_av_info(&av_);
    fps_ = av_.timing.fps > 1 ? av_.timing.fps : 60;
    fit(av_.geometry.aspect_ratio);
    api_.set_controller_port_device(0, kDeviceJoypad);

    // The game's battery save.
    if (size_t n = api_.get_memory_size(kMemorySaveRam); n > 0) {
        auto* mem = static_cast<uint8_t*>(api_.get_memory_data(kMemorySaveRam));
        auto file = read_all(base_path(save_dir_, ".srm"), n);
        if (mem && file.size() == n) std::memcpy(mem, file.data(), n);
        if (mem) sram_copy_.assign(mem, mem + n);
    }
    if (!kit::simulated()) open_audio(av_.timing.sample_rate);
    pad_.open();
    return true;
#endif
}

void Emulator::fit(double aspect) {
    unsigned bw = av_.geometry.base_width ? av_.geometry.base_width : 256;
    unsigned bh = av_.geometry.base_height ? av_.geometry.base_height : 224;
    if (aspect <= 0) aspect = double(bw) / bh;
    int w, h;
    if (double(max_w_) / max_h_ > aspect) {
        h = max_h_;
        w = int(std::lround(max_h_ * aspect));
    } else {
        w = max_w_;
        h = int(std::lround(max_w_ / aspect));
    }
    // Just a little bigger than the console's own picture: 1:1 is sharper.
    double scale = double(h) / bh;
    if (scale > 1.0 && scale < 1.2 && int(bw) <= max_w_ && int(bh) <= max_h_) {
        w = int(bw);
        h = int(bh);
    }
    dst_w_ = std::max(1, std::min(w, max_w_));
    dst_h_ = std::max(1, std::min(h, max_h_));
    map_src_w_ = map_src_h_ = 0;   // rebuild the scaling tables on the next frame
}

void Emulator::start(bool threaded) {
    if (!game_loaded_ || started_) return;
    started_ = true;
    threaded_ = threaded;
    stop_ = false;
    paused_ = false;
    step_at_ = -1;
#ifndef __EMSCRIPTEN__
    if (threaded_) thread_ = std::thread([this] { loop(); });
#endif
}

// One frame: read the controls, run the core, send the sound on.
void Emulator::run_frame() {
    pad_.poll();
    uint16_t b = pad_.buttons();
    if (hold_.load()) {
        if (b == 0) hold_ = false;
        else b = 0;
    }
    buttons_ = b;
    if (paused_.load()) return;
    {
        std::lock_guard<std::mutex> lock(core_mu_);
        api_.run();
    }
    flush_audio();
}

void Emulator::step(uint32_t now_ms) {
    if (!started_ || threaded_ || stop_.load()) return;
    double period = 1000.0 / fps_;
    if (step_at_ < 0 || now_ms - step_at_ > 250) step_at_ = now_ms;   // first, or fell behind
    for (int n = 0; n < 4 && step_at_ <= now_ms; ++n) {
        run_frame();
        step_at_ += period;
    }
}

void Emulator::loop() {
#ifndef __EMSCRIPTEN__
    // aplay may go away: a write to its pipe must fail, not kill ZeroShell.
    sigset_t s;
    sigemptyset(&s);
    sigaddset(&s, SIGPIPE);
    pthread_sigmask(SIG_BLOCK, &s, nullptr);
#endif
    auto period = std::chrono::duration_cast<Clock::duration>(std::chrono::duration<double>(1.0 / fps_));
    auto next = Clock::now();
    auto sram_at = Clock::now();
    while (!stop_.load()) {
        if (paused_.load()) {
            run_frame();   // still reads the controls (a gamepad's Home button)
            std::this_thread::sleep_for(std::chrono::milliseconds(30));
            next = Clock::now();
            continue;
        }
        run_frame();
        if (Clock::now() - sram_at > std::chrono::seconds(10)) {   // battery saves, as they happen
            sram_at = Clock::now();
            write_sram(false);
        }
        if (quit_req_.load()) break;
        next += period;
        auto now = Clock::now();
        if (now - next > std::chrono::milliseconds(250)) next = now;   // fell behind: don't race to catch up
        else std::this_thread::sleep_until(next);
    }
}

void Emulator::stop() {
#ifndef __EMSCRIPTEN__
    stop_ = true;
    if (thread_.joinable()) thread_.join();
    started_ = false;
    if (game_loaded_) {
        write_sram(true);
        api_.unload_game();
        api_.deinit();
        game_loaded_ = false;
    }
    if (lib_) {
        dlclose(lib_);
        lib_ = nullptr;
    }
    close_audio();
    pad_.close();
    if (g_emu == this) g_emu = nullptr;
    rom_.clear();
    sram_copy_.clear();
#endif
}

void Emulator::reset() {
    if (!game_loaded_) return;
    std::lock_guard<std::mutex> lock(core_mu_);
    api_.reset();
}

bool Emulator::save_state() {
    if (!game_loaded_) return false;
    std::vector<uint8_t> buf;
    {
        std::lock_guard<std::mutex> lock(core_mu_);
        size_t n = api_.serialize_size();
        if (n == 0) return false;
        buf.resize(n);
        if (!api_.serialize(buf.data(), n)) return false;
    }
    return write_all(base_path(states_dir(), ".state"), buf.data(), buf.size());
}

bool Emulator::load_state() {
    if (!game_loaded_) return false;
    auto buf = read_all(base_path(states_dir(), ".state"), size_t(64) << 20);
    if (buf.empty()) return false;
    std::lock_guard<std::mutex> lock(core_mu_);
    return api_.unserialize(buf.data(), buf.size());
}

bool Emulator::has_state() const { return kit::exists(base_path(states_dir(), ".state")); }

void Emulator::write_sram(bool force) {
    if (!game_loaded_) return;
    size_t n = api_.get_memory_size(kMemorySaveRam);
    auto* mem = static_cast<uint8_t*>(api_.get_memory_data(kMemorySaveRam));
    if (!n || !mem) return;
    if (!force && sram_copy_.size() == n && std::memcmp(sram_copy_.data(), mem, n) == 0) return;
    if (sram_copy_.size() == n && std::memcmp(sram_copy_.data(), mem, n) == 0 &&
        kit::exists(base_path(save_dir_, ".srm")))
        return;   // nothing new, and it's on disk already
    if (write_all(base_path(save_dir_, ".srm"), mem, n)) sram_copy_.assign(mem, mem + n);
}

bool Emulator::take_frame(std::vector<uint16_t>& out, int& w, int& h) {
    std::lock_guard<std::mutex> lock(frame_mu_);
    if (!new_frame_) return false;
    new_frame_ = false;
    out.assign(ready_.begin(), ready_.end());
    w = ready_w_;
    h = ready_h_;
    return true;
}

std::string Emulator::take_message() {
    std::lock_guard<std::mutex> lock(msg_mu_);
    std::string m;
    m.swap(message_);
    return m;
}

// ---- libretro callbacks ---------------------------------------------------

bool Emulator::env(unsigned cmd, void* data) {
    switch (cmd) {
        case kEnvGetCanDupe:
            if (data) *static_cast<bool*>(data) = true;
            return true;
        case kEnvGetOverscan:   // crop the border most TVs hid
            if (data) *static_cast<bool*>(data) = false;
            return true;
        case kEnvSetPixelFormat: {
            int f = data ? *static_cast<const int*>(data) : -1;
            if (f != k0RGB1555 && f != kXRGB8888 && f != kRGB565) return false;
            fmt_ = PixelFormat(f);
            return true;
        }
        case kEnvGetSystemDirectory:
        case kEnvGetCoreAssetsDirectory:
            if (data) *static_cast<const char**>(data) = sys_dir_.c_str();
            return true;
        case kEnvGetSaveDirectory:
            if (data) *static_cast<const char**>(data) = save_dir_.c_str();
            return true;
        case kEnvGetLibretroPath:
            if (data) *static_cast<const char**>(data) = core_path_.c_str();
            return true;
        case kEnvGetVariable:   // core options: the core's defaults
            if (data) static_cast<Variable*>(data)->value = nullptr;
            return false;
        case kEnvGetVariableUpdate:
            if (data) *static_cast<bool*>(data) = false;
            return true;
        case kEnvSetVariables:
        case kEnvSetInputDescriptors:
        case kEnvSetPerformanceLevel:
        case kEnvSetSupportNoGame:
            return true;
        case kEnvGetCoreOptionsVersion:   // the plain (version 0) options
            if (data) *static_cast<unsigned*>(data) = 0;
            return true;
        case kEnvGetLogInterface:
            if (data) static_cast<LogCallback*>(data)->log = cb_log;
            return true;
        case kEnvSetMessage:
            if (data && static_cast<const Message*>(data)->msg) {
                std::lock_guard<std::mutex> lock(msg_mu_);
                message_ = static_cast<const Message*>(data)->msg;
            }
            return true;
        case kEnvShutdown:
            quit_req_ = true;
            return true;
        case kEnvSetGeometry:
            if (data) {
                av_.geometry = *static_cast<const GameGeometry*>(data);
                fit(av_.geometry.aspect_ratio);
            }
            return true;
        case kEnvSetSystemAvInfo:
            if (data) {
                av_ = *static_cast<const SystemAvInfo*>(data);
                fps_ = av_.timing.fps > 1 ? av_.timing.fps : fps_;
                fit(av_.geometry.aspect_ratio);
            }
            return true;
        case kEnvGetLanguage:
            if (data) *static_cast<unsigned*>(data) = 0;   // English
            return true;
        case kEnvGetInputBitmasks:
            return true;
        case kEnvGetInputMaxUsers:
            if (data) *static_cast<unsigned*>(data) = 1;
            return true;
        default:
            return false;   // includes hardware (OpenGL) rendering: this screen has no GPU
    }
}

void Emulator::video(const void* data, unsigned w, unsigned h, size_t pitch) {
    if (!data || !w || !h || dst_w_ <= 0) return;   // a repeated frame
    if (w != map_src_w_ || h != map_src_h_ || int(xmap_.size()) != dst_w_ || int(ymap_.size()) != dst_h_) {
        xmap_.resize(size_t(dst_w_));
        ymap_.resize(size_t(dst_h_));
        // Nearest pixel, sampled at each output pixel's centre.
        for (int x = 0; x < dst_w_; ++x)
            xmap_[size_t(x)] = std::min(int(w) - 1, int((uint64_t(2 * x + 1) * w) / (2u * unsigned(dst_w_))));
        for (int y = 0; y < dst_h_; ++y)
            ymap_[size_t(y)] = std::min(int(h) - 1, int((uint64_t(2 * y + 1) * h) / (2u * unsigned(dst_h_))));
        map_src_w_ = w;
        map_src_h_ = h;
    }
    back_.resize(size_t(dst_w_) * size_t(dst_h_));
    const auto* base = static_cast<const uint8_t*>(data);
    uint16_t* out = back_.data();
    for (int y = 0; y < dst_h_; ++y) {
        const uint8_t* row = base + size_t(ymap_[size_t(y)]) * pitch;
        if (fmt_ == kRGB565) {
            auto* p = reinterpret_cast<const uint16_t*>(row);
            for (int x = 0; x < dst_w_; ++x) *out++ = p[xmap_[size_t(x)]];
        } else if (fmt_ == kXRGB8888) {
            auto* p = reinterpret_cast<const uint32_t*>(row);
            for (int x = 0; x < dst_w_; ++x) {
                uint32_t c = p[xmap_[size_t(x)]];
                *out++ = uint16_t(((c >> 8) & 0xf800) | ((c >> 5) & 0x07e0) | ((c >> 3) & 0x001f));
            }
        } else {   // 0RGB1555
            auto* p = reinterpret_cast<const uint16_t*>(row);
            for (int x = 0; x < dst_w_; ++x) {
                uint16_t c = p[xmap_[size_t(x)]];
                uint16_t r = (c >> 10) & 31, g = (c >> 5) & 31, b = c & 31;
                *out++ = uint16_t((r << 11) | (g << 6) | ((g >> 4) << 5) | b);
            }
        }
    }
    std::lock_guard<std::mutex> lock(frame_mu_);
    ready_.swap(back_);
    ready_w_ = dst_w_;
    ready_h_ = dst_h_;
    new_frame_ = true;
}

void Emulator::audio(const int16_t* data, size_t frames) {
    if (audio_fd_ < 0) return;
    abuf_.insert(abuf_.end(), data, data + frames * 2);
}

int16_t Emulator::input(unsigned port, unsigned device, unsigned index, unsigned id) {
    (void)index;
    if (port != 0 || (device & 0xff) != kDeviceJoypad) return 0;
    uint16_t b = buttons_.load();
    if (id == kJoypadMask) return int16_t(b);
    return id < 16 ? int16_t((b >> id) & 1) : 0;
}

// ---- Sound: raw samples into aplay, which plays them through PipeWire -----

void Emulator::open_audio(double rate) {
#ifndef __EMSCRIPTEN__
    close_audio();
    if (rate < 8000 || !kit::have("aplay")) return;
    std::string r = std::to_string(int(std::lround(rate)));
    const char* argv[] = {"aplay", "-q", "-t", "raw", "-f", "S16_LE", "-c", "2", "-r", r.c_str(),
                          "--buffer-time=80000", nullptr};
    int p[2];
    if (pipe(p) != 0) return;
    pid_t pid = fork();
    if (pid == 0) {
        setpgid(0, 0);
        dup2(p[0], 0);
        int devnull = open("/dev/null", O_WRONLY);
        if (devnull >= 0) { dup2(devnull, 1); dup2(devnull, 2); }
        close(p[0]);
        close(p[1]);
        execvp(argv[0], const_cast<char* const*>(argv));
        _exit(127);
    }
    close(p[0]);
    if (pid < 0) {
        close(p[1]);
        return;
    }
    fcntl(p[1], F_SETFL, O_NONBLOCK);
    fcntl(p[1], F_SETFD, FD_CLOEXEC);
#ifdef F_SETPIPE_SZ
    fcntl(p[1], F_SETPIPE_SZ, 16384);   // ~90 ms queued at most: low delay
#endif
    audio_fd_ = p[1];
    audio_pid_ = pid;
#else
    (void)rate;
#endif
}

void Emulator::flush_audio() {
#ifndef __EMSCRIPTEN__
    if (audio_fd_ < 0 || abuf_.empty()) {
        abuf_.clear();
        return;
    }
    // Whole 4 KiB pieces (a pipe takes those all or nothing): a sample is
    // never split, so left and right never swap.
    const auto* p = reinterpret_cast<const uint8_t*>(abuf_.data());
    size_t left = abuf_.size() * sizeof(int16_t);
    while (left > 0) {
        size_t n = std::min<size_t>(left, 4096);
        ssize_t w = write(audio_fd_, p, n);
        if (w <= 0) break;   // full (drop the rest: we're ahead) or aplay gone
        p += w;
        left -= size_t(w);
    }
    abuf_.clear();
#endif
}

void Emulator::close_audio() {
#ifndef __EMSCRIPTEN__
    if (audio_fd_ >= 0) close(audio_fd_);
    audio_fd_ = -1;
    if (audio_pid_ > 0) {
        kill(audio_pid_, SIGTERM);
        waitpid(audio_pid_, nullptr, 0);
    }
    audio_pid_ = -1;
    abuf_.clear();
#endif
}

}  // namespace zs::games
