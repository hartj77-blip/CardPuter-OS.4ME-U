// Memory and CPU readings from /proc and /sys, shared by the Linux backend
// (everything) and the mock backend (the shell's own memory, when available).
#pragma once

#include "hal/hal.h"

namespace zs {

// Fills the memory, zram, swap and CPU fields of `s`. Missing files leave
// fields at zero. `cpu_prev` holds the previous /proc/stat sample (busy, total).
void read_mem_cpu(SystemInfo& s, long long cpu_prev[2]);

// The shell's own resident and peak memory, in kB. False if unavailable.
bool read_self_mem(int& rss_kb, int& peak_kb);

// The n processes using the most resident memory.
std::vector<ProcMem> top_processes(size_t n);

// What Hal::processes() lists: every process of `uid` except `self` and the
// session programs ZeroShell needs (systemd --user, PipeWire, D-Bus...),
// biggest first. `prev` keeps each pid's CPU time and the system's total
// between calls, for cpu_percent.
struct ProcCpuPrev {
    std::vector<std::pair<int, long long>> ticks;   // pid, utime + stime
    long long total = 0;                            // /proc/stat cpu, all fields
};
std::vector<ProcInfo> user_processes(unsigned uid, int self, ProcCpuPrev& prev);

// The name a person knows a program by: "python3 /home/pi/weather.py" is
// "weather.py", "/usr/bin/foo --x" is "foo". `comm` is the kernel's name.
std::string process_display_name(const std::string& comm, const std::vector<std::string>& argv);

// Hal::process_stop(): if `pid` is one user_processes() lists, SIGTERM it,
// then SIGKILL after 1.5 s. True once it has gone or is a zombie (a child
// of ours is left for whoever started it to reap).
bool stop_user_process(unsigned uid, int self, int pid);

// A session program processes() never lists (stopping it breaks the UI).
bool process_is_essential(const std::string& comm);

}  // namespace zs
