#include "platform/shell_cmd.h"

#include <cstdio>

#include "platform/watchdog.h"

namespace zs {

std::string shell_quote(const std::string& s) {
    std::string r = "'";
    for (char c : s) {
        if (c == '\'') r += "'\\''";
        else r += c;
    }
    return r + "'";
}

std::string run_command(const std::string& cmd) {
    WatchdogGrace grace;
    std::string out;
    FILE* p = popen((cmd + " 2>/dev/null").c_str(), "r");
    if (!p) return out;
    char buf[512];
    while (fgets(buf, sizeof buf, p)) out += buf;
    pclose(p);
    return out;
}

}  // namespace zs
