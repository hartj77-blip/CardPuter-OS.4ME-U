// Video through libmpv, drawn by ZeroShell itself.
//
// mpv decodes, keeps sound and picture in step and plays the sound; its
// software renderer scales each picture into a buffer, which we turn into
// RGB565 for an LVGL image. ZeroShell keeps the screen and the keyboard, so
// the status bar, toasts and screen dimming all carry on as usual.
//
// libmpv is opened at run time (dlopen), not linked: it comes with mpv,
// which ZeroOS installs anyway, and the build needs no mpv headers. Without
// it, open() fails with a message that says so.
#pragma once

#include <cstdint>
#include <memory>
#include <string>

namespace zs {

struct VideoState {
    bool open = false;        // a video is loaded: playing, paused or loading
    bool paused = false;
    bool loading = false;     // waiting on the network (start, seek, slow Wi-Fi)
    bool finished = false;    // played to the end
    std::string error;        // why it stopped, when it failed
    int pos_ms = 0;
    int dur_ms = -1;
};

class MpvVideo {
public:
    MpvVideo();
    ~MpvVideo();

    // Starts playing url at start_s seconds, pictures scaled to fit w x h.
    // auth, if set, goes in the Authorization header of every request.
    bool open(const std::string& url, const std::string& auth, int start_s, int w, int h, std::string& error);
    // Copies the newest picture into rgb565 (w*h pixels, black bars round
    // it) if there is a new one. True if it did.
    bool frame(uint16_t* rgb565);
    VideoState state();
    void set_paused(bool paused);
    // Seek by seconds, or to seconds when absolute.
    void seek(int seconds, bool absolute);
    void close();

private:
    struct Impl;
    std::unique_ptr<Impl> p_;
};

}  // namespace zs
