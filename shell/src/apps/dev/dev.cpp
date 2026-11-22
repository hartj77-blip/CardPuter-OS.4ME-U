#include "apps/dev/dev.h"

#include "apps/common.h"
#include "apps/kit/run.h"

namespace zs::dev {

std::string projects_dir() { return home_dir() + "/Code"; }

namespace {
bool has(const std::string& dir, const char* name) { return kit::exists(dir + "/" + name); }

// The first file in dir with this extension, or "".
std::string first_with(const std::string& dir, const std::string& ext) {
    for (auto& f : kit::list_files(dir))
        if (f.size() > ext.size() && f.compare(f.size() - ext.size(), ext.size(), ext) == 0) return f;
    return "";
}

std::string trim(std::string s) {
    while (!s.empty() && (s.back() == '\n' || s.back() == '\r' || s.back() == ' ')) s.pop_back();
    while (!s.empty() && s.front() == ' ') s.erase(s.begin());
    return s;
}
}  // namespace

std::string project_kind(const std::string& dir) {
    if (has(dir, "Cargo.toml")) return "Rust";
    if (has(dir, "go.mod")) return "Go";
    if (has(dir, "package.json")) return "Node.js";
    if (has(dir, "platformio.ini")) return "PlatformIO";
    if (!first_with(dir, ".ino").empty()) return "Arduino";
    if (has(dir, "main.c") || (has(dir, "Makefile") && !first_with(dir, ".c").empty())) return "C";
    if (has(dir, "main.cpp")) return "C++";
    if (!first_with(dir, ".py").empty()) return "Python";
    if (!first_with(dir, ".lua").empty()) return "Lua";
    if (!first_with(dir, ".sh").empty()) return "Shell";
    if (has(dir, "index.html")) return "Web";
    if (has(dir, "Makefile")) return "make";
    return "";
}

RunPlan run_plan(const std::string& dir) {
    // The project says how: one line in .zeroos-run.
    std::string own = trim(kit::read_file(dir + "/.zeroos-run", 4096));
    if (auto nl = own.find('\n'); nl != std::string::npos) own = trim(own.substr(0, nl));
    if (!own.empty()) return {own, "", ""};

    if (has(dir, "Makefile")) {
        std::string mk = kit::read_file(dir + "/Makefile", 1 << 16);
        bool run_target = mk.rfind("run:", 0) == 0 || mk.find("\nrun:") != std::string::npos;
        return {run_target ? "make run" : "make", "make", "make"};
    }
    if (has(dir, "Cargo.toml")) return {"cargo run", "cargo", "cargo"};
    if (has(dir, "go.mod")) return {"go run .", "go", "golang-go"};
    if (has(dir, "package.json")) return {"npm start", "npm", "npm"};
    if (has(dir, "platformio.ini")) return {"pio run -t upload && pio device monitor", "pio", "platformio"};
    if (has(dir, "main.py")) return {"python3 main.py", "python3", "python3"};
    if (std::string py = first_with(dir, ".py"); !py.empty())
        return {"python3 " + kit::quote(py), "python3", "python3"};
    if (has(dir, "main.sh")) return {"bash main.sh", "bash", "bash"};
    if (has(dir, "main.lua")) return {"lua5.4 main.lua", "lua5.4", "lua5.4"};
    if (has(dir, "main.js")) return {"node main.js", "node", "nodejs"};
    if (has(dir, "index.js")) return {"node index.js", "node", "nodejs"};
    if (has(dir, "main.c")) return {"cc -Wall -O2 -o main *.c && ./main", "cc", "gcc"};
    if (has(dir, "main.cpp")) return {"c++ -Wall -O2 -o main *.cpp && ./main", "c++", "g++"};
    if (has(dir, "index.html")) return {"python3 -m http.server 8000", "python3", "python3"};
    if (std::string sh = first_with(dir, ".sh"); !sh.empty()) return {"bash " + kit::quote(sh), "bash", "bash"};
    return {};
}

std::string editor() {
    if (kit::have("micro")) return "micro";
    if (kit::have("nano")) return "nano";
    return "vi";
}

std::vector<std::string> git_repos() {
    std::vector<std::string> v;
    std::string base = projects_dir();
    for (auto& d : kit::list_dirs(base))
        if (kit::exists(base + "/" + d + "/.git")) v.push_back(base + "/" + d);
    return v;
}

}  // namespace zs::dev
