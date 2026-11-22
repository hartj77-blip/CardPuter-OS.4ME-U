// DNS: look up a name with dig. Left/Right change the record type (A, AAAA,
// MX, TXT, NS); R looks it up again.
#include "apps/net/net_sim.h"
#include "apps/net/target_app.h"

namespace zs {
namespace {

const char* kTypes[] = {"A", "AAAA", "MX", "TXT", "NS"};

class DnsApp : public TargetApp {
protected:
    const char* title() const override { return type(); }
    const char* input_title() const override { return "Look up a name"; }
    const char* placeholder() const override { return "name, e.g. example.com"; }
    const char* hint() const override { return LV_SYMBOL_LEFT " type " LV_SYMBOL_RIGHT "   R run"; }
    const char* waiting_text() const override { return "Looking it up..."; }

    std::vector<std::string> command(const std::string& t) const override {
        return {"dig", "+nocmd", "+noall", "+answer", t, type()};
    }
    std::vector<std::string> sim(const std::string& t) const override { return net::sim::dig(t, type()); }

    bool extra_key(uint32_t key) override {
        if (key == LV_KEY_RIGHT) { ti_ = (ti_ + 1) % int(std::size(kTypes)); rerun(); return true; }
        if (key == LV_KEY_LEFT) { ti_ = (ti_ + int(std::size(kTypes)) - 1) % int(std::size(kTypes)); rerun(); return true; }
        return false;
    }

    void finished() override {
        using net::Done;
        if (job().lines_seen() == 0)
            out().append(job().status() == Done::Missing ? "dig is not installed (dnsutils)."
                                                         : fmt("No %s records.", type()));
    }

private:
    const char* type() const { return kTypes[ti_]; }
    int ti_ = 0;
};
}  // namespace

std::unique_ptr<App> make_dns_app() { return std::make_unique<DnsApp>(); }

}  // namespace zs
