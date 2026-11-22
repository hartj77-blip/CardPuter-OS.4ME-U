#include "hal/mpv_video.h"

#ifndef __EMSCRIPTEN__
#include <dlfcn.h>
#endif

#include <atomic>
#include <type_traits>
#include <cstdlib>
#include <cstring>
#include <vector>

namespace zs {
namespace {

// ---- The parts of libmpv's C API we use (mpv/client.h, mpv/render.h). ----
// Declared here so the build needs no mpv headers; libmpv keeps this ABI
// stable within a major version (libmpv.so.2, and .so.1 before it).

struct mpv_handle;
struct mpv_render_context;

enum mpv_format { MPV_FORMAT_NONE = 0, MPV_FORMAT_STRING = 1, MPV_FORMAT_FLAG = 3, MPV_FORMAT_DOUBLE = 5,
                  MPV_FORMAT_NODE = 6, MPV_FORMAT_NODE_ARRAY = 7 };
enum mpv_event_id { MPV_EVENT_NONE = 0, MPV_EVENT_SHUTDOWN = 1, MPV_EVENT_START_FILE = 6, MPV_EVENT_END_FILE = 7,
                    MPV_EVENT_FILE_LOADED = 8, MPV_EVENT_PLAYBACK_RESTART = 21 };
enum { MPV_END_FILE_REASON_EOF = 0, MPV_END_FILE_REASON_ERROR = 4 };
enum { MPV_ERROR_AO_INIT_FAILED = -14, MPV_ERROR_UNKNOWN_FORMAT = -17 };
enum { MPV_RENDER_PARAM_INVALID = 0, MPV_RENDER_PARAM_API_TYPE = 1, MPV_RENDER_PARAM_BLOCK_FOR_TARGET_TIME = 12,
       MPV_RENDER_PARAM_SW_SIZE = 17, MPV_RENDER_PARAM_SW_FORMAT = 18, MPV_RENDER_PARAM_SW_STRIDE = 19,
       MPV_RENDER_PARAM_SW_POINTER = 20 };
constexpr uint64_t MPV_RENDER_UPDATE_FRAME = 1;

struct mpv_node_list;
struct mpv_node {
    union {
        char* string;
        int flag;
        int64_t int64;
        double double_;
        mpv_node_list* list;
        void* ba;
    } u;
    mpv_format format;
};
struct mpv_node_list {
    int num;
    mpv_node* values;
    char** keys;
};
struct mpv_event {
    int event_id;
    int error;
    uint64_t reply_userdata;
    void* data;
};
struct mpv_event_end_file {
    int reason;
    int error;
};
struct mpv_render_param {
    int type;
    void* data;
};

struct Lib {
    mpv_handle* (*create)();
    int (*initialize)(mpv_handle*);
    void (*terminate_destroy)(mpv_handle*);
    int (*set_option)(mpv_handle*, const char*, mpv_format, void*);
    int (*set_option_string)(mpv_handle*, const char*, const char*);
    int (*command)(mpv_handle*, const char**);
    int (*set_property)(mpv_handle*, const char*, mpv_format, void*);
    int (*get_property)(mpv_handle*, const char*, mpv_format, void*);
    mpv_event* (*wait_event)(mpv_handle*, double);
    const char* (*error_string)(int);
    int (*render_create)(mpv_render_context**, mpv_handle*, mpv_render_param*);
    void (*render_set_update_callback)(mpv_render_context*, void (*)(void*), void*);
    uint64_t (*render_update)(mpv_render_context*);
    int (*render)(mpv_render_context*, mpv_render_param*);
    void (*render_free)(mpv_render_context*);
    bool ok = false;
};

// Opened once, on first use, and kept: a few hundred kB of address space
// until ZeroShell exits, nothing while no video plays.
const Lib& lib() {
    static Lib l = [] {
        Lib x{};
#ifndef __EMSCRIPTEN__
        void* h = nullptr;
        for (const char* name : {"libmpv.so.2", "libmpv.so.1", "libmpv.so"})
            if ((h = dlopen(name, RTLD_NOW | RTLD_LOCAL))) break;
        if (!h) return x;
        bool all = true;
        auto get = [&](auto& fn, const char* sym) {
            fn = reinterpret_cast<std::remove_reference_t<decltype(fn)>>(dlsym(h, sym));
            if (!fn) all = false;
        };
        get(x.create, "mpv_create");
        get(x.initialize, "mpv_initialize");
        get(x.terminate_destroy, "mpv_terminate_destroy");
        get(x.set_option, "mpv_set_option");
        get(x.set_option_string, "mpv_set_option_string");
        get(x.command, "mpv_command");
        get(x.set_property, "mpv_set_property");
        get(x.get_property, "mpv_get_property");
        get(x.wait_event, "mpv_wait_event");
        get(x.error_string, "mpv_error_string");
        get(x.render_create, "mpv_render_context_create");
        get(x.render_set_update_callback, "mpv_render_context_set_update_callback");
        get(x.render_update, "mpv_render_context_update");
        get(x.render, "mpv_render_context_render");
        get(x.render_free, "mpv_render_context_free");
        x.ok = all;
#endif
        return x;
    }();
    return l;
}

}  // namespace

struct MpvVideo::Impl {
    mpv_handle* mpv = nullptr;
    mpv_render_context* rc = nullptr;
    std::atomic<bool> redraw{false};   // set on mpv's thread, read on ours
    int w = 0, h = 0;
    size_t stride = 0;
    uint32_t* pixels = nullptr;        // bgr0, 64-byte aligned for mpv's scaler
    bool loaded = false;               // the file opened
    bool restarted = false;            // a picture since the last load or seek
    VideoState st;

    static void on_update(void* self) { static_cast<Impl*>(self)->redraw = true; }

    void drain_events() {
        const Lib& L = lib();
        while (mpv) {
            mpv_event* e = L.wait_event(mpv, 0);
            if (!e || e->event_id == MPV_EVENT_NONE) break;
            switch (e->event_id) {
                case MPV_EVENT_FILE_LOADED: loaded = true; break;
                case MPV_EVENT_PLAYBACK_RESTART: restarted = true; break;
                case MPV_EVENT_END_FILE: {
                    auto* ef = static_cast<mpv_event_end_file*>(e->data);
                    if (!ef) break;
                    st.open = false;
                    if (ef->reason == MPV_END_FILE_REASON_EOF) {
                        st.finished = true;
                    } else if (ef->reason == MPV_END_FILE_REASON_ERROR) {
                        // With no sound device mpv plays the picture alone,
                        // then ends with "audio output failed": that's an end.
                        if (ef->error == MPV_ERROR_AO_INIT_FAILED && loaded) st.finished = true;
                        else if (ef->error == MPV_ERROR_UNKNOWN_FORMAT) st.error = "Can't play this kind of video";
                        // "Loading failed" covers a server that said no and
                        // a dropped connection.
                        else st.error = loaded ? "The video stopped: lost the server"
                                               : "Can't load the video from the server";
                    }
                    break;
                }
                case MPV_EVENT_SHUTDOWN: st.open = false; break;
                default: break;
            }
        }
    }
};

MpvVideo::MpvVideo() : p_(new Impl) {}
MpvVideo::~MpvVideo() { close(); }

bool MpvVideo::open(const std::string& url, const std::string& auth, int start_s, int w, int h,
                    std::string& error) {
    close();
    const Lib& L = lib();
    if (!L.ok) {
        error = "Video needs mpv (sudo apt install mpv)";
        return false;
    }
    if (w <= 0 || h <= 0) return false;
    Impl& p = *p_;
    p.mpv = L.create();
    if (!p.mpv) {
        error = "The video player didn't start";
        return false;
    }
    auto opt = [&](const char* k, const std::string& v) { L.set_option_string(p.mpv, k, v.c_str()); };
    opt("config", "no");                // ignore ~/.config/mpv: the Dock's player has its own
    opt("terminal", "no");
    opt("vo", "libmpv");                // pictures come to us
    opt("idle", "yes");                 // stay alive after the end, so we see why it ended
    opt("keep-open", "no");
    opt("osc", "no");
    opt("input-default-bindings", "no");
    opt("input-vo-keyboard", "no");
    opt("ytdl", "no");
    opt("load-scripts", "no");
    opt("audio-client-name", "ZeroOS");
    // The CPU decodes: the server sends a picture the size of the screen,
    // a small job for the Cardputer's four cores.
    opt("hwdec", "no");
    opt("vd-lavc-threads", "2");
    opt("sws-scaler", "fast-bilinear");
    // 512 MB of RAM: a modest buffer, enough to ride out a Wi-Fi hiccup at
    // the bitrates we ask for.
    opt("cache", "yes");
    opt("demuxer-max-bytes", "12MiB");
    opt("demuxer-max-back-bytes", "4MiB");
    opt("cache-pause-wait", "2");
    opt("network-timeout", "20");
    if (start_s > 0) opt("start", std::to_string(start_s));
    if (!auth.empty()) {
        // A list option set as a list: a plain string would be split at the
        // commas in Jellyfin's header (the bug that broke music streams).
        std::string header = "Authorization: " + auth;
        mpv_node item{};
        item.format = MPV_FORMAT_STRING;
        item.u.string = const_cast<char*>(header.c_str());
        mpv_node_list list{1, &item, nullptr};
        mpv_node node{};
        node.format = MPV_FORMAT_NODE_ARRAY;
        node.u.list = &list;
        L.set_option(p.mpv, "http-header-fields", MPV_FORMAT_NODE, &node);
    }
    if (L.initialize(p.mpv) < 0) {
        close();
        error = "The video player didn't start";
        return false;
    }
    char api[] = "sw";
    mpv_render_param params[] = {{MPV_RENDER_PARAM_API_TYPE, api}, {MPV_RENDER_PARAM_INVALID, nullptr}};
    if (L.render_create(&p.rc, p.mpv, params) < 0) {
        close();
        error = "The video player can't draw";
        return false;
    }
    L.render_set_update_callback(p.rc, &Impl::on_update, &p);

    p.w = w;
    p.h = h;
    p.stride = (size_t(w) * 4 + 63) / 64 * 64;
    p.pixels = static_cast<uint32_t*>(std::aligned_alloc(64, p.stride * size_t(h)));
    if (!p.pixels) {
        close();
        error = "Not enough memory for video";
        return false;
    }
    std::memset(p.pixels, 0, p.stride * size_t(h));

    const char* cmd[] = {"loadfile", url.c_str(), nullptr};
    if (L.command(p.mpv, cmd) < 0) {
        close();
        error = "Can't open that address";
        return false;
    }
    p.st = VideoState{};
    p.st.open = true;
    p.loaded = p.restarted = false;
    return true;
}

bool MpvVideo::frame(uint16_t* out) {
    Impl& p = *p_;
    if (!p.rc || !p.redraw.exchange(false)) return false;
    const Lib& L = lib();
    if (!(L.render_update(p.rc) & MPV_RENDER_UPDATE_FRAME)) return false;
    int size[2] = {p.w, p.h};
    char fmt[] = "bgr0";
    int block = 0;   // never wait for the picture's time on the UI thread
    mpv_render_param params[] = {{MPV_RENDER_PARAM_SW_SIZE, size},
                                 {MPV_RENDER_PARAM_SW_FORMAT, fmt},
                                 {MPV_RENDER_PARAM_SW_STRIDE, &p.stride},
                                 {MPV_RENDER_PARAM_SW_POINTER, p.pixels},
                                 {MPV_RENDER_PARAM_BLOCK_FOR_TARGET_TIME, &block},
                                 {MPV_RENDER_PARAM_INVALID, nullptr}};
    if (L.render(p.rc, params) < 0) return false;
    // bgr0 read as a little-endian word is 0x00RRGGBB.
    for (int y = 0; y < p.h; ++y) {
        const uint32_t* in = reinterpret_cast<const uint32_t*>(reinterpret_cast<const uint8_t*>(p.pixels) +
                                                               p.stride * size_t(y));
        uint16_t* o = out + size_t(y) * size_t(p.w);
        for (int x = 0; x < p.w; ++x) {
            uint32_t v = in[x];
            o[x] = uint16_t(((v >> 8) & 0xF800) | ((v >> 5) & 0x07E0) | ((v >> 3) & 0x001F));
        }
    }
    return true;
}

VideoState MpvVideo::state() {
    Impl& p = *p_;
    if (!p.mpv) return p.st;
    p.drain_events();
    if (!p.st.open) return p.st;
    const Lib& L = lib();
    double pos = 0, dur = 0;
    int paused = 0, waiting = 0, seeking = 0;
    if (L.get_property(p.mpv, "time-pos", MPV_FORMAT_DOUBLE, &pos) >= 0) p.st.pos_ms = int(pos * 1000);
    if (L.get_property(p.mpv, "duration", MPV_FORMAT_DOUBLE, &dur) >= 0 && dur > 0) p.st.dur_ms = int(dur * 1000);
    L.get_property(p.mpv, "pause", MPV_FORMAT_FLAG, &paused);
    L.get_property(p.mpv, "paused-for-cache", MPV_FORMAT_FLAG, &waiting);
    L.get_property(p.mpv, "seeking", MPV_FORMAT_FLAG, &seeking);
    p.st.paused = paused != 0;
    p.st.loading = !p.loaded || !p.restarted || waiting || seeking;
    return p.st;
}

void MpvVideo::set_paused(bool paused) {
    if (!p_->mpv) return;
    int v = paused;
    lib().set_property(p_->mpv, "pause", MPV_FORMAT_FLAG, &v);
}

void MpvVideo::seek(int seconds, bool absolute) {
    if (!p_->mpv) return;
    std::string s = std::to_string(seconds);
    const char* cmd[] = {"seek", s.c_str(), absolute ? "absolute" : "relative", nullptr};
    lib().command(p_->mpv, cmd);
    p_->restarted = false;
}

void MpvVideo::close() {
    Impl& p = *p_;
    const Lib& L = lib();
    // The render context must go before the player (render.h says so).
    if (p.rc) {
        L.render_set_update_callback(p.rc, nullptr, nullptr);
        L.render_free(p.rc);
        p.rc = nullptr;
    }
    if (p.mpv) {
        L.terminate_destroy(p.mpv);
        p.mpv = nullptr;
    }
    std::free(p.pixels);
    p.pixels = nullptr;
    p.redraw = false;
    p.st.open = false;
}

}  // namespace zs
