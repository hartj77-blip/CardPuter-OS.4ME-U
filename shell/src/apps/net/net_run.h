// Running the command-line network tools (nmap, arp-scan, tcpdump, mtr, dig,
// ip, ss...) from a ZeroShell app without freezing the UI.
//
// A Job runs one program on a background thread and streams its stdout to the
// UI thread, which drains it each tick. Programs that keep running (tcpdump,
// mtr) are stopped with cancel(); one-shot ones (dig, nmap) finish on their
// own. In the simulator and the headless CI build there is no real network, so
// a Job replays canned lines instead of running anything.
#pragma once

#include <atomic>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

namespace zs::net {

// True on the PC simulator / headless build (MockHal): don't touch the real
// network, replay the sim lines a Job is given.
bool simulated();

// Is this program on PATH? (Real builds only; always true when simulated.)
bool have(const std::string& prog);

// How a finished Job ended.
enum class Done { Running, Ok, Failed, Missing, Cancelled };

class Job {
public:
    Job() = default;
    ~Job();
    Job(const Job&) = delete;
    Job& operator=(const Job&) = delete;

    // Run argv[0] (found on PATH) with the rest as arguments. With root, it is
    // run through `sudo -n` (the net pack ships a sudoers rule for the exact
    // tools). When simulated(), `sim` lines are emitted one every sim_ms ms
    // instead, and argv is ignored. stderr is merged into the output.
    void start(std::vector<std::string> argv, bool root,
               std::vector<std::string> sim, int sim_ms = 150);

    // Append any new output lines to `out`. Returns how many were added.
    size_t drain(std::vector<std::string>& out);

    void cancel();            // stop the program (and the reader thread)
    void reset();             // stop any run, return to a fresh, startable state
    bool running() const { return running_.load(); }
    bool started() const { return started_; }
    Done status() const { return done_.load(); }
    int lines_seen() const { return seen_.load(); }

private:
    void run_real(const std::vector<std::string>& argv, bool root);
    void push(const std::string& line);

    std::thread thread_;
    std::mutex mu_;
    std::vector<std::string> pending_;    // lines the UI hasn't drained yet
    std::atomic<bool> running_{false};
    std::atomic<bool> stop_{false};
    std::atomic<Done> done_{Done::Running};
    std::atomic<int> seen_{0};
    std::atomic<int> pid_{-1};            // child pid while it runs (real)
    bool started_ = false;
    // Simulated mode: no subprocess, no thread. The canned lines are released
    // one per drain() so they stream as the UI polls, in both the real
    // simulator (wall-clock ticks) and the headless renderer (virtual ticks).
    bool sim_mode_ = false;
    std::vector<std::string> sim_lines_;
    size_t sim_at_ = 0;
};

// Shell-quote one argument for a command built as a string (used where a tool
// is driven through a shell pipeline).
std::string quote(const std::string& s);

}  // namespace zs::net
