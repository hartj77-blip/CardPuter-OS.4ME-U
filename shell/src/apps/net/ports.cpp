// Ports: what a host is running. Runs nmap against one address (typed, or
// handed over from Discover with Enter) and lists the open ports and the
// service nmap names on each.
#include "apps/net/net_sim.h"
#include "apps/net/target_app.h"

namespace zs {
namespace {

class PortsApp : public TargetApp {
protected:
    const char* title() const override { return "Ports"; }
    const char* input_title() const override { return "Scan a host's ports"; }
    const char* waiting_text() const override { return "Scanning ports..."; }

    std::vector<std::string> command(const std::string& t) const override {
        // A connect scan (no root needed) of the common ports, with versions.
        return {"nmap", "-Pn", "-T4", "--top-ports", "200", "-sV", "--version-light", t};
    }
    std::vector<std::string> sim(const std::string& t) const override { return net::sim::nmap_ports(t); }

    void finished() override {
        using net::Done;
        if (job().lines_seen() == 0) {
            Done d = job().status();
            out().append(d == Done::Missing ? "nmap is not installed." : "No response from this host.");
        }
    }
};
}  // namespace

std::unique_ptr<App> make_ports_app() { return std::make_unique<PortsApp>(); }

}  // namespace zs
