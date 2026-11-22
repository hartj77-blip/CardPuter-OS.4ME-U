// Git: the repositories in ~/Code. Status, log and diff at a glance; commit
// everything, pull and push with one key; clone a new one.
//
//   Repositories  Enter open          (last row: clone a repository)
//   Repository    S status  L log  D diff (V: full diff in the terminal)
//                 C commit all  P pull  U push  F fetch  T terminal here
//
// Git never asks for a password here (GIT_TERMINAL_PROMPT=0): if a push
// needs one, T opens the terminal in the repository to do it there, once.
#include <string>
#include <vector>

#include "apps/common.h"
#include "apps/dev/dev.h"
#include "apps/kit/run.h"
#include "apps/kit/ui.h"

namespace zs {
namespace {
using namespace theme;

std::string base_name(const std::string& p) { return p.substr(p.find_last_of('/') + 1); }

// The repository name from a clone URL: .../name.git -> name.
std::string repo_from_url(std::string url) {
    while (!url.empty() && url.back() == '/') url.pop_back();
    std::string n = url.substr(url.find_last_of("/:") + 1);
    if (n.size() > 4 && n.compare(n.size() - 4, 4, ".git") == 0) n.resize(n.size() - 4);
    return n;
}

namespace sim {
std::vector<std::string> status() {
    return {"## main...origin/main [ahead 1]", " M main.py", "?? notes.md"};
}
std::vector<std::string> log() {
    return {"3f2a9c1 Start the weather station", "8be1d07 Read the BME280 over I2C",
            "c41f5e2 Send readings over LoRa", "e09b6aa (HEAD -> main) Show readings on the screen"};
}
std::vector<std::string> diff() {
    return {" main.py  | 12 ++++++++----", " notes.md |  3 +++", " 2 files changed, 11 insertions(+), 4 deletions(-)"};
}
std::vector<std::string> commit(const std::string& msg) {
    return {"[main 5d1c2e4] " + msg, " 2 files changed, 11 insertions(+), 4 deletions(-)", " create mode 100644 notes.md"};
}
std::vector<std::string> pull() { return {"Already up to date."}; }
std::vector<std::string> push() {
    return {"To github.com:you/weather-station.git", "   e09b6aa..5d1c2e4  main -> main"};
}
std::vector<std::string> clone(const std::string& name) {
    return {"Cloning into '" + name + "'...", "Receiving objects: 100% (214/214), 61.20 KiB, done.",
            "Resolving deltas: 100% (97/97), done."};
}
}  // namespace sim

class GitApp : public App {
public:
    void build(lv_obj_t* root) override {
        root_ = root;
        kit::make_dirs(dev::projects_dir());
        std::string arg = shell_app_arg();
        if (!arg.empty() && kit::is_dir(arg)) open_repo(arg);
        else show_repos();
    }

    void tick() override {
        if (pending_row_ >= 0) {
            int i = pending_row_;
            pending_row_ = -1;
            if (size_t(i) < repos_.size()) open_repo(repos_[size_t(i)]);
            else ask_clone();
            return;
        }
        if (pending_submit_) {
            pending_submit_ = false;
            submitted();
            return;
        }
        if (!job_.started()) return;
        std::vector<std::string> lines;
        if (job_.drain(lines))
            for (auto& l : lines) {
                out_.append(l);
                if (l.find("could not read Username") != std::string::npos ||
                    l.find("Permission denied (publickey)") != std::string::npos)
                    need_auth_ = true;
                if (l.find("Please tell me who you are") != std::string::npos) need_name_ = true;
            }
        if (!job_.running() && !finished_) {
            finished_ = true;
            done();
        }
    }

    bool on_key(uint32_t key) override {
        if (page_ != Page::Repo) return false;
        switch (key) {
            case 's': case 'S': status(); return true;
            case 'l': case 'L':
                run("Log", {"git", "--no-pager", "log", "--oneline", "--decorate", "-n", "40", "--reverse"}, sim::log());
                return true;
            case 'd': case 'D':
                run("Changes", {"sh", "-c", "git --no-pager diff --stat; git --no-pager diff --stat --cached"},
                    sim::diff());
                return true;
            case 'v': case 'V': kit::terminal_run("git diff; git diff --cached", repo_); return true;
            case 'p': case 'P': run("Pull", {"git", "pull", "--ff-only"}, sim::pull()); return true;
            case 'f': case 'F': run("Fetch", {"git", "fetch", "--prune"}, {"From github.com:you/weather-station"}); return true;
            case 'u': case 'U': push(); return true;
            case 'c': case 'C': ask_commit(); return true;
            case 't': case 'T': kit::terminal_run("bash", repo_); return true;
            default: return false;
        }
    }

    bool on_back() override {
        switch (page_) {
            case Page::Repos: return false;
            case Page::Repo: job_.reset(); show_repos(); return true;
            case Page::Commit: open_repo(repo_); return true;
            case Page::Clone: show_repos(); return true;
            case Page::Cloning: job_.reset(); show_repos(); return true;
        }
        return false;
    }

private:
    enum class Page { Repos, Repo, Commit, Clone, Cloning };

    void clear() {
        lv_obj_clean(root_);
        if (group) lv_group_remove_all_objs(group);
    }

    static void on_click(lv_event_t* e) {
        auto* self = static_cast<GitApp*>(lv_event_get_user_data(e));
        self->pending_row_ = int(lv_obj_get_index(lv_event_get_target_obj(e)));
    }

    // ---- The list of repositories
    void show_repos() {
        page_ = Page::Repos;
        clear();
        repos_ = dev::git_repos();
        kit::title_row(root_, "Repositories", "in ~/Code");
        lv_obj_t* list = make_list(root_, 0, 18, kScreenW, kContentH - 18);
        for (auto& r : repos_) list_button(list, LV_SYMBOL_SHUFFLE, base_name(r).c_str(), on_click, this);
        list_button(list, LV_SYMBOL_DOWNLOAD, "Clone a repository", on_click, this);
        finish_list(list);
        if (!kit::have("git")) shell_toast("git is not installed (sudo apt install git)");
    }

    // ---- One repository
    std::string branch() {
        if (kit::simulated()) return "main";
        std::string b = kit::capture({"git", "rev-parse", "--abbrev-ref", "HEAD"}, repo_);
        while (!b.empty() && (b.back() == '\n' || b.back() == '\r')) b.pop_back();
        return b.find(' ') == std::string::npos ? b : "";
    }

    void open_repo(const std::string& path) {
        page_ = Page::Repo;
        repo_ = path;
        clear();
        std::string b = branch();
        title_ = kit::title_row(root_, base_name(repo_) + (b.empty() ? "" : "  " LV_SYMBOL_SHUFFLE " " + b), "");
        lv_obj_set_width(title_, kScreenW - 90);
        what_ = label(root_, "", &zs_font_12, kMuted);
        lv_obj_align(what_, LV_ALIGN_TOP_RIGHT, -4, 3);
        out_.build(root_, 18, 16);
        lv_obj_t* keys = label(root_, "S stat  L log  D diff  C commit  P pull  U push  T term", &zs_font_12, kMuted);
        lv_obj_align(keys, LV_ALIGN_BOTTOM_LEFT, 6, -1);
        status();
    }

    void status() { run("Status", {"git", "-c", "color.ui=never", "status", "-sb"}, sim::status()); }

    void push() {
        // A branch with no upstream yet: push it to origin and remember that.
        bool upstream = kit::simulated() ||
                        kit::capture({"git", "rev-parse", "--abbrev-ref", "@{u}"}, repo_).find("fatal") == std::string::npos;
        if (upstream) run("Push", {"git", "push"}, sim::push());
        else run("Push", {"git", "push", "-u", "origin", "HEAD"}, sim::push());
    }

    void run(const char* what, std::vector<std::string> argv, std::vector<std::string> sim_lines) {
        job_.reset();
        out_.clear();
        out_.set_placeholder(std::string(what) + "...");
        if (what_) lv_label_set_text(what_, what);
        need_auth_ = need_name_ = false;
        finished_ = false;
        kit::RunOpts o;
        o.cwd = repo_;
        o.env = {"GIT_TERMINAL_PROMPT=0", "GIT_SSH_COMMAND=ssh -o BatchMode=yes", "LANG=C"};
        o.sim = std::move(sim_lines);
        job_.start(std::move(argv), std::move(o));
    }

    void done() {
        if (page_ == Page::Cloning) {
            if (job_.status() == kit::Done::Ok) {
                std::string dir = dev::projects_dir() + "/" + clone_name_;
                if (kit::simulated()) kit::make_dirs(dir + "/.git");
                shell_toast("Cloned " + clone_name_);
                open_repo(dir);
            } else {
                out_.append(job_.status() == kit::Done::Missing ? "git is not installed." : "Clone failed.");
            }
            return;
        }
        using kit::Done;
        if (job_.status() == Done::Missing) out_.append("git is not installed (sudo apt install git).");
        else if (job_.lines_seen() == 0 && job_.status() == Done::Ok) out_.append("Nothing to show.");
        if (need_auth_)
            out_.append("Sign-in needed. Press T and run the command in the terminal once, "
                        "or add an SSH key to your Git host.");
        if (need_name_)
            out_.append("Set your name first. Press T, then: git config --global user.name \"You\" "
                        "and git config --global user.email you@example.com");
        // The branch may have changed (pull, first commit).
        std::string b = branch();
        if (title_) lv_label_set_text(title_, (base_name(repo_) + (b.empty() ? "" : "  " LV_SYMBOL_SHUFFLE " " + b)).c_str());
    }

    // ---- Commit everything
    void ask_commit() {
        page_ = Page::Commit;
        job_.reset();
        clear();
        title_ = what_ = nullptr;
        kit::input_screen(root_, group, "Commit all changes", "what changed, e.g. Read the sensor",
                          "", [this](const std::string& v) { typed_ = v; pending_submit_ = true; },
                          "Enter commits every change (git add -A)    Esc cancels");
    }

    // ---- Clone
    void ask_clone() {
        page_ = Page::Clone;
        clear();
        kit::input_screen(root_, group, "Clone into ~/Code", "https://github.com/you/project.git", "",
                          [this](const std::string& v) { typed_ = v; pending_submit_ = true; });
    }

    void submitted() {
        std::string v = typed_;
        while (!v.empty() && v.front() == ' ') v.erase(v.begin());
        while (!v.empty() && v.back() == ' ') v.pop_back();
        if (v.empty()) return;
        if (page_ == Page::Commit) {
            std::string msg = v;
            open_repo(repo_);
            run("Commit", {"sh", "-c", "git add -A && git commit -q -m \"$1\" && git --no-pager log --stat -n 1 --format='[%h] %s'", "sh", msg},
                sim::commit(msg));
        } else if (page_ == Page::Clone) {
            clone_name_ = repo_from_url(v);
            bool address = v.find('/') != std::string::npos || v.find(':') != std::string::npos;
            if (!address || v.find(' ') != std::string::npos || clone_name_.empty() || clone_name_[0] == '.' ||
                clone_name_[0] == '-') {
                shell_toast("That doesn't look like a repository address");
                return;
            }
            if (kit::exists(dev::projects_dir() + "/" + clone_name_)) {
                shell_toast("~/Code/" + clone_name_ + " is already there");
                return;
            }
            page_ = Page::Cloning;
            clear();
            title_ = what_ = nullptr;
            kit::title_row(root_, "Cloning " + clone_name_, "Esc stop");
            out_.build(root_, 18);
            out_.set_placeholder("Cloning " + v + " ...");
            finished_ = false;
            job_.reset();
            kit::RunOpts o;
            o.cwd = dev::projects_dir();
            o.env = {"GIT_TERMINAL_PROMPT=0", "GIT_SSH_COMMAND=ssh -o BatchMode=yes", "LANG=C"};
            o.sim = sim::clone(clone_name_);
            job_.start({"git", "clone", "--", v, clone_name_}, std::move(o));
        }
    }

    lv_obj_t* root_ = nullptr;
    lv_obj_t* title_ = nullptr;
    lv_obj_t* what_ = nullptr;
    Page page_ = Page::Repos;
    std::vector<std::string> repos_;
    std::string repo_, typed_, clone_name_;
    kit::OutputView out_;
    kit::Job job_;
    int pending_row_ = -1;
    bool pending_submit_ = false;
    bool finished_ = true;
    bool need_auth_ = false, need_name_ = false;
};
}  // namespace

std::unique_ptr<App> make_git_app() { return std::make_unique<GitApp>(); }

}  // namespace zs
