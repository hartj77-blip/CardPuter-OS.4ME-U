#include "hal/proc_stats.h"

#include <dirent.h>
#include <signal.h>
#include <unistd.h>

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <set>
#include <sstream>
#include <string>

namespace zs {
namespace {

std::string slurp_line(const char* path) {
    std::ifstream f(path);
    std::string s;
    if (f) std::getline(f, s);
    return s;
}

long long read_ll(const char* path) {
    std::string s = slurp_line(path);
    return s.empty() ? 0 : std::strtoll(s.c_str(), nullptr, 10);
}

// Reads "Key: value kB" lines from a /proc status-style file.
long long field_kb(const std::string& text, const char* key) {
    auto p = text.find(key);
    if (p == std::string::npos) return 0;
    return std::strtoll(text.c_str() + p + std::strlen(key), nullptr, 10);
}

std::string slurp(const std::string& path) {
    std::ifstream f(path);
    std::stringstream ss;
    ss << f.rdbuf();
    return ss.str();
}

}  // namespace

void read_mem_cpu(SystemInfo& s, long long cpu_prev[2]) {
    std::string mi = slurp("/proc/meminfo");
    if (!mi.empty()) {
        long long total = field_kb(mi, "MemTotal:"), avail = field_kb(mi, "MemAvailable:");
        s.mem_total_mb = int(total / 1024);
        s.mem_avail_mb = int(avail / 1024);
        s.mem_used_mb = int((total - avail) / 1024);
        s.mem_cache_mb = int(field_kb(mi, "\nCached:") / 1024);
    }

    // zram0: mm_stat = orig_data_size compr_data_size mem_used_total ...
    std::string mm = slurp_line("/sys/block/zram0/mm_stat");
    if (!mm.empty()) {
        long long orig = 0, compr = 0, used = 0;
        std::sscanf(mm.c_str(), "%lld %lld %lld", &orig, &compr, &used);
        s.zram_stored_mb = float(orig / 1048576.0);
        s.zram_ram_mb = float(used / 1048576.0);
        s.zram_capacity_mb = int(read_ll("/sys/block/zram0/disksize") / 1048576);
    }

    // /proc/swaps: Filename Type Size Used Priority (kB); skip zram devices.
    std::ifstream sw("/proc/swaps");
    std::string line;
    std::getline(sw, line);  // header
    s.disk_swap_total_mb = s.disk_swap_used_mb = 0;
    while (std::getline(sw, line)) {
        char name[256];
        long long size = 0, used = 0;
        if (std::sscanf(line.c_str(), "%255s %*s %lld %lld", name, &size, &used) == 3 &&
            std::strstr(name, "zram") == nullptr) {
            s.disk_swap_total_mb += int(size / 1024);
            s.disk_swap_used_mb += int(used / 1024);
        }
    }

    // CPU busy % since the previous sample.
    std::string st = slurp_line("/proc/stat");
    if (st.rfind("cpu ", 0) == 0) {
        std::istringstream in(st.substr(4));
        long long v, total = 0, idle = 0;
        for (int i = 0; in >> v; ++i) {
            total += v;
            if (i == 3 || i == 4) idle += v;  // idle + iowait
        }
        long long busy = total - idle;
        if (cpu_prev[1] > 0 && total > cpu_prev[1])
            s.cpu_load_percent = int(100 * (busy - cpu_prev[0]) / (total - cpu_prev[1]));
        cpu_prev[0] = busy;
        cpu_prev[1] = total;
    }
    s.cpu_mhz = int(read_ll("/sys/devices/system/cpu/cpu0/cpufreq/scaling_cur_freq") / 1000);
    s.cpu_max_mhz = int(read_ll("/sys/devices/system/cpu/cpu0/cpufreq/scaling_max_freq") / 1000);
    s.cpu_governor = slurp_line("/sys/devices/system/cpu/cpu0/cpufreq/scaling_governor");

    read_self_mem(s.self_rss_kb, s.self_peak_kb);
}

bool read_self_mem(int& rss_kb, int& peak_kb) {
    std::string st = slurp("/proc/self/status");
    if (st.empty()) return false;
    rss_kb = int(field_kb(st, "VmRSS:"));
    peak_kb = int(field_kb(st, "VmHWM:"));
    return rss_kb > 0;
}

std::vector<ProcMem> top_processes(size_t n) {
    std::vector<ProcMem> v;
    if (DIR* d = opendir("/proc")) {
        while (dirent* e = readdir(d)) {
            if (e->d_name[0] < '0' || e->d_name[0] > '9') continue;
            std::string st = slurp(std::string("/proc/") + e->d_name + "/status");
            long long rss = field_kb(st, "VmRSS:");
            if (rss <= 0) continue;  // kernel threads
            auto p = st.find("Name:");
            std::string name;
            if (p != std::string::npos) {
                p += 5;
                while (p < st.size() && (st[p] == ' ' || st[p] == '\t')) ++p;
                name = st.substr(p, st.find('\n', p) - p);
            }
            // Merge processes with the same name (e.g. several bash shells).
            auto it = std::find_if(v.begin(), v.end(), [&](const ProcMem& m) { return m.name == name; });
            if (it != v.end()) it->rss_kb += int(rss);
            else v.push_back({name, int(rss)});
        }
        closedir(d);
    }
    std::sort(v.begin(), v.end(), [](const ProcMem& a, const ProcMem& b) { return a.rss_kb > b.rss_kb; });
    if (v.size() > n) v.resize(n);
    return v;
}

namespace {

std::string base_name(const std::string& path) {
    auto slash = path.rfind('/');
    return slash == std::string::npos ? path : path.substr(slash + 1);
}

// The CPU line of /proc/stat, all fields added up (in clock ticks).
long long total_ticks() {
    std::string st = slurp_line("/proc/stat");
    if (st.rfind("cpu ", 0) != 0) return 0;
    std::istringstream in(st.substr(4));
    long long v, total = 0;
    while (in >> v) total += v;
    return total;
}

}  // namespace

bool process_is_essential(const std::string& comm) {
    static const std::set<std::string> kEssential = {
        "systemd", "(sd-pam)", "sd-pam", "pipewire", "pipewire-pulse", "wireplumber",
        "dbus-daemon", "dbus-broker", "dbus-broker-lau", "zeroshell",
    };
    return kEssential.count(comm) > 0;
}

std::string process_display_name(const std::string& comm, const std::vector<std::string>& argv) {
    if (argv.empty()) return comm;
    std::string prog = base_name(argv[0]);
    // An interpreter: the script it runs is what the person started.
    static const char* const kInterpreters[] = {"python", "python3", "node", "sh", "bash", "dash",
                                                "perl", "ruby", "lua", "php", "java"};
    std::string p = prog;
    while (!p.empty() && (std::isdigit((unsigned char)p.back()) || p.back() == '.')) p.pop_back();
    for (const char* i : kInterpreters) {
        if (p != i && prog != i) continue;
        for (size_t a = 1; a < argv.size(); ++a) {
            const std::string& x = argv[a];
            if (x.empty() || x[0] == '-') {
                if (x == "-c" || x == "-m") break;   // inline code or a module: keep the interpreter
                continue;
            }
            return base_name(x);
        }
        break;
    }
    if (prog.empty()) return comm;
    return prog;
}

std::vector<ProcInfo> user_processes(unsigned uid, int self, ProcCpuPrev& prev) {
    std::vector<ProcInfo> v;
    long long total = total_ticks();
    ProcCpuPrev next;
    next.total = total;
    DIR* d = opendir("/proc");
    if (!d) return v;
    while (dirent* e = readdir(d)) {
        if (e->d_name[0] < '0' || e->d_name[0] > '9') continue;
        int pid = std::atoi(e->d_name);
        if (pid <= 1 || pid == self) continue;
        std::string dir = std::string("/proc/") + e->d_name;
        std::string status = slurp(dir + "/status");
        auto u = status.find("\nUid:");
        if (u == std::string::npos || std::strtoul(status.c_str() + u + 5, nullptr, 10) != uid) continue;
        long long rss = field_kb(status, "VmRSS:");
        if (rss <= 0) continue;   // kernel thread, or exiting

        // /proc/<pid>/stat: pid (comm) state ppid ... utime(14) stime(15)
        std::string stat = slurp_line((dir + "/stat").c_str());
        auto open_p = stat.find('('), close_p = stat.rfind(')');
        if (open_p == std::string::npos || close_p == std::string::npos || close_p < open_p) continue;
        std::string comm = stat.substr(open_p + 1, close_p - open_p - 1);
        std::istringstream rest(stat.substr(close_p + 1));
        std::string state;
        rest >> state;
        if (state == "Z" || state == "X") continue;
        if (process_is_essential(comm)) continue;
        std::string skip;
        for (int i = 0; i < 10; ++i) rest >> skip;   // ppid .. cmajflt
        long long ut = 0, stt = 0;
        rest >> ut >> stt;

        std::string raw = slurp(dir + "/cmdline");
        std::vector<std::string> argv;
        std::string cur;
        for (char c : raw) {
            if (c == '\0') {
                argv.push_back(cur);
                cur.clear();
            } else {
                cur += c;
            }
        }
        if (!cur.empty()) argv.push_back(cur);

        ProcInfo p;
        p.pid = pid;
        p.name = process_display_name(comm, argv);
        for (size_t i = 0; i < argv.size() && p.command.size() < 200; ++i) {
            if (i) p.command += ' ';
            p.command += argv[i];
        }
        if (p.command.size() > 200) p.command.resize(200);
        if (p.command.empty()) p.command = comm;
        p.rss_kb = int(rss);
        long long ticks = ut + stt;
        next.ticks.push_back({pid, ticks});
        for (auto& [ppid, pt] : prev.ticks)
            if (ppid == pid && total > prev.total && ticks >= pt) {
                p.cpu_percent = int((ticks - pt) * 100 / (total - prev.total));
                break;
            }
        v.push_back(std::move(p));
    }
    closedir(d);
    prev = std::move(next);
    std::sort(v.begin(), v.end(), [](const ProcInfo& a, const ProcInfo& b) {
        return a.rss_kb != b.rss_kb ? a.rss_kb > b.rss_kb : a.pid < b.pid;
    });
    return v;
}

bool stop_user_process(unsigned uid, int self, int pid) {
#ifdef __EMSCRIPTEN__
    (void)uid, (void)self, (void)pid;
    return false;
#else
    if (pid <= 1 || pid == self) return false;
    ProcCpuPrev scratch;
    bool listed = false;
    for (auto& p : user_processes(uid, self, scratch))
        if (p.pid == pid) listed = true;
    if (!listed) return false;   // gone already, or not one we may stop
    // Gone, or a zombie. Our own children (a player, an M5 app) are left as
    // zombies for the code that started them: its waitpid() sees the exit.
    auto gone = [pid] {
        std::string stat = slurp_line(("/proc/" + std::to_string(pid) + "/stat").c_str());
        auto close_p = stat.rfind(')');
        if (stat.empty() || close_p == std::string::npos) return true;
        char state = close_p + 2 < stat.size() ? stat[close_p + 2] : 'X';
        return state == 'Z' || state == 'X';
    };
    kill(pid, SIGCONT);   // a stopped program can't act on SIGTERM
    if (kill(pid, SIGTERM) < 0) return gone();
    for (int i = 0; i < 150 && !gone(); ++i) usleep(10000);   // 1.5 s to save and quit
    if (!gone()) {
        std::fprintf(stderr, "zeroshell: %d ignored SIGTERM, killing it\n", pid);
        kill(pid, SIGKILL);
        for (int i = 0; i < 50 && !gone(); ++i) usleep(10000);
    }
    return gone();
#endif
}

}  // namespace zs
