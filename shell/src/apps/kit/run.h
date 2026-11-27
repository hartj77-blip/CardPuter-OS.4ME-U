// Running command-line programs (git, make, curl...) from a ZeroShell app
// without freezing the UI. Shared by the app packs (Dev Tools, Libretro).
//
// A Job runs one program on a background thread and streams its output to the
// UI thread, which drains it each tick. In the simulator and the headless CI
// build nothing real is run: a Job replays the canned lines it was given, one
// per drain(), so they stream under both wall-clock and virtual time.
#pragma once

#include <atomic>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

namespace zs::kit {

// True on the PC simulator / headless build (MockHal): run nothing real.
bool simulated();

// Is this program on PATH? (Real builds only; always true when simulated.)
bool have(const std::string& prog);

// Shell-quote one argument for a command built as a string.
std::string quote(const std::string& s);

// How a finished Job ended.
enum class Done { Running, Ok, Failed, Missing, Cancelled };

struct RunOpts {
    std::string cwd;                    // working directory ("" = ours)
    std::vector<std::string> env;       // extra NAME=value entries
    std::vector<std::string> sim;       // simulated output
    bool split_cr = false;              // a '\r' also ends a line (progress output)
};

class Job {
public:
    Job() = default;
    ~Job();
    Job(const Job&) = delete;
    Job& operator=(const Job&) = delete;

    // Run argv[0] (found on PATH) with the rest as arguments. stderr is
    // merged into the output; stdin is /dev/null.
    void start(std::vector<std::string> argv, RunOpts opts = {});

    // Append any new output lines to `out`. Returns how many were added.
    size_t drain(std::vector<std::string>& out);

    void cancel();            // stop the program (and the reader thread)
    void reset();             // stop any run, return to a fresh, startable state
    bool running() const { return running_.load(); }
    bool started() const { return started_; }
    Done status() const { return done_.load(); }
    int exit_code() const { return code_.load(); }
    int lines_seen() const { return seen_.load(); }

private:
    void run_real(std::vector<std::string> argv, RunOpts opts);
    void push(const std::string& line);

    std::thread thread_;
    std::mutex mu_;
    std::vector<std::string> pending_;
    std::atomic<bool> running_{false};
    std::atomic<bool> stop_{false};
    std::atomic<Done> done_{Done::Running};
    std::atomic<int> code_{-1};
    std::atomic<int> seen_{0};
    std::atomic<int> pid_{-1};
    bool started_ = false;
    bool sim_mode_ = false;
    std::vector<std::string> sim_lines_;
    size_t sim_at_ = 0;
};

// Run a command to the end and return its output (stdout + stderr). Blocks:
// only for quick things (git rev-parse). Empty when simulated.
std::string capture(const std::vector<std::string>& argv, const std::string& cwd = "");

// Files and folders. Names only, sorted, without "." entries.
std::vector<std::string> list_dirs(const std::string& dir);
std::vector<std::string> list_files(const std::string& dir);
bool is_dir(const std::string& path);
bool exists(const std::string& path);
bool write_file(const std::string& path, const std::string& text, bool executable = false);
std::string read_file(const std::string& path, size_t max = 1 << 20);
// mkdir -p
bool make_dirs(const std::string& path);

}  // namespace zs::kit
