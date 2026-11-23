// See desktop_entry.h. No LVGL here: tests/desktop_entry_test.cpp uses it.
#include "ui/desktop_entry.h"

#include <cctype>
#include <sstream>

namespace zs {

namespace {

std::string trim(const std::string& s) {
    size_t a = s.find_first_not_of(" \t\r");
    if (a == std::string::npos) return "";
    size_t b = s.find_last_not_of(" \t\r");
    return s.substr(a, b - a + 1);
}

bool truthy(const std::string& v) { return v == "true" || v == "1" || v == "yes" || v == "True"; }

// Exec's field codes stand for files or URLs the launcher would pass; it
// never passes any, so they go. "%%" is a plain "%".
std::string strip_field_codes(const std::string& exec) {
    std::string out;
    for (size_t i = 0; i < exec.size(); ++i) {
        if (exec[i] == '%' && i + 1 < exec.size()) {
            if (exec[i + 1] == '%') out += '%';
            ++i;
            continue;
        }
        out += exec[i];
    }
    return trim(out);
}

bool has_control(const std::string& s) {
    for (unsigned char ch : s)
        if (ch < 0x20 || ch == 0x7f) return true;
    return false;
}

}  // namespace

std::optional<DesktopEntry> parse_desktop_entry(const std::string& contents) {
    DesktopEntry e;
    bool in_group = false, hidden = false, app = true;
    std::istringstream in(contents);
    std::string line;
    while (std::getline(in, line)) {
        if (!line.empty() && line.back() == '\r') line.pop_back();
        if (line.empty() || line[0] == '#' || line[0] == ';') continue;
        if (line[0] == '[') {
            in_group = trim(line) == "[Desktop Entry]";
            continue;
        }
        if (!in_group) continue;
        size_t eq = line.find('=');
        if (eq == std::string::npos) continue;
        std::string k = trim(line.substr(0, eq)), v = trim(line.substr(eq + 1));
        if (k == "Name") e.name = v;
        else if (k == "Exec") e.exec = strip_field_codes(v);
        else if (k == "Icon") e.icon = v;
        else if (k == "Terminal") e.terminal = truthy(v);
        else if (k == "Sysplause") e.sysplause = truthy(v);
        else if (k == "Hidden" || k == "NoDisplay") hidden = hidden || truthy(v);
        else if (k == "Type") app = v == "Application";
    }
    if (hidden || !app || e.name.empty() || e.exec.empty() || e.name.size() > 64 || e.exec.size() > 1024 ||
        has_control(e.name) || has_control(e.exec) || has_control(e.icon))
        return std::nullopt;
    return e;
}

std::string desktop_app_id(const std::string& base) {
    std::string id = "m5-";
    for (unsigned char ch : base) {
        char c = char(std::tolower(ch));
        bool ok = (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9');
        id += ok ? c : '-';
    }
    return id;
}

}  // namespace zs
