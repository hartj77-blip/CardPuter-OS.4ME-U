// Settings > Running programs: the names it shows, what it leaves out, and
// stopping a real program (one that ignores SIGTERM, too).
#include <signal.h>
#include <sys/wait.h>
#include <unistd.h>

#include <cstdio>
#include <string>
#include <vector>

#include "hal/proc_stats.h"

using namespace zs;

static int failures = 0;
#define CHECK(cond)                                                      \
    do {                                                                 \
        if (!(cond)) {                                                   \
            std::fprintf(stderr, "%s:%d: FAILED: %s\n", __FILE__, __LINE__, #cond); \
            ++failures;                                                  \
        }                                                                \
    } while (0)

static bool listed(int pid, std::string* name = nullptr) {
    ProcCpuPrev prev;
    for (auto& p : user_processes(getuid(), getpid(), prev))
        if (p.pid == pid) {
            if (name) *name = p.name;
            return true;
        }
    return false;
}

static pid_t spawn(const char* script) {
    pid_t pid = fork();
    if (pid == 0) {
        execl("/bin/sh", "sh", "-c", script, (char*)nullptr);
        _exit(127);
    }
    usleep(150000);   // let exec happen
    return pid;
}

int main() {
    // Names
    CHECK(process_display_name("python3", {"python3", "/home/pi/weather.py"}) == "weather.py");
    CHECK(process_display_name("python3", {"/usr/bin/python3.11", "-u", "bot.py", "--x"}) == "bot.py");
    CHECK(process_display_name("python3", {"python3", "-m", "http.server"}) == "python3");
    CHECK(process_display_name("node", {"node", "server.js"}) == "server.js");
    CHECK(process_display_name("sh", {"sh", "-c", "sleep 5"}) == "sh");
    CHECK(process_display_name("htop", {"/usr/bin/htop"}) == "htop");
    CHECK(process_display_name("kworker", {}) == "kworker");

    // Never listed
    CHECK(process_is_essential("systemd"));
    CHECK(process_is_essential("pipewire"));
    CHECK(process_is_essential("wireplumber"));
    CHECK(process_is_essential("zeroshell"));
    CHECK(!process_is_essential("python3"));

    // Ourselves: never listed, never stopped
    CHECK(!listed(getpid()));
    CHECK(!stop_user_process(getuid(), getpid(), getpid()));
    CHECK(!stop_user_process(getuid(), getpid(), 1));

    // A program that quits on SIGTERM
    pid_t a = fork();
    if (a == 0) {
        execlp("sleep", "sleep", "30", (char*)nullptr);
        _exit(127);
    }
    usleep(150000);
    std::string name;
    CHECK(listed(a, &name));
    CHECK(name == "sleep");
    CHECK(stop_user_process(getuid(), getpid(), a));
    int st = 0;
    CHECK(waitpid(a, &st, 0) == a);   // left for us, its parent, to reap
    CHECK(WIFSIGNALED(st) && WTERMSIG(st) == SIGTERM);
    CHECK(!listed(a));
    CHECK(!stop_user_process(getuid(), getpid(), a));   // gone: nothing to stop

    // One that ignores SIGTERM: killed after the grace time
    pid_t b = spawn("trap '' TERM; while :; do sleep 1; done");
    CHECK(listed(b));
    CHECK(stop_user_process(getuid(), getpid(), b));
    CHECK(waitpid(b, &st, 0) == b);
    CHECK(WIFSIGNALED(st) && WTERMSIG(st) == SIGKILL);

    // Someone else's (another uid): not listed, not stopped
    if (getuid() != 0) CHECK(!stop_user_process(getuid(), getpid(), 1));
    else CHECK(!listed(1));

    if (failures) {
        std::fprintf(stderr, "proc_test: %d failed\n", failures);
        return 1;
    }
    std::printf("proc_test: ok\n");
    return 0;
}
