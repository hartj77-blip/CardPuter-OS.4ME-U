// Checks the flags ZeroShell gives "tailscale up" (Headscale, auth keys).
// Build and run: shell/tests/run.sh
#include <cstdio>
#include <string>

#include "hal/tailscale_up.h"

using namespace zs;

namespace {
int g_failed = 0;

void check(const std::string& got, const std::string& want, const char* what) {
    if (got == want) return;
    std::printf("FAIL %s\n  got:  %s\n  want: %s\n", what, got.c_str(), want.c_str());
    ++g_failed;
}
}  // namespace

int main() {
    check(tailscale_server_url(""), "", "empty is Tailscale");
    check(tailscale_server_url("  "), "", "blank is Tailscale");
    check(tailscale_server_url("hs.example.com"), "https://hs.example.com", "adds https");
    check(tailscale_server_url(" https://hs.example.com:8080/ "), "https://hs.example.com:8080", "trims");
    check(tailscale_server_url("http://10.0.0.2:8080"), "http://10.0.0.2:8080", "keeps http");

    check(tailscale_up_flags("", "", ""), " --login-server='https://controlplane.tailscale.com'",
          "Tailscale, as before");
    check(tailscale_up_flags("hs.example.com", "https://hs.example.com", ""),
          " --login-server='https://hs.example.com'", "same Headscale server");
    check(tailscale_up_flags("hs.example.com", "", ""),
          " --login-server='https://hs.example.com' --force-reauth", "Tailscale -> Headscale");
    check(tailscale_up_flags("", "https://hs.example.com", ""),
          " --login-server='https://controlplane.tailscale.com' --force-reauth", "Headscale -> Tailscale");
    check(tailscale_up_flags("hs.example.com", "https://hs.example.com", "k'1"),
          " --login-server='https://hs.example.com' --authkey='k'\\''1'", "auth key, quoted");

    if (g_failed) return 1;
    std::printf("tailscale_up_test: all passed\n");
    return 0;
}
