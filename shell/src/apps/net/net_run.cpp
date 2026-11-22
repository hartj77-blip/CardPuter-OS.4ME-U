#include "apps/net/net_run.h"

#include <fcntl.h>
#include <signal.h>
#include <spawn.h>
#include <sys/wait.h>
#include <unistd.h>

#include <chrono>
#include <cstdlib>
#include <cstring>

#include "hal/hal.h"

extern char** environ;

namespace zs::net {

bool simulated() { return hal().system().simulated; }

bool have(const std::string& prog) {
    if (simulated()) return true;
    // `command -v` exits 0 when the program is on PATH.
    std::string cmd = "command -v " + quote(prog) + " >/dev/null 2>&1";
    return std::system(cmd.c_str()) == 0;
}

std::string quote(const std::string& s) {
    std::string r = "'";
    for (char ch : s) r += ch == '\'' ? std::string("'\\''") : std::string(1, ch);
    return r + "'";
}

Job::~Job() {
    cancel();
    if (thread_.joinable()) thread_.join();
}

void Job::start(std::vector<std::string> argv, bool root, std::vector<std::string> sim, int sim_ms) {
    (void)sim_ms;
    if (started_) return;
    started_ = true;
    running_ = true;
    done_ = Done::Running;
    if (simulated()) {
        sim_mode_ = true;
        sim_lines_ = std::move(sim);
        sim_at_ = 0;
        if (sim_lines_.empty()) { done_ = Done::Ok; running_ = false; }
    } else {
        thread_ = std::thread([this, argv = std::move(argv), root] { run_real(argv, root); });
    }
}

void Job::push(const std::string& line) {
    std::lock_guard<std::mutex> lock(mu_);
    pending_.push_back(line);
    seen_.fetch_add(1);
}

size_t Job::drain(std::vector<std::string>& out) {
    if (sim_mode_) {
        if (stop_.load()) {
            if (running_.load()) { done_ = Done::Cancelled; running_ = false; }
            return 0;
        }
        if (sim_at_ >= sim_lines_.size()) {
            if (running_.load()) { done_ = Done::Ok; running_ = false; }
            return 0;
        }
        out.push_back(sim_lines_[sim_at_++]);   // one line per poll: it streams
        seen_.fetch_add(1);
        if (sim_at_ >= sim_lines_.size()) { done_ = Done::Ok; running_ = false; }
        return 1;
    }
    std::lock_guard<std::mutex> lock(mu_);
    size_t n = pending_.size();
    for (auto& l : pending_) out.push_back(std::move(l));
    pending_.clear();
    return n;
}

void Job::run_real(const std::vector<std::string>& argv, bool root) {
#ifdef __EMSCRIPTEN__
    // The browser demo has no processes (and no posix_spawn); it is always
    // simulated, so this path is never taken, but it still has to link.
    (void)argv;
    (void)root;
    done_ = Done::Failed;
    running_ = false;
#else
    if (argv.empty()) {
        done_ = Done::Failed;
        running_ = false;
        return;
    }
    if (!have(argv[0])) {
        done_ = Done::Missing;
        running_ = false;
        return;
    }

    std::vector<std::string> words;
    if (root) {
        words.push_back("sudo");
        words.push_back("-n");
    }
    for (auto& a : argv) words.push_back(a);

    int fds[2];
    if (pipe(fds) != 0) {
        done_ = Done::Failed;
        running_ = false;
        return;
    }

    std::vector<char*> cargv;
    cargv.reserve(words.size() + 1);
    for (auto& w : words) cargv.push_back(const_cast<char*>(w.c_str()));
    cargv.push_back(nullptr);

    posix_spawn_file_actions_t fa;
    posix_spawn_file_actions_init(&fa);
    posix_spawn_file_actions_adddup2(&fa, fds[1], STDOUT_FILENO);
    posix_spawn_file_actions_adddup2(&fa, fds[1], STDERR_FILENO);
    posix_spawn_file_actions_addclose(&fa, fds[0]);
    posix_spawn_file_actions_addclose(&fa, fds[1]);

    pid_t pid = -1;
    int rc = posix_spawnp(&pid, cargv[0], &fa, nullptr, cargv.data(), environ);
    posix_spawn_file_actions_destroy(&fa);
    close(fds[1]);
    if (rc != 0) {
        close(fds[0]);
        done_ = Done::Failed;
        running_ = false;
        return;
    }
    pid_.store(pid);

    // Read stdout line by line. A cancel() closes the gap by killing the child,
    // which ends the read.
    std::string buf;
    char chunk[1024];
    for (;;) {
        ssize_t n = read(fds[0], chunk, sizeof chunk);
        if (n <= 0) break;
        buf.append(chunk, size_t(n));
        size_t nl;
        while ((nl = buf.find('\n')) != std::string::npos) {
            push(buf.substr(0, nl));
            buf.erase(0, nl + 1);
        }
        if (stop_.load()) break;
    }
    if (!buf.empty() && !stop_.load()) push(buf);
    close(fds[0]);

    if (stop_.load()) {
        kill(pid, SIGTERM);
    }
    int wstatus = 0;
    waitpid(pid, &wstatus, 0);
    pid_.store(-1);

    if (stop_.load()) done_ = Done::Cancelled;
    else if (WIFEXITED(wstatus) && WEXITSTATUS(wstatus) == 0) done_ = Done::Ok;
    else done_ = Done::Failed;
    running_ = false;
#endif  // __EMSCRIPTEN__
}

void Job::cancel() {
    stop_.store(true);
    int p = pid_.load();
    if (p > 0) kill(p, SIGTERM);
}

void Job::reset() {
    cancel();
    if (thread_.joinable()) thread_.join();
    {
        std::lock_guard<std::mutex> lock(mu_);
        pending_.clear();
    }
    running_ = false;
    stop_ = false;
    done_ = Done::Running;
    seen_ = 0;
    pid_ = -1;
    started_ = false;
    sim_mode_ = false;
    sim_lines_.clear();
    sim_at_ = 0;
}

}  // namespace zs::net
