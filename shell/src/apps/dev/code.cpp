// Code: your projects in ~/Code. Make one from a template, browse its files,
// edit them in micro (in the Terminal app) and run the project with one key.
//
//   Projects   Enter open      N new project
//   Files      Enter edit      R run      T terminal here
//              N new file      G Git      Esc up / back
//
// How R runs a project: the command in its .zeroos-run file if it has one,
// otherwise what its files suggest (Makefile, Cargo.toml, main.py, ...).
#include <cstdint>
#include <string>
#include <vector>

#include "apps/common.h"
#include "apps/dev/dev.h"
#include "apps/kit/run.h"
#include "apps/kit/ui.h"

namespace zs {
namespace {
using namespace theme;

struct Template {
    const char* name;
    const char* about;
    std::vector<std::pair<const char*, const char*>> files;   // name, contents
};

const std::vector<Template>& templates() {
    static const std::vector<Template> t = {
        {"Python", "main.py, runs with python3",
         {{"main.py",
           "# Press R in Code to run this, Enter on a file to edit it.\n"
           "name = input(\"What's your name? \")\n"
           "print(f\"Hello, {name}! This is Python on ZeroOS.\")\n"}}},
        {"C", "main.c and a Makefile, builds with make",
         {{"main.c",
           "#include <stdio.h>\n\n"
           "int main(void) {\n"
           "    printf(\"Hello from C on ZeroOS!\\n\");\n"
           "    return 0;\n"
           "}\n"},
          {"Makefile",
           "CFLAGS ?= -Wall -O2\n\n"
           "main: main.c\n\t$(CC) $(CFLAGS) -o $@ $^\n\n"
           "run: main\n\t./main\n\n"
           "clean:\n\trm -f main\n\n"
           ".PHONY: run clean\n"},
          {".gitignore", "main\n"}}},
        {"Shell script", "main.sh, runs with bash",
         {{"main.sh",
           "#!/usr/bin/env bash\n"
           "set -euo pipefail\n"
           "echo \"Hello from $(hostname), up $(uptime -p | sed 's/up //').\"\n"}}},
        {"Lua", "main.lua, runs with lua5.4",
         {{"main.lua", "print(\"Hello from Lua on ZeroOS!\")\n"}}},
        {"Web page", "index.html, served on port 8000",
         {{"index.html",
           "<!doctype html>\n<meta charset=\"utf-8\">\n<title>Hello</title>\n"
           "<h1>Hello from a Cardputer</h1>\n"
           "<p>Press R in Code to serve this page, then open\n"
           "http://&lt;this device&gt;:8000 on your phone or PC.</p>\n"}}},
        {"Empty", "just a README",
         {{"README.md", "# New project\n"}}},
    };
    return t;
}

// A project or file name: letters, digits, - _ . and spaces; no path tricks.
std::string clean_name(std::string s) {
    while (!s.empty() && s.front() == ' ') s.erase(s.begin());
    while (!s.empty() && s.back() == ' ') s.pop_back();
    std::string out;
    for (char ch : s) {
        bool ok = (ch >= 'a' && ch <= 'z') || (ch >= 'A' && ch <= 'Z') || (ch >= '0' && ch <= '9') ||
                  ch == '-' || ch == '_' || ch == '.' || ch == ' ';
        out += ok ? ch : '-';
    }
    if (out.empty() || out[0] == '.') return "";
    return out;
}

const char* file_symbol(const std::string& name) {
    auto ext = name.substr(name.find_last_of('.') == std::string::npos ? name.size() : name.find_last_of('.'));
    if (ext == ".png" || ext == ".jpg" || ext == ".gif") return LV_SYMBOL_IMAGE;
    if (name == "Makefile" || ext == ".sh") return LV_SYMBOL_SETTINGS;
    return LV_SYMBOL_FILE;
}

class CodeApp : public App {
public:
    void build(lv_obj_t* root) override {
        root_ = root;
        base_ = dev::projects_dir();
        kit::make_dirs(base_);
        std::string arg = shell_app_arg();
        if (!arg.empty() && kit::is_dir(arg)) {
            project_ = arg;
            show_files();
        } else {
            show_projects();
        }
    }

    void tick() override {
        if (pending_ < 0 && !pending_submit_) return;
        int i = pending_;
        pending_ = -1;
        if (pending_submit_) {
            pending_submit_ = false;
            submitted();
            return;
        }
        picked(i);
    }

    bool on_key(uint32_t key) override {
        if (page_ == Page::Projects) {
            if (key == 'n' || key == 'N') { ask_project_name(); return true; }
        } else if (page_ == Page::Files) {
            if (key == 'r' || key == 'R') { run(); return true; }
            if (key == 't' || key == 'T') { kit::terminal_run("bash", here()); return true; }
            if (key == 'n' || key == 'N') { ask_file_name(); return true; }
            if (key == 'g' || key == 'G') {
                if (!app_find("git")) shell_toast("Git is part of Dev Tools");
                else shell_open_app("git", project_);
                return true;
            }
        }
        return false;
    }

    bool on_back() override {
        switch (page_) {
            case Page::Projects: return false;
            case Page::Files:
                if (!sub_.empty()) {
                    auto slash = sub_.find_last_of('/');
                    sub_ = slash == std::string::npos ? "" : sub_.substr(0, slash);
                    show_files();
                } else {
                    show_projects();
                }
                return true;
            case Page::NewName: show_projects(); return true;
            case Page::NewKind: ask_project_name(); return true;
            case Page::NewFile: show_files(); return true;
        }
        return false;
    }

private:
    enum class Page { Projects, Files, NewName, NewKind, NewFile };

    void clear() {
        lv_obj_clean(root_);
        if (group) lv_group_remove_all_objs(group);
        rows_.clear();
    }

    static void on_click(lv_event_t* e) {
        auto* self = static_cast<CodeApp*>(lv_event_get_user_data(e));
        self->pending_ = int(lv_obj_get_index(lv_event_get_target_obj(e)));
    }

    lv_obj_t* list_page(const std::string& title, const std::string& hints) {
        kit::title_row(root_, title, hints);
        lv_obj_t* list = make_list(root_, 0, 18, kScreenW, kContentH - 18);
        return list;
    }

    // ---- Projects
    void show_projects() {
        page_ = Page::Projects;
        project_.clear();
        sub_.clear();
        clear();
        lv_obj_t* list = list_page("Projects", "N new project");
        for (auto& d : kit::list_dirs(base_)) {
            std::string info = dev::project_kind(base_ + "/" + d);
            std::string text = info.empty() ? d : d + "   " + info;
            list_button(list, LV_SYMBOL_DIRECTORY, text.c_str(), on_click, this);
            rows_.push_back(d);
        }
        list_button(list, LV_SYMBOL_PLUS, "New project", on_click, this);
        rows_.push_back("");
        finish_list(list);
    }

    // ---- Files of a project
    std::string here() const { return sub_.empty() ? project_ : project_ + "/" + sub_; }

    void show_files() {
        page_ = Page::Files;
        clear();
        std::string name = project_.substr(project_.find_last_of('/') + 1);
        if (!sub_.empty()) name += "/" + sub_;
        lv_obj_t* list = list_page(name, "R run  T term  N new  G git");
        if (!sub_.empty()) {
            list_button(list, LV_SYMBOL_UP, "..", on_click, this);
            rows_.push_back("..");
        }
        for (auto& d : kit::list_dirs(here())) {
            list_button(list, LV_SYMBOL_DIRECTORY, (d + "/").c_str(), on_click, this);
            rows_.push_back(d + "/");
        }
        for (auto& f : kit::list_files(here())) {
            list_button(list, file_symbol(f), f.c_str(), on_click, this);
            rows_.push_back(f);
        }
        if (rows_.empty() || (rows_.size() == 1 && rows_[0] == "..")) {
            list_button(list, LV_SYMBOL_PLUS, "New file", on_click, this);
            rows_.push_back("");
        }
        finish_list(list);
        dev::RunPlan plan = dev::run_plan(project_);
        lv_obj_t* foot = label(root_, plan.command.empty() ? "R: no run command found (add .zeroos-run)"
                                                           : ("R runs: " + plan.command).c_str(),
                               &zs_font_12, kMuted);
        lv_obj_set_width(foot, kScreenW - 8);
        lv_label_set_long_mode(foot, LV_LABEL_LONG_DOT);
        lv_obj_align(foot, LV_ALIGN_BOTTOM_LEFT, 6, -2);
        lv_obj_set_height(list, kContentH - 18 - 16);
    }

    void run() {
        dev::RunPlan plan = dev::run_plan(project_);
        if (plan.command.empty()) {
            shell_toast("Nothing to run: add main.py, a Makefile or .zeroos-run");
            return;
        }
        if (!plan.tool.empty() && !kit::have(plan.tool)) {
            shell_toast(plan.tool + " is not installed (sudo apt install " + plan.package + ")");
            return;
        }
        kit::terminal_run(plan.command, project_);
    }

    void edit(const std::string& path) {
        std::string ed = dev::editor();
        std::string file = path.substr(path.find_last_of('/') + 1);
        kit::terminal_run(ed + " " + kit::quote(file), path.substr(0, path.find_last_of('/')));
    }

    // ---- New project: name, then template
    void ask_project_name() {
        page_ = Page::NewName;
        clear();
        kit::input_screen(root_, group, "Name the new project", "e.g. weather-station", "",
                          [this](const std::string& v) { typed_ = v; pending_submit_ = true; });
    }

    void ask_template() {
        page_ = Page::NewKind;
        clear();
        lv_obj_t* list = list_page("New project: " + new_name_, "Enter create");
        for (auto& t : templates()) {
            std::string text = std::string(t.name) + "   " + t.about;
            list_button(list, LV_SYMBOL_FILE, text.c_str(), on_click, this);
            rows_.push_back(t.name);
        }
        finish_list(list);
    }

    void create_project(const Template& t) {
        std::string dir = base_ + "/" + new_name_;
        if (!kit::make_dirs(dir)) {
            shell_toast("Could not make " + dir);
            return;
        }
        for (auto& [name, text] : t.files) {
            std::string p = dir + "/" + name;
            if (!kit::exists(p)) kit::write_file(p, text, std::string(name).find(".sh") != std::string::npos);
        }
        if (kit::simulated()) kit::make_dirs(dir + "/.git");   // as git init would
        else if (!kit::exists(dir + "/.git") && kit::have("git")) kit::capture({"git", "init", "-q"}, dir);
        project_ = dir;
        sub_.clear();
        show_files();
        shell_toast("Made " + new_name_ + ": R runs it");
    }

    // ---- New file in the current folder
    void ask_file_name() {
        page_ = Page::NewFile;
        clear();
        kit::input_screen(root_, group, "Name the new file", "e.g. sensors.py", "",
                          [this](const std::string& v) { typed_ = v; pending_submit_ = true; });
    }

    void submitted() {
        std::string name = clean_name(typed_);
        if (name.empty()) {
            shell_toast("Use letters, digits, - _ and .");
            return;
        }
        if (page_ == Page::NewName) {
            if (kit::exists(base_ + "/" + name)) {
                project_ = base_ + "/" + name;   // already there: just open it
                sub_.clear();
                show_files();
                return;
            }
            new_name_ = name;
            ask_template();
        } else if (page_ == Page::NewFile) {
            std::string p = here() + "/" + name;
            if (!kit::exists(p)) kit::write_file(p, "");
            show_files();
            edit(p);
        }
    }

    void picked(int i) {
        if (i < 0 || size_t(i) >= rows_.size()) return;
        std::string row = rows_[size_t(i)];
        switch (page_) {
            case Page::Projects:
                if (row.empty()) { ask_project_name(); return; }
                project_ = base_ + "/" + row;
                sub_.clear();
                show_files();
                return;
            case Page::Files:
                if (row.empty()) { ask_file_name(); return; }
                if (row == "..") { on_back(); return; }
                if (row.back() == '/') {
                    row.pop_back();
                    sub_ = sub_.empty() ? row : sub_ + "/" + row;
                    show_files();
                    return;
                }
                edit(here() + "/" + row);
                return;
            case Page::NewKind:
                for (auto& t : templates())
                    if (row == t.name) create_project(t);
                return;
            default: return;
        }
    }

    lv_obj_t* root_ = nullptr;
    Page page_ = Page::Projects;
    std::string base_, project_, sub_, new_name_, typed_;
    std::vector<std::string> rows_;
    int pending_ = -1;
    bool pending_submit_ = false;
};
}  // namespace

std::unique_ptr<App> make_code_app() { return std::make_unique<CodeApp>(); }

}  // namespace zs
