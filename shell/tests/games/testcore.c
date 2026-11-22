/* A tiny libretro core for testing the Games app without a real emulator or
 * game: colour bars and a square you move with the d-pad (A makes it orange
 * and beeps). It keeps its position in save states and counts A presses in
 * its battery save, so both can be checked. Plays files named *.zstest.
 * Built for the PC builds only (shell/CMakeLists.txt). */
#include <math.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

#define W 160
#define H 120
#define RATE 32000

typedef bool (*env_t)(unsigned, void*);
typedef void (*video_t)(const void*, unsigned, unsigned, size_t);
typedef void (*sample_t)(int16_t, int16_t);
typedef size_t (*batch_t)(const int16_t*, size_t);
typedef void (*poll_t)(void);
typedef int16_t (*state_t)(unsigned, unsigned, unsigned, unsigned);

struct system_info { const char *name, *version, *exts; bool need_fullpath, block_extract; };
struct geometry { unsigned bw, bh, mw, mh; float aspect; };
struct timing { double fps, rate; };
struct av_info { struct geometry g; struct timing t; };
struct game_info { const char* path; const void* data; size_t size; const char* meta; };

static env_t env;
static video_t video;
static batch_t batch;
static poll_t poll_cb;
static state_t input;
static uint16_t fb[W * H];
static struct { int32_t x, y, frame; } st;
static uint8_t sram[16];
static double phase;

#define API __attribute__((visibility("default")))

API unsigned retro_api_version(void) { return 1; }
API void retro_set_environment(env_t e) { env = e; }
API void retro_set_video_refresh(video_t v) { video = v; }
API void retro_set_audio_sample(sample_t s) { (void)s; }
API void retro_set_audio_sample_batch(batch_t b) { batch = b; }
API void retro_set_input_poll(poll_t p) { poll_cb = p; }
API void retro_set_input_state(state_t s) { input = s; }
API void retro_init(void) {}
API void retro_deinit(void) {}
API void retro_get_system_info(struct system_info* i) {
    i->name = "ZeroOS test pattern";
    i->version = "1";
    i->exts = "zstest";
    i->need_fullpath = false;
    i->block_extract = false;
}
API void retro_get_system_av_info(struct av_info* a) {
    a->g.bw = W; a->g.bh = H; a->g.mw = W; a->g.mh = H; a->g.aspect = 4.0f / 3.0f;
    a->t.fps = 60.0; a->t.rate = RATE;
}
API void retro_set_controller_port_device(unsigned p, unsigned d) { (void)p; (void)d; }
API void retro_reset(void) { st.x = W / 2; st.y = H / 2; st.frame = 0; }
API bool retro_load_game(const struct game_info* g) {
    int fmt = 2; /* RGB565 */
    if (!g || !g->data || !env(10, &fmt)) return false;
    retro_reset();
    return true;
}
API void retro_unload_game(void) {}
API void* retro_get_memory_data(unsigned id) { return id == 0 ? sram : NULL; }
API size_t retro_get_memory_size(unsigned id) { return id == 0 ? sizeof sram : 0; }
API size_t retro_serialize_size(void) { return sizeof st; }
API bool retro_serialize(void* d, size_t n) { if (n < sizeof st) return false; memcpy(d, &st, sizeof st); return true; }
API bool retro_unserialize(const void* d, size_t n) { if (n < sizeof st) return false; memcpy(&st, d, sizeof st); return true; }

API void retro_run(void) {
    poll_cb();
    int16_t b = input(0, 1, 0, 256); /* the whole joypad as a bitmask */
    static int16_t was;
    if (b & (1 << 4)) st.y -= 2;
    if (b & (1 << 5)) st.y += 2;
    if (b & (1 << 6)) st.x -= 2;
    if (b & (1 << 7)) st.x += 2;
    if ((b & (1 << 8)) && !(was & (1 << 8))) sram[0]++;
    was = b;
    if (st.x < 8) st.x = 8;
    if (st.x > W - 8) st.x = W - 8;
    if (st.y < 8) st.y = 8;
    if (st.y > H - 8) st.y = H - 8;
    static const uint16_t bars[] = {0xffff, 0xffe0, 0x07ff, 0x07e0, 0xf81f, 0xf800, 0x001f, 0x0000};
    for (int y = 0; y < H; ++y)
        for (int x = 0; x < W; ++x) fb[y * W + x] = bars[(x * 8 / W + st.frame / 30) % 8] & (y < H - 12 ? 0xffff : 0x7bef);
    uint16_t sq = (b & (1 << 8)) ? 0xfc00 : 0x0000;
    for (int y = st.y - 8; y < st.y + 8; ++y)
        for (int x = st.x - 8; x < st.x + 8; ++x) fb[y * W + x] = (x == st.x - 8 || y == st.y - 8 || x == st.x + 7 || y == st.y + 7) ? 0xffff : sq;
    video(fb, W, H, W * 2);
    int16_t audio[RATE / 60 * 2];
    for (int i = 0; i < RATE / 60; ++i) {
        int16_t v = (b & (1 << 8)) ? (int16_t)(3000 * sin(phase)) : 0;
        phase += 2 * 3.14159265 * 440.0 / RATE;
        audio[2 * i] = audio[2 * i + 1] = v;
    }
    batch(audio, RATE / 60);
    st.frame++;
}
