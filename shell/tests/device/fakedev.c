// LD_PRELOAD shim: lets the device build of ZeroShell run on a PC with no
// framebuffer and no keyboard. A plain file stands in for the LCD
// (ZS_FRAMEBUFFER) and a FIFO for the keyboard (ZS_KEYBOARD); this answers
// the few ioctls those would not. Used by device_test.py.
#define _GNU_SOURCE
#include <dlfcn.h>
#include <errno.h>
#include <linux/fb.h>
#include <linux/input.h>
#include <stdarg.h>
#include <string.h>
#include <sys/ioctl.h>

#define W 320
#define H 170

int ioctl(int fd, unsigned long req, ...) {
    static int (*real)(int, unsigned long, void*);
    if (!real) real = dlsym(RTLD_NEXT, "ioctl");
    va_list ap;
    va_start(ap, req);
    void* arg = va_arg(ap, void*);
    va_end(ap);
    int r = real(fd, req, arg);
    if (r == 0 || (errno != ENOTTY && errno != EINVAL)) return r;
    if (req == FBIOGET_VSCREENINFO) {
        struct fb_var_screeninfo* v = arg;
        memset(v, 0, sizeof *v);
        v->xres = v->xres_virtual = W;
        v->yres = v->yres_virtual = H;
        v->bits_per_pixel = 16;
        v->red.offset = 11; v->red.length = 5;
        v->green.offset = 5; v->green.length = 6;
        v->blue.offset = 0; v->blue.length = 5;
        return 0;
    }
    if (req == FBIOGET_FSCREENINFO) {
        struct fb_fix_screeninfo* f = arg;
        memset(f, 0, sizeof *f);
        f->line_length = W * 2;
        f->smem_len = W * H * 2;
        return 0;
    }
    if (req == FBIOPUT_VSCREENINFO || req == FBIOBLANK) return 0;
    if (_IOC_TYPE(req) == 'E' && _IOC_NR(req) == _IOC_NR(EVIOCGNAME(0))) {
        const char name[] = "tca8418c";   // M5's driver names the device after its I2C client
        size_t n = _IOC_SIZE(req) < sizeof name ? _IOC_SIZE(req) : sizeof name;
        memcpy(arg, name, n);
        return (int)n;
    }
    errno = ENOTTY;
    return -1;
}
