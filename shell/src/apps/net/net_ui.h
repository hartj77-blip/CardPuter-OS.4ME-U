// Little UI pieces shared by the Net Tools apps: a one-line "type a target"
// screen, and a scrolling text panel the streamed command output goes into.
#pragma once

#include <functional>
#include <string>

#include "lvgl.h"

namespace zs::net {

// A full-screen "type a host" prompt. on_submit(text) is called from the
// textarea's Enter; keep it light (set a flag, rebuild on the next tick) so no
// widget is deleted inside its own event. Returns the page object.
lv_obj_t* input_screen(lv_obj_t* root, lv_group_t* group, const char* title, const char* placeholder,
                       const std::string& initial, std::function<void(const std::string&)> on_submit);

// A scrolling, fixed-width-ish text view that fills the app area below `top`.
// append() adds a line; it auto-scrolls to the bottom and caps its backlog.
class OutputView {
public:
    void build(lv_obj_t* root, int top);
    void append(const std::string& line);
    void clear();
    void set_placeholder(const std::string& text);

private:
    void render();
    lv_obj_t* panel_ = nullptr;
    lv_obj_t* label_ = nullptr;
    lv_obj_t* placeholder_ = nullptr;
    std::string text_;
    int lines_ = 0;
};

}  // namespace zs::net
