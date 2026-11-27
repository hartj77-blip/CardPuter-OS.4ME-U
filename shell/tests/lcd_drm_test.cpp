// Tests for finding the LCD's DRM card (hal/lcd_drm.h; no LVGL) and for the
// preload library that swaps two cards for an M5Stack app
// (platform/m5_drm_shim.c):  cmake ... && ctest, or
//   lcd_drm_test <m5-drm-shim.so>
// The swap itself is checked from a child process started with the library
// preloaded, the way ZeroShell starts an app: plain files stand in for the
// two cards.
#include <fcntl.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <unistd.h>

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <string>

#include "hal/lcd_drm.h"

using namespace zs;

static int fails = 0;
#define CHECK(cond)                                                       \
    do {                                                                  \
        if (!(cond)) {                                                    \
            std::printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond);   \
            ++fails;                                                      \
        }                                                                 \
    } while (0)

static void mkdirs(const std::string& path) {
    for (size_t i = 1; i <= path.size(); ++i)
        if (i == path.size() || path[i] == '/') mkdir(path.substr(0, i).c_str(), 0755);
}

static void put(const std::string& path, const std::string& text) {
    mkdirs(path.substr(0, path.rfind('/')));
    std::ofstream(path) << text;
}

// What open() of `path` reads, through each way a program can call it.
static std::string read_via(const char* how, const char* path) {
    int fd = -1;
    if (!std::strcmp(how, "open")) fd = open(path, O_RDONLY);
    if (!std::strcmp(how, "open64")) fd = open64(path, O_RDONLY);
    if (!std::strcmp(how, "openat")) fd = openat(AT_FDCWD, path, O_RDONLY | O_CLOEXEC);
    if (!std::strcmp(how, "fopen")) {   // not swapped: DRM cards aren't opened this way
        std::string s;
        if (FILE* f = std::fopen(path, "r")) {
            char buf[32] = {0};
            if (std::fgets(buf, sizeof buf, f)) s = buf;
            std::fclose(f);
        }
        return s;
    }
    if (fd < 0) return "<" + std::string(std::strerror(errno)) + ">";
    char buf[32] = {0};
    ssize_t n = read(fd, buf, sizeof buf - 1);
    close(fd);
    return n > 0 ? std::string(buf, size_t(n)) : "";
}

// Run by the test itself, preloaded: print what each open gives.
static int child(char** argv) {
    for (const char* how : {"open", "open64", "openat", "fopen"})
        for (int i = 2; argv[i]; ++i) std::printf("%s %s=%s\n", how, argv[i], read_via(how, argv[i]).c_str());
    // A new file still gets its mode (the optional third argument).
    std::string made = std::string(argv[2]) + ".made";
    umask(0);
    int fd = open(made.c_str(), O_WRONLY | O_CREAT | O_EXCL, 0640);
    struct stat st {};
    if (fd >= 0 && fstat(fd, &st) == 0) std::printf("mode=%o\n", unsigned(st.st_mode & 0777));
    if (fd >= 0) close(fd);
    return 0;
}

static std::string run(const std::string& cmd) {
    std::string out;
    if (FILE* p = popen(cmd.c_str(), "r")) {
        char buf[256];
        while (std::fgets(buf, sizeof buf, p)) out += buf;
        pclose(p);
    }
    return out;
}

int main(int argc, char** argv) {
    if (argc > 2 && !std::strcmp(argv[1], "--child")) return child(argv);

    // --- names in /sys/class/drm ---
    std::string conn = "x";
    CHECK(drm_card_name("card0") == "card0" && drm_card_name("card12") == "card12");
    CHECK(drm_card_name("card0-SPI-1", &conn) == "card0" && conn == "SPI-1");
    CHECK(drm_card_name("card1-HDMI-A-1", &conn) == "card1" && conn == "HDMI-A-1");
    CHECK(drm_card_name("card0-SPI-1").empty());   // a connector, when only cards are wanted
    CHECK(drm_card_name("card").empty() && drm_card_name("cardx").empty() && drm_card_name("renderD128").empty() &&
          drm_card_name("version").empty() && drm_card_name("card0x").empty() && drm_card_name("").empty());

    char tmpl[] = "/tmp/zs-lcd-drm-XXXXXX";
    std::string tmp = mkdtemp(tmpl);

    // --- the LCD's card ---
    // ZeroOS: the LCD's driver started first.
    std::string sys = tmp + "/a";
    put(sys + "/class/graphics/fb0/device/drm/card0/dev", "226:0");
    put(sys + "/class/graphics/fb0/device/drm/card0-SPI-1/status", "connected");
    put(sys + "/class/drm/card0-SPI-1/status", "connected");
    put(sys + "/class/drm/card1-HDMI-A-1/status", "disconnected");
    CHECK(lcd_drm_card(sys, "fb0") == "card0");
    CHECK(m5_drm_swap("card0") == "/dev/dri/card1:/dev/dri/card0");
    // M5's order: nothing to swap.
    sys = tmp + "/b";
    put(sys + "/class/graphics/fb1/device/drm/card1/dev", "226:1");
    CHECK(lcd_drm_card(sys, "fb1") == "card1");
    CHECK(m5_drm_swap("card1").empty());
    // The framebuffer has no such link: by the connector's name.
    sys = tmp + "/c";
    put(sys + "/class/graphics/fb0/name", "x");
    put(sys + "/class/drm/card0-HDMI-A-1/status", "disconnected");
    put(sys + "/class/drm/card2-SPI-1/status", "connected");
    put(sys + "/class/drm/card2/dev", "226:2");
    CHECK(lcd_drm_card(sys, "fb0") == "card2");
    CHECK(m5_drm_swap("card2") == "/dev/dri/card1:/dev/dri/card2");
    // Not known (no LCD, a PC): no swap, the app runs as it would have.
    CHECK(lcd_drm_card(tmp + "/none", "fb0").empty());
    CHECK(m5_drm_swap("").empty());
    setenv("ZS_M5_DRM_SWAP", "/x:/y", 1);
    CHECK(m5_drm_swap("card1") == "/x:/y");
    unsetenv("ZS_M5_DRM_SWAP");

    // --- which apps get the swap: those whose program names /dev/dri/card1 ---
    put(tmp + "/bin/AdvanceWebUI", std::string("\x7f" "ELF\0\0junk", 10) + "/dev/dri/card1" + std::string("\0more", 5));
    put(tmp + "/bin/plain", "#!/bin/sh\necho /dev/fb0 /dev/dri/card0\n");
    put(tmp + "/apps/t/app.py", "CARD = '/dev/dri/card1'\n");
    put(tmp + "/apps/t/other.py", "FB = '/dev/fb0'\n");
    CHECK(m5_opens_card1(tmp + "/bin/AdvanceWebUI", tmp));
    CHECK(m5_opens_card1("env X=1 '" + tmp + "/bin/AdvanceWebUI' --flag", tmp));
    CHECK(!m5_opens_card1(tmp + "/bin/plain", tmp));
    CHECK(m5_opens_card1("/usr/bin/python3 apps/t/app.py", tmp));        // a script, from where apps are started
    CHECK(!m5_opens_card1("/usr/bin/python3 apps/t/other.py", tmp));
    CHECK(!m5_opens_card1("python3 -m thing", tmp) && !m5_opens_card1("", tmp));
    CHECK(!m5_opens_card1(tmp + "/bin", tmp) && !m5_opens_card1(tmp + "/bin/missing", tmp));   // a folder, nothing

    // --- the preload library ---
    if (argc > 1) {
        std::string shim = argv[1], self = argv[0];
        std::string one = tmp + "/card1", other = tmp + "/card0", third = tmp + "/card2";
        put(one, "hdmi");
        put(other, "lcd");
        put(third, "third");
        auto with = [&](const std::string& swap) {
            return run("LD_PRELOAD='" + shim + "' " + (swap.empty() ? "" : "ZEROOS_DRM_SWAP='" + swap + "' ") + "'" +
                       self + "' --child '" + one + "' '" + other + "' '" + third + "' 2>&1");
        };
        std::string out = with(one + ":" + other);
        for (const char* how : {"open", "open64", "openat"}) {
            CHECK(out.find(std::string(how) + " " + one + "=lcd\n") != std::string::npos);     // the LCD, by M5's number
            CHECK(out.find(std::string(how) + " " + other + "=hdmi\n") != std::string::npos);  // and the other way
            CHECK(out.find(std::string(how) + " " + third + "=third\n") != std::string::npos); // nothing else
        }
        CHECK(out.find("fopen " + one + "=hdmi\n") != std::string::npos);
        CHECK(out.find("mode=640\n") != std::string::npos);
        if (fails) std::printf("%s", out.c_str());
        unlink((one + ".made").c_str());
        // Nothing to swap, or nonsense: every path opens itself.
        for (const std::string& swap : {std::string(), std::string("nonsense"), one + ":relative", one + ":" + other + ":" + third,
                                        "/" + std::string(200, 'a') + ":" + other}) {
            out = with(swap);
            CHECK(out.find("open " + one + "=hdmi\n") != std::string::npos);
            CHECK(out.find("openat " + other + "=lcd\n") != std::string::npos);
            CHECK(out.find("mode=640\n") != std::string::npos);
            unlink((one + ".made").c_str());
        }
    }

    run("rm -rf '" + tmp + "'");
    if (fails) return 1;
    std::printf("lcd_drm_test: all passed%s\n", argc > 1 ? "" : " (the preload library was not given: not tested)");
    return 0;
}
