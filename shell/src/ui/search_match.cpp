#include "ui/search_match.h"

#include <cctype>
#include <vector>

namespace zs {
namespace {

// Lower case; anything that isn't a letter or digit splits words
// ("Wi-Fi" -> "wi fi", "Battery & power" -> "battery power").
std::vector<std::string> words(const std::string& s) {
    std::vector<std::string> out;
    std::string w;
    for (char ch : s) {
        if (std::isalnum((unsigned char)ch)) {
            w += char(std::tolower((unsigned char)ch));
        } else if (!w.empty()) {
            out.push_back(w);
            w.clear();
        }
    }
    if (!w.empty()) out.push_back(w);
    return out;
}

std::string joined(const std::vector<std::string>& ws) {
    std::string out;
    for (const auto& w : ws) out += w;
    return out;
}

bool starts_with(const std::string& s, const std::string& p) { return s.compare(0, p.size(), p) == 0; }

bool starts_a_word(const std::vector<std::string>& ws, const std::string& q) {
    for (const auto& w : ws)
        if (starts_with(w, q)) return true;
    return false;
}

}  // namespace

std::string search_norm(const std::string& s) {
    std::string out;
    for (char ch : s)
        if (std::isalnum((unsigned char)ch) || ch == ' ') out += char(std::tolower((unsigned char)ch));
    return out;
}

int search_score(const std::string& name, const std::string& keywords, const std::string& query) {
    std::vector<std::string> q = words(query);
    if (q.empty()) return 0;
    std::vector<std::string> n = words(name);
    std::string nj = joined(n), qj = joined(q);
    if (nj == qj) return 100;
    if (starts_with(nj, qj)) return 80;
    bool all_start = true, all_in = true;
    for (const auto& w : q) {
        if (!starts_a_word(n, w)) all_start = false;
        // In the name, also across its word breaks ("wifi" in "Wi-Fi").
        if (nj.find(w) == std::string::npos) all_in = false;
    }
    if (all_start) return 60;
    if (all_in) return 40;
    // Each word in the name or in a keyword ("bt off" finds "Turn Bluetooth
    // off": "off" in the name, "bt" a keyword).
    std::vector<std::string> k = words(keywords);
    for (const auto& w : q) {
        bool found = nj.find(w) != std::string::npos;
        for (size_t i = 0; !found && i < k.size(); ++i) found = k[i].find(w) != std::string::npos;
        if (!found) return 0;
    }
    return 20;
}

}  // namespace zs
