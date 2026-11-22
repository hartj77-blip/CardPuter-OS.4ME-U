// The part of the libretro API (https://docs.libretro.com) the Games app
// uses to drive an emulator core. The values and layouts are libretro's own
// (libretro.h, MIT licence); only what a software-rendered, single-player
// frontend needs is declared here.
#pragma once

#include <cstddef>
#include <cstdint>

namespace zs::retro {

constexpr unsigned kApiVersion = 1;

// Input
constexpr unsigned kDeviceJoypad = 1;
enum JoypadId : unsigned {
    kB = 0, kY = 1, kSelect = 2, kStart = 3, kUp = 4, kDown = 5, kLeft = 6, kRight = 7,
    kA = 8, kX = 9, kL = 10, kR = 11, kL2 = 12, kR2 = 13, kL3 = 14, kR3 = 15,
    kJoypadMask = 256,   // input_state() returns all buttons as a bitmask
};

// Memory regions
constexpr unsigned kMemorySaveRam = 0;
constexpr unsigned kMemoryRtc = 1;

// Pixel formats
enum PixelFormat : int { k0RGB1555 = 0, kXRGB8888 = 1, kRGB565 = 2 };

// Environment commands
constexpr unsigned kEnvExperimental = 0x10000;
enum Env : unsigned {
    kEnvSetRotation = 1,
    kEnvGetOverscan = 2,
    kEnvGetCanDupe = 3,
    kEnvSetMessage = 6,
    kEnvShutdown = 7,
    kEnvSetPerformanceLevel = 8,
    kEnvGetSystemDirectory = 9,
    kEnvSetPixelFormat = 10,
    kEnvSetInputDescriptors = 11,
    kEnvGetVariable = 15,
    kEnvSetVariables = 16,
    kEnvGetVariableUpdate = 17,
    kEnvSetSupportNoGame = 18,
    kEnvGetLibretroPath = 19,
    kEnvGetLogInterface = 27,
    kEnvGetCoreAssetsDirectory = 30,
    kEnvGetSaveDirectory = 31,
    kEnvSetSystemAvInfo = 32,
    kEnvSetGeometry = 37,
    kEnvGetLanguage = 39,
    kEnvGetInputBitmasks = 51 | kEnvExperimental,
    kEnvGetCoreOptionsVersion = 52,
    kEnvGetInputMaxUsers = 61,
};

enum LogLevel : int { kLogDebug = 0, kLogInfo, kLogWarn, kLogError };
using LogPrintf = void (*)(LogLevel level, const char* fmt, ...);
struct LogCallback {
    LogPrintf log;
};

struct Message {
    const char* msg;
    unsigned frames;
};

struct Variable {
    const char* key;
    const char* value;
};

struct SystemInfo {
    const char* library_name;
    const char* library_version;
    const char* valid_extensions;   // "nes|fds|unf"
    bool need_fullpath;             // the core reads the file itself
    bool block_extract;
};

struct GameGeometry {
    unsigned base_width, base_height;
    unsigned max_width, max_height;
    float aspect_ratio;             // <= 0: base_width / base_height
};

struct SystemTiming {
    double fps;
    double sample_rate;
};

struct SystemAvInfo {
    GameGeometry geometry;
    SystemTiming timing;
};

struct GameInfo {
    const char* path;
    const void* data;
    size_t size;
    const char* meta;
};

// Callbacks a frontend gives the core.
using EnvironmentFn = bool (*)(unsigned cmd, void* data);
using VideoRefreshFn = void (*)(const void* data, unsigned width, unsigned height, size_t pitch);
using AudioSampleFn = void (*)(int16_t left, int16_t right);
using AudioSampleBatchFn = size_t (*)(const int16_t* data, size_t frames);
using InputPollFn = void (*)();
using InputStateFn = int16_t (*)(unsigned port, unsigned device, unsigned index, unsigned id);

// The functions every core exports (looked up with dlsym).
struct CoreApi {
    void (*set_environment)(EnvironmentFn);
    void (*set_video_refresh)(VideoRefreshFn);
    void (*set_audio_sample)(AudioSampleFn);
    void (*set_audio_sample_batch)(AudioSampleBatchFn);
    void (*set_input_poll)(InputPollFn);
    void (*set_input_state)(InputStateFn);
    void (*init)();
    void (*deinit)();
    unsigned (*api_version)();
    void (*get_system_info)(SystemInfo*);
    void (*get_system_av_info)(SystemAvInfo*);
    void (*set_controller_port_device)(unsigned port, unsigned device);
    void (*reset)();
    void (*run)();
    size_t (*serialize_size)();
    bool (*serialize)(void* data, size_t size);
    bool (*unserialize)(const void* data, size_t size);
    bool (*load_game)(const GameInfo*);
    void (*unload_game)();
    void* (*get_memory_data)(unsigned id);
    size_t (*get_memory_size)(unsigned id);
};

}  // namespace zs::retro
