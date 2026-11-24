#include "ui/screenshot.h"

#include <sys/stat.h>
#include <unistd.h>

#include <cerrno>
#include <cstdio>
#include <cstdlib>
#include <ctime>
#include <vector>

#include "platform/platform.h"
#include "ui/png.h"

namespace zs {

std::string screenshot_save() {
    std::vector<uint8_t> rgb;
    int w = 0, h = 0;
    if (!platform_capture(rgb, w, h) || w <= 0 || h <= 0 || rgb.size() != size_t(w) * size_t(h) * 3) {
        std::fprintf(stderr, "zeroshell: screenshot: could not read the screen\n");
        return "";
    }
    const char* home = std::getenv("HOME");
    std::string dir = std::string(home && *home ? home : "/home/pi") + "/Pictures";
    mkdir(dir.c_str(), 0755);
    dir += "/Screenshots";
    if (mkdir(dir.c_str(), 0755) != 0 && errno != EEXIST) {
        std::fprintf(stderr, "zeroshell: screenshot: cannot make %s\n", dir.c_str());
        return "";
    }
    std::time_t now = std::time(nullptr);
    std::tm lt{};
    localtime_r(&now, &lt);
    char stamp[32];
    std::strftime(stamp, sizeof stamp, "%Y-%m-%d_%H-%M-%S", &lt);
    // Two in the same second get -2, -3...
    std::string path = dir + "/screenshot_" + stamp + ".png";
    for (int n = 2; access(path.c_str(), F_OK) == 0 && n < 100; ++n)
        path = dir + "/screenshot_" + stamp + "-" + std::to_string(n) + ".png";
    if (!png_write_rgb(path, rgb.data(), w, h)) {
        std::fprintf(stderr, "zeroshell: screenshot: cannot write %s\n", path.c_str());
        return "";
    }
    std::printf("zeroshell: screenshot %s\n", path.c_str());
    std::fflush(stdout);
    return path;
}

}  // namespace zs
