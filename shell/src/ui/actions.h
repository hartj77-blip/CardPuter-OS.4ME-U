// The open screen's actions (ui/app.h, docs/UX.md): the soft-key bar along
// the bottom of the screen, the menu that Tab opens, and Ctrl + a letter.
#pragma once

#include <cstdint>
#include <functional>
#include <string>
#include <vector>

#include "lvgl.h"
#include "ui/app.h"

namespace zs {

// Build the (hidden) bar on the shell's screen. Once, at start.
void actions_build(lv_obj_t* screen);
// Replace the actions; an empty list hides the bar. `title` names the screen
// in the menu when nothing in focus has a name of its own.
void actions_set(std::vector<Action> actions);
void actions_clear();
bool actions_bar_shown();
// The menu has something to run (Tab would open it).
bool actions_have_menu();

// The menu. `focused` is the widget in focus (its text heads the menu).
void actions_menu_open(lv_obj_t* focused, const std::string& title);
void actions_menu_close();
bool actions_menu_is_open();
// A key while the menu is open: it takes every key.
bool actions_menu_key(uint32_t key);
// The key sheet (0): every key on this screen, then the ones that work
// everywhere. Any key closes it.
void actions_sheet_open(const std::string& title);
void actions_sheet_close();
bool actions_sheet_is_open();

// The confirm box (shell_confirm). While it's open it takes every key.
void actions_confirm(const std::string& question, const std::string& detail, const std::string& yes,
                     std::function<void()> on_yes);
bool actions_confirm_is_open();
bool actions_confirm_key(uint32_t key);
void actions_confirm_close();

// Ctrl + letter: run that action. False if there is none.
bool actions_run_letter(char letter);

}  // namespace zs
