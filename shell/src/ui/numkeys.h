// Number keys as buttons (docs/UX.md). Away from a text box:
//   1 - 9  pick the 1st to 9th item on screen, as if moved to it and Enter pressed
//   0      the key sheet on a screen with actions (ui/actions.h); elsewhere,
//          show which number is which (a badge on each item, for 3 seconds)
// Home is hold Esc and the taskbar panel Ctrl + Space. The open app sees each
// key first (Launcher uses 1 - 7 for its dock). Lists (ui/list_view.h) show
// their numbers all the time.
#pragma once

#include <vector>

#include "lvgl.h"

namespace zs {

constexpr int kNumberSlots = 9;   // keys 1..9

// What 1..9 pick in a focus group: the items on screen, in focus order,
// leaving out text boxes (typing has its own keys) and disabled items.
std::vector<lv_obj_t*> number_targets(lv_group_t* group);

// A small numbered badge for key `digit`, on the top layer at x, y.
lv_obj_t* number_badge(int digit, int x, int y);

// Badges over the targets; hide them again with number_hints_hide().
// Returns how many it showed.
int number_hints_show(const std::vector<lv_obj_t*>& targets);
void number_hints_hide();
bool number_hints_shown();
// Hides the badges once they have been up 3 seconds (the shell's timer).
void number_hints_tick();

}  // namespace zs
