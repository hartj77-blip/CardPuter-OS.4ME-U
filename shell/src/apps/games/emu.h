// Runs one game in a libretro core (docs/GAMES.md): loads the core, feeds it
// the game, runs it on its own thread at the console's frame rate, and hands
// the UI each picture scaled to fit the screen. Sound goes to aplay
// (PipeWire), controls come from Pad. Battery saves (.srm) are kept in
// ~/Games/saves; one save state per game in ~/Games/states.
#pragma once

#include <atomic>
#include <cstdint>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#include "apps/games/libretro_api.h"
#include "apps/games/pad.h"

namespace zs::games {

class Emulator {
public:
    ~Emulator();

    // Load core + game. On failure, error says why. max_w x max_h is the
    // largest picture the screen can show.
    bool load(const std::string& core_path, const std::string& game, int max_w, int max_h, std::string& error);
    // Run on the emulator thread at the console's frame rate; or, threaded =
    // false (the simulator, whose clock may be virtual), only when step() is called.
    void start(bool threaded = true);
    // Not threaded: run the frames that are due by now_ms (LVGL's clock).
    void step(uint32_t now_ms);
    void stop();                // save, unload, close the core (safe to call twice)
    void set_paused(bool p) { paused_ = p; }
    // Ignore the controls until every button is let go (after the menu).
    void hold_until_release() { hold_ = true; }
    bool paused() const { return paused_.load(); }
    bool running() const { return started_; }
    bool quit_requested() const { return quit_req_.load(); }   // the core asked to shut down

    void reset();
    bool save_state();
    bool load_state();
    bool has_state() const;

    // The newest picture, if there is one the UI hasn't taken yet. It is
    // already scaled: w x h RGB565 pixels.
    bool take_frame(std::vector<uint16_t>& out, int& w, int& h);
    // A message from the core (e.g. "Saved"), once.
    std::string take_message();
    double fps() const { return fps_; }
    const std::string& core_name() const { return core_name_; }

    Pad& pad() { return pad_; }

    // libretro callbacks (static, one emulator at a time).
    bool env(unsigned cmd, void* data);
    void video(const void* data, unsigned w, unsigned h, size_t pitch);
    void audio(const int16_t* data, size_t frames);
    int16_t input(unsigned port, unsigned device, unsigned index, unsigned id);

private:
    void loop();
    void run_frame();
    bool threaded_ = true;
    bool started_ = false;
    double step_at_ = 0;            // not threaded: when the next frame is due (ms)
    void fit(double aspect);
    void open_audio(double rate);
    void close_audio();
    void flush_audio();
    void write_sram(bool force);
    std::string base_path(const std::string& dir, const char* ext) const;

    void* lib_ = nullptr;
    retro::CoreApi api_{};
    bool game_loaded_ = false;
    std::string core_path_, game_, core_name_, sys_dir_, save_dir_;
    std::vector<uint8_t> rom_;          // the game, for cores that take it in memory
    retro::PixelFormat fmt_ = retro::k0RGB1555;
    retro::SystemAvInfo av_{};
    int max_w_ = 320, max_h_ = 170;

    // The scaled picture: built on the emulator thread into back_, handed over as ready_.
    int dst_w_ = 0, dst_h_ = 0;
    std::vector<int> xmap_, ymap_;
    unsigned map_src_w_ = 0, map_src_h_ = 0;
    std::vector<uint16_t> back_, ready_;
    int ready_w_ = 0, ready_h_ = 0;
    bool new_frame_ = false;
    std::mutex frame_mu_;

    std::mutex core_mu_;                // held while the core runs a frame
    std::thread thread_;
    std::atomic<bool> stop_{false}, paused_{false}, quit_req_{false}, hold_{false};
    std::atomic<uint16_t> buttons_{0};
    double fps_ = 60;

    std::vector<int16_t> abuf_;
    int audio_fd_ = -1, audio_pid_ = -1;

    std::vector<uint8_t> sram_copy_;    // what's in the .srm file now

    std::mutex msg_mu_;
    std::string message_;

    Pad pad_;
};

}  // namespace zs::games
