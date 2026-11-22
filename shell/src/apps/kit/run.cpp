#include "apps/kit/run.h"

#include <dirent.h>
#include <fcntl.h>
#include <strings.h>
#include <signal.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <unistd.h>

#include <algorithm>
#include <cerrno>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <sstream>

#include "hal/hal.h"

extern char** environ;

namespace zs::kit {

bool simulated() { return hal().system().simulated; }

namespace {

// Search PATH the way execvp does.
bool on_path(const std::string& prog) {
    if (prog.find('/') != std::string::npos) return access(prog.c_str(), X_OK) == 0;
    const char* p = std::getenv("PATH");
    std::string path = p ? p : "/usr/local/bin:/usr/bin:/bin";
    std::stringstream ss(path);
    for (std::string dir; std::getline(ss, dir, ':');) {
        if (dir.empty()) dir = ".";
        if (access((dir + "/" + prog).c_str(), X_OK) == 0) return true;
    }
    return false;
}

// fork + exec with output on a pipe. Returns the pid (or -1) and the read end.
pid_t spawn(const std::vector<std::string>& argv, const RunOpts& opts, int& out_fd) {
    out_fd = -1;
#ifdef __EMSCRIPTEN__
    (void)argv;
    (void)opts;
    return -1;
#else
    // Everything the child needs is built before fork(): after it, only
    // async-signal-safe calls.
    std::vector<char*> a;
    for (auto& s : argv) a.push_back(const_cast<char*>(s.c_str()));
    a.push_back(nullptr);
    std::vector<std::string> env_store;
    for (char** e = environ; e && *e; ++e) {
        std::string kv = *e;
        std::string name = kv.substr(0, kv.find('='));
        bool replaced = false;
        for (auto& x : opts.env)
            if (x.compare(0, name.size() + 1, name + "=") == 0) replaced = true;
        if (!replaced) env_store.push_back(kv);
    }
    for (auto& x : opts.env) env_store.push_back(x);
    std::vector<char*> envp;
    for (auto& s : env_store) envp.push_back(const_cast<char*>(s.c_str()));
    envp.push_back(nullptr);

    int p[2];
    if (pipe(p) != 0) return -1;
    pid_t pid = fork();
    if (pid == 0) {
        setpgid(0, 0);   // our own group: cancel() stops what it started too
        int devnull = open("/dev/null", O_RDONLY);
        if (devnull >= 0) dup2(devnull, 0);
        dup2(p[1], 1);
        dup2(p[1], 2);
        close(p[0]);
        close(p[1]);
        if (!opts.cwd.empty() && chdir(opts.cwd.c_str()) != 0) _exit(126);
        execvpe(a[0], a.data(), envp.data());
        _exit(127);
    }
    close(p[1]);
    if (pid < 0) {
        close(p[0]);
        return -1;
    }
    fcntl(p[0], F_SETFD, FD_CLOEXEC);
    out_fd = p[0];
    return pid;
#endif
}

}  // namespace

bool have(const std::string& prog) {
    if (simulated()) return true;
    return on_path(prog);
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

void Job::start(std::vector<std::string> argv, RunOpts opts) {
    if (started_) return;
    started_ = true;
    running_ = true;
    done_ = Done::Running;
    if (simulated()) {
        sim_mode_ = true;
        sim_lines_ = std::move(opts.sim);
        sim_at_ = 0;
        if (sim_lines_.empty()) { done_ = Done::Ok; code_ = 0; running_ = false; }
        return;
    }
    thread_ = std::thread([this, argv = std::move(argv), opts = std::move(opts)]() mutable {
        run_real(std::move(argv), std::move(opts));
    });
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
            if (running_.load()) { done_ = Done::Ok; code_ = 0; running_ = false; }
            return 0;
        }
        out.push_back(sim_lines_[sim_at_++]);   // one line per poll: it streams
        seen_.fetch_add(1);
        if (sim_at_ >= sim_lines_.size()) { done_ = Done::Ok; code_ = 0; running_ = false; }
        return 1;
    }
    std::lock_guard<std::mutex> lock(mu_);
    size_t n = pending_.size();
    for (auto& l : pending_) out.push_back(std::move(l));
    pending_.clear();
    return n;
}

void Job::run_real(std::vector<std::string> argv, RunOpts opts) {
    if (argv.empty()) { done_ = Done::Failed; running_ = false; return; }
    if (!on_path(argv[0])) { done_ = Done::Missing; running_ = false; return; }
    int fd = -1;
    pid_t pid = spawn(argv, opts, fd);
    if (pid < 0) { done_ = Done::Failed; running_ = false; return; }
    pid_.store(pid);

    std::string buf;
    char chunk[1024];
    for (;;) {
        ssize_t n = read(fd, chunk, sizeof chunk);
        if (n < 0 && errno == EINTR) continue;
        if (n <= 0) break;
        buf.append(chunk, size_t(n));
        size_t nl;
        while ((nl = buf.find_first_of(opts.split_cr ? "\r\n" : "\n")) != std::string::npos) {
            std::string line = buf.substr(0, nl);
            if (!line.empty() && line.back() == '\r') line.pop_back();
            if (!line.empty() || buf[nl] == '\n') push(line);
            buf.erase(0, nl + 1);
        }
        if (stop_.load()) break;
    }
    if (!buf.empty() && !stop_.load()) push(buf);
    close(fd);

    if (stop_.load()) kill(-pid, SIGTERM);
    int ws = 0;
    while (waitpid(pid, &ws, 0) < 0 && errno == EINTR) {}
    pid_.store(-1);

    if (stop_.load()) done_ = Done::Cancelled;
    else if (WIFEXITED(ws) && WEXITSTATUS(ws) == 0) done_ = Done::Ok;
    else done_ = (WIFEXITED(ws) && WEXITSTATUS(ws) == 127) ? Done::Missing : Done::Failed;
    code_ = WIFEXITED(ws) ? WEXITSTATUS(ws) : -1;
    running_ = false;
}

void Job::cancel() {
    stop_.store(true);
    int p = pid_.load();
    if (p > 0) kill(-p, SIGTERM);
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
    code_ = -1;
    seen_ = 0;
    pid_ = -1;
    started_ = false;
    sim_mode_ = false;
    sim_lines_.clear();
    sim_at_ = 0;
}

std::string capture(const std::vector<std::string>& argv, const std::string& cwd) {
    if (simulated() || argv.empty() || !on_path(argv[0])) return "";
    RunOpts o;
    o.cwd = cwd;
    int fd = -1;
    pid_t pid = spawn(argv, o, fd);
    if (pid < 0) return "";
    std::string out;
    char chunk[1024];
    for (;;) {
        ssize_t n = read(fd, chunk, sizeof chunk);
        if (n < 0 && errno == EINTR) continue;
        if (n <= 0) break;
        out.append(chunk, size_t(n));
    }
    close(fd);
    int ws;
    while (waitpid(pid, &ws, 0) < 0 && errno == EINTR) {}
    return out;
}

namespace {
std::vector<std::string> list(const std::string& dir, bool want_dirs) {
    std::vector<std::string> v;
    if (DIR* d = opendir(dir.c_str())) {
        while (dirent* e = readdir(d)) {
            std::string n = e->d_name;
            if (n.empty() || n[0] == '.') continue;
            if (is_dir(dir + "/" + n) == want_dirs) v.push_back(n);
        }
        closedir(d);
    }
    std::sort(v.begin(), v.end(), [](const std::string& a, const std::string& b) {
        return strcasecmp(a.c_str(), b.c_str()) < 0;
    });
    return v;
}
}  // namespace

std::vector<std::string> list_dirs(const std::string& dir) { return list(dir, true); }
std::vector<std::string> list_files(const std::string& dir) { return list(dir, false); }

bool is_dir(const std::string& path) {
    struct stat st;
    return stat(path.c_str(), &st) == 0 && S_ISDIR(st.st_mode);
}

bool exists(const std::string& path) { return access(path.c_str(), F_OK) == 0; }

bool write_file(const std::string& path, const std::string& text, bool executable) {
    std::ofstream f(path, std::ios::binary | std::ios::trunc);
    if (!f) return false;
    f << text;
    f.close();
    if (executable) chmod(path.c_str(), 0755);
    return bool(f);
}

std::string read_file(const std::string& path, size_t max) {
    std::ifstream f(path, std::ios::binary);
    if (!f) return "";
    std::string s(max, '\0');
    f.read(&s[0], std::streamsize(max));
    s.resize(size_t(f.gcount()));
    return s;
}

bool make_dirs(const std::string& path) {
    if (path.empty()) return false;
    std::string cur;
    std::stringstream ss(path);
    if (path[0] != '/') cur = ".";
    for (std::string part; std::getline(ss, part, '/');) {
        if (part.empty()) continue;
        cur += "/" + part;
        if (mkdir(cur.c_str(), 0755) != 0 && errno != EEXIST) return false;
    }
    return is_dir(path);
}

}  // namespace zs::kit
