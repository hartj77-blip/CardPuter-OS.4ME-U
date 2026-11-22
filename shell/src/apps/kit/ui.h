// UI pieces shared by the app packs (Dev Tools, Games): a one-line "type
// something" screen, a scrolling text panel for streamed command output, a
// title row with key hints, and running a command in the Terminal app.
#pragma once

#include <functional>
#include <string>

#include "lvgl.h"

namespace zs::kit {

// A full-screen one-line prompt. on_submit(text) is called from the
// textarea's Enter; keep it light (set a flag, rebuild on the next tick) so no
// widget is deleted inside its own event. Returns the page object.
lv_obj_t* input_screen(lv_obj_t* root, lv_group_t* group, const char* title, const char* placeholder,
                       const std::string& initial, std::function<void(const std::string&)> on_submit,
                       const char* help = "Enter to continue     Esc to go back");

// The title (accent, left) and key hints (muted, right) on the top row of an
// app's page. Returns the title label.
lv_obj_t* title_row(lv_obj_t* root, const std::string& title, const std::string& hints);

// A scrolling text view that fills the app area below `top` (down to
// `bottom_gap` above the bottom). append() adds a line, auto-scrolls to the
// bottom and caps the backlog.
class OutputView {
public:
    void build(lv_obj_t* root, int top, int bottom_gap = 2);
    void append(const std::string& line);
    void clear();
    void set_placeholder(const std::string& text);
    bool empty() const { return lines_ == 0; }

private:
    void render();
    lv_obj_t* panel_ = nullptr;
    lv_obj_t* label_ = nullptr;
    lv_obj_t* placeholder_ = nullptr;
    std::string text_;
    int lines_ = 0;
};

// Run a shell command in the Terminal app (tmux on the built-in screen), in
// `cwd` if given. False (and a toast) if the Terminal app isn't installed.
bool terminal_run(const std::string& command, const std::string& cwd = "");

}  // namespace zs::kit
