// Trace: the path to a host and where it slows down. Runs mtr in report mode,
// one line per hop with packet loss and round-trip time.
#include "apps/net/net_sim.h"
#include "apps/net/target_app.h"

namespace zs {
namespace {

class TraceApp : public TargetApp {
protected:
    const char* title() const override { return "Trace"; }
    const char* input_title() const override { return "Trace the path to a host"; }
    const char* placeholder() const override { return "host, e.g. 1.1.1.1"; }
    const char* waiting_text() const override { return "Tracing the route..."; }

    std::vector<std::string> command(const std::string& t) const override {
        return {"mtr", "--report", "--report-wide", "-n", "-c", "3", t};
    }
    std::vector<std::string> sim(const std::string& t) const override { return net::sim::mtr(t); }

    void finished() override {
        using net::Done;
        if (job().lines_seen() == 0)
            out().append(job().status() == Done::Missing ? "mtr is not installed." : "Could not trace the route.");
    }
};
}  // namespace

std::unique_ptr<App> make_trace_app() { return std::make_unique<TraceApp>(); }

}  // namespace zs
