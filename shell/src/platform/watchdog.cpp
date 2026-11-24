// sd_notify() written out, so ZeroShell needs no libsystemd: the protocol is
// one datagram of "KEY=value" lines to the unix socket named in NOTIFY_SOCKET
// (a name starting with '@' is in the abstract namespace).
// See sd_notify(3) and systemd.service(5), WatchdogSec=.
#include "platform/watchdog.h"

#include <sys/socket.h>
#include <sys/un.h>
#include <time.h>
#include <unistd.h>

#include <cstddef>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <thread>

namespace zs {

namespace {
int g_fd = -1;
sockaddr_un g_addr{};
socklen_t g_len = 0;
long long g_limit_us = 0;   // WatchdogSec, as systemd passed it
long long g_last_ms = 0;    // last ping
int g_grace = 0;            // WatchdogGrace objects alive (main thread only)
std::thread::id g_main;

long long now_ms() {
    timespec ts{};
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (long long)ts.tv_sec * 1000 + ts.tv_nsec / 1000000;
}

void send(const std::string& msg) {
    if (g_fd < 0) return;
    // MSG_NOSIGNAL: systemd gone (shutting down) must not kill us with SIGPIPE.
    if (sendto(g_fd, msg.data(), msg.size(), MSG_NOSIGNAL | MSG_DONTWAIT, (sockaddr*)&g_addr, g_len) >= 0)
        g_last_ms = now_ms();
}
}  // namespace

void watchdog_init() {
    const char* sock = std::getenv("NOTIFY_SOCKET");
    const char* usec = std::getenv("WATCHDOG_USEC");
    const char* pid = std::getenv("WATCHDOG_PID");
    bool ours = !pid || std::atoll(pid) == (long long)getpid();
    long long limit = usec ? std::atoll(usec) : 0;
    std::string path = sock ? sock : "";
    // Programs ZeroShell starts (M5 apps, the terminal, curl) must not see
    // these: a child pinging for us would hide a frozen UI.
    unsetenv("NOTIFY_SOCKET");
    unsetenv("WATCHDOG_USEC");
    unsetenv("WATCHDOG_PID");
    if (path.empty() || limit <= 0 || !ours || path.size() >= sizeof g_addr.sun_path) return;

    g_addr.sun_family = AF_UNIX;
    std::memcpy(g_addr.sun_path, path.data(), path.size());
    if (path[0] == '@') g_addr.sun_path[0] = '\0';   // abstract socket
    g_len = socklen_t(offsetof(sockaddr_un, sun_path) + path.size());
    g_fd = socket(AF_UNIX, SOCK_DGRAM | SOCK_CLOEXEC, 0);
    if (g_fd < 0) return;
    g_limit_us = limit;
    g_main = std::this_thread::get_id();
    std::fprintf(stderr, "zeroshell: watchdog on, %llds\n", limit / 1000000);
    send("WATCHDOG=1");
}

void watchdog_kick() {
    if (g_fd < 0) return;
    // A quarter of the limit (2.5 s for 10 s): a late pass or two never
    // trips it, and it costs one tiny send() every few seconds.
    if (now_ms() - g_last_ms >= g_limit_us / 4000) send("WATCHDOG=1");
}

WatchdogGrace::WatchdogGrace(int seconds) {
    // Only the UI thread's waits matter: a worker thread may take as long
    // as it likes while the loop keeps pinging.
    if (g_fd < 0 || std::this_thread::get_id() != g_main) return;
    active_ = true;
    if (g_grace++ > 0) return;   // an outer one already raised it
    send("WATCHDOG=1\nWATCHDOG_USEC=" + std::to_string((long long)seconds * 1000000));
}

WatchdogGrace::~WatchdogGrace() {
    if (!active_ || --g_grace > 0) return;
    send("WATCHDOG_USEC=" + std::to_string(g_limit_us) + "\nWATCHDOG=1");
}

}  // namespace zs
