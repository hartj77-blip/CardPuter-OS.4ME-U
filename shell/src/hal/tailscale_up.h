// The flags LinuxHal gives "tailscale up": which control server to sign in
// to (Tailscale's own, or a Headscale server of your own), and an auth key
// instead of the sign-in link. See docs/REMOTE.md (Headscale).
//
// Settings (prefs): tailscale.server = "" (Tailscale) or a URL;
// tailscale.authkey = a key to use once; tailscale.server_used = the server
// of the last "up", so switching servers signs in again (--force-reauth).
#pragma once

#include <string>

namespace zs {

inline const char* const kTailscaleControl = "https://controlplane.tailscale.com";

// "hs.example.com/ " -> "https://hs.example.com". "" stays "" (Tailscale).
inline std::string tailscale_server_url(std::string s) {
    while (!s.empty() && (s.back() == ' ' || s.back() == '/' || s.back() == '\t')) s.pop_back();
    size_t i = s.find_first_not_of(" \t");
    s = i == std::string::npos ? "" : s.substr(i);
    if (s.empty()) return s;
    if (s.find("://") == std::string::npos) s = "https://" + s;
    return s;
}

// For a single-quoted shell word.
inline std::string tailscale_quote(const std::string& s) {
    std::string q = "'";
    for (char ch : s) q += ch == '\'' ? std::string("'\\''") : std::string(1, ch);
    return q + "'";
}

// " --login-server='...' [--force-reauth] [--authkey='file:...']". The server
// is always named: once a non-default one was used, a bare "tailscale up"
// would refuse to quietly change it back. The auth key itself never goes on
// the command line (any process can read that): auth_key_file is a private
// file holding it, which tailscale reads ("file:" prefix).
inline std::string tailscale_up_flags(const std::string& server, const std::string& last,
                                      const std::string& auth_key_file) {
    std::string url = tailscale_server_url(server);
    std::string f = " --login-server=" + tailscale_quote(url.empty() ? kTailscaleControl : url);
    if (url != tailscale_server_url(last)) f += " --force-reauth";
    if (!auth_key_file.empty()) f += " --authkey=" + tailscale_quote("file:" + auth_key_file);
    return f;
}

}  // namespace zs
