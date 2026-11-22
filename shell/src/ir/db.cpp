#include "ir/db.h"

#include <dirent.h>
#include <sys/stat.h>

#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <fstream>
#include <sstream>

namespace zs::ir {
namespace {

std::string lower(std::string s) {
    for (auto& ch : s) ch = char(std::tolower((unsigned char)ch));
    return s;
}

bool exists(const std::string& p) {
    struct stat st;
    return stat(p.c_str(), &st) == 0;
}

}  // namespace

std::string db_root() {
    if (const char* e = std::getenv("ZEROOS_IR_DB"); e && *e) return e;
    return "/usr/share/zeroos/ir";
}

std::string user_dir() {
    const char* h = std::getenv("HOME");
    return std::string(h ? h : "/tmp") + "/IR";
}

std::vector<RemoteRef> load_index(const std::string& root) {
    std::vector<RemoteRef> out;
    std::ifstream f(root + "/index.tsv");
    std::string line;
    while (std::getline(f, line)) {
        if (line.empty() || line[0] == '#') continue;
        std::vector<std::string> col;
        std::stringstream ss(line);
        std::string c;
        while (std::getline(ss, c, '\t')) col.push_back(c);
        if (col.size() < 5) continue;
        out.push_back({col[0], col[1], col[2], col[3], std::atoi(col[4].c_str())});
    }
    return out;
}

std::vector<std::string> universal_names(const std::string& root) {
    std::vector<std::string> out;
    if (DIR* d = opendir((root + "/universal").c_str())) {
        while (dirent* e = readdir(d)) {
            std::string n = e->d_name;
            if (n.size() > 3 && n.compare(n.size() - 3, 3, ".ir") == 0) out.push_back(n.substr(0, n.size() - 3));
        }
        closedir(d);
    }
    // Most-wanted first, the rest by name.
    static const char* order[] = {"TVs", "Projectors", "Audio", "ACs", "Fans", "Lights"};
    auto rank = [](const std::string& n) {
        for (int i = 0; i < 6; ++i)
            if (n == order[i]) return i;
        return 99;
    };
    std::sort(out.begin(), out.end(), [&](const std::string& a, const std::string& b) {
        return rank(a) != rank(b) ? rank(a) < rank(b) : a < b;
    });
    return out;
}

bool load_universal(const std::string& root, const std::string& name, Universal& out) {
    std::vector<Signal> sigs;
    if (!load(root + "/universal/" + name + ".ir", sigs)) return false;
    out = Universal{};
    out.name = name;
    for (auto& s : sigs) {
        size_t i = 0;
        while (i < out.buttons.size() && out.buttons[i] != s.name) ++i;
        if (i == out.buttons.size()) {
            out.buttons.push_back(s.name);
            out.codes.emplace_back();
        }
        out.codes[i].push_back(s);
    }
    return !out.buttons.empty();
}

std::vector<std::string> user_remotes() {
    std::vector<std::string> out;
    std::string dir = user_dir();
    if (DIR* d = opendir(dir.c_str())) {
        while (dirent* e = readdir(d)) {
            std::string n = e->d_name;
            if (n[0] != '.' && n.size() > 3 && lower(n.substr(n.size() - 3)) == ".ir") out.push_back(dir + "/" + n);
        }
        closedir(d);
    }
    std::sort(out.begin(), out.end(), [](const std::string& a, const std::string& b) { return lower(a) < lower(b); });
    return out;
}

std::string user_remote_path(const std::string& name) {
    std::string dir = user_dir();
    mkdir(dir.c_str(), 0755);
    std::string clean;
    for (char ch : name) clean += (ch == '/' || ch == '\\' || (unsigned char)ch < 32) ? ' ' : ch;
    while (!clean.empty() && (clean[0] == ' ' || clean[0] == '.')) clean.erase(0, 1);
    while (!clean.empty() && clean.back() == ' ') clean.pop_back();
    if (clean.empty()) clean = "Remote";
    std::string p = dir + "/" + clean + ".ir";
    for (int i = 2; exists(p); ++i) p = dir + "/" + clean + " " + std::to_string(i) + ".ir";
    return p;
}

std::string display(const std::string& s) {
    std::string out = s;
    std::replace(out.begin(), out.end(), '_', ' ');
    return out;
}

std::string stem(const std::string& path) {
    auto slash = path.find_last_of('/');
    std::string n = slash == std::string::npos ? path : path.substr(slash + 1);
    auto dot = n.find_last_of('.');
    return dot == std::string::npos ? n : n.substr(0, dot);
}

std::string source_brand(const std::string& src) {
    auto a = src.find('/');
    if (a == std::string::npos) return "";
    auto b = src.find('/', a + 1);
    return display(src.substr(a + 1, b == std::string::npos ? std::string::npos : b - a - 1));
}

bool contains(const std::string& haystack, const std::string& needle) {
    return lower(haystack).find(lower(needle)) != std::string::npos;
}

}  // namespace zs::ir
