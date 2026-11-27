// A preload library for apps made for M5Stack's launcher: the LCD's DRM card
// under the number it has on M5's system.
//
// On M5's system the LCD is /dev/dri/card1 and HDMI card0, and some apps open
// "/dev/dri/card1" by name (AdvanceWebUI: LVGL's DRM driver with that path
// and the LCD's connector number compiled in). The kernel numbers the cards
// in the order their drivers start, and on ZeroOS the LCD's starts before
// vc4: the two are the other way round. Such an app got the HDMI card, found
// no connector with its number, and crashed (LVGL goes on to use the
// connector it has just freed). The numbers are the kernel's, so no link in
// /dev can swap them, the way m5-fb0-link gives the LCD the name /dev/fb0.
//
// ZeroShell (hal/linux_hal.cpp, external_open) starts an M5Stack app whose
// program names /dev/dri/card1 with this in LD_PRELOAD and
// ZEROOS_DRM_SWAP=<path>:<path>, when the LCD isn't card1. An open() of
// either path opens the other; every other path, and everything else, is
// untouched. Apps that don't name that path never get it. It is built into the zeroshell binary
// (cmake/embed.cmake) and written to ~/.cache/zeroos when first needed.
#undef _FORTIFY_SOURCE   // its headers define open() inline; this file defines the real ones
#define _GNU_SOURCE
#include <dlfcn.h>
#include <errno.h>
#include <fcntl.h>
#include <stdarg.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>

#define PATH_LEN 96

static char one_[PATH_LEN], other_[PATH_LEN];
static int ready_;

// ZEROOS_DRM_SWAP=/dev/dri/card1:/dev/dri/card0. Anything else: no swap.
__attribute__((constructor)) static void setup(void) {
    if (ready_) return;
    const char* e = getenv("ZEROOS_DRM_SWAP");
    const char* colon = e ? strchr(e, ':') : NULL;
    if (colon && e[0] == '/' && colon[1] == '/') {
        size_t a = (size_t)(colon - e), b = strlen(colon + 1);
        if (a < PATH_LEN && b < PATH_LEN && !strchr(colon + 1, ':')) {
            memcpy(other_, colon + 1, b + 1);
            memcpy(one_, e, a);
            one_[a] = 0;
        }
    }
    ready_ = 1;
}

static const char* swapped(const char* path) {
    if (!ready_) setup();   // an open() from another library's start-up code
    if (!path || !one_[0]) return path;
    if (strcmp(path, one_) == 0) return other_;
    if (strcmp(path, other_) == 0) return one_;
    return path;
}

// The third argument is only there with these flags.
#define HAS_MODE(flags) (((flags) & O_CREAT) || ((flags) & O_TMPFILE) == O_TMPFILE)
#define MODE_ARG(flags, last, mode)      \
    do {                                 \
        if (HAS_MODE(flags)) {           \
            va_list ap;                  \
            va_start(ap, last);          \
            mode = va_arg(ap, mode_t);   \
            va_end(ap);                  \
        }                                \
    } while (0)

typedef int (*open_fn)(const char*, int, ...);
typedef int (*openat_fn)(int, const char*, int, ...);

static void* next(const char* name) {
    void* f = dlsym(RTLD_NEXT, name);
    if (!f) errno = ENOSYS;
    return f;
}

int open(const char* path, int flags, ...) {
    static open_fn real;
    mode_t mode = 0;
    MODE_ARG(flags, flags, mode);
    if (!real && !(real = (open_fn)next("open"))) return -1;
    return real(swapped(path), flags, mode);
}

int open64(const char* path, int flags, ...) {
    static open_fn real;
    mode_t mode = 0;
    MODE_ARG(flags, flags, mode);
    if (!real && !(real = (open_fn)next("open64"))) return -1;
    return real(swapped(path), flags, mode);
}

int openat(int dir, const char* path, int flags, ...) {
    static openat_fn real;
    mode_t mode = 0;
    MODE_ARG(flags, flags, mode);
    if (!real && !(real = (openat_fn)next("openat"))) return -1;
    return real(dir, swapped(path), flags, mode);
}

int openat64(int dir, const char* path, int flags, ...) {
    static openat_fn real;
    mode_t mode = 0;
    MODE_ARG(flags, flags, mode);
    if (!real && !(real = (openat_fn)next("openat64"))) return -1;
    return real(dir, swapped(path), flags, mode);
}

// What open() becomes in a program built with _FORTIFY_SOURCE (most of
// Debian): no mode argument, by definition.
int __open_2(const char* path, int flags) { return open(path, flags); }
int __open64_2(const char* path, int flags) { return open64(path, flags); }
int __openat_2(int dir, const char* path, int flags) { return openat(dir, path, flags); }
int __openat64_2(int dir, const char* path, int flags) { return openat64(dir, path, flags); }
