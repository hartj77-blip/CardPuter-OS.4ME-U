// Shared by the Dev Tools apps (docs/DEV.md): where projects live, how to run
// one, and which editor to use.
#pragma once

#include <string>
#include <vector>

namespace zs::dev {

// ~/Code: every project is a folder in here.
std::string projects_dir();

// "Python", "C", "Rust"... from a project's files; "" if it can't tell.
std::string project_kind(const std::string& dir);

// How R in Code runs a project.
struct RunPlan {
    std::string command;   // shell command, run in the project folder ("" = none)
    std::string tool;      // program it needs, to check it is installed
    std::string package;   // Debian package that has it
};
RunPlan run_plan(const std::string& dir);

// The terminal text editor to open files with: micro, else nano, else vi.
std::string editor();

// Git repositories under ~/Code (folders with a .git), as full paths.
std::vector<std::string> git_repos();

}  // namespace zs::dev
