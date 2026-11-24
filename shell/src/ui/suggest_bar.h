// Word suggestions in every text box (docs/KEYS.md): while a word is being
// typed, up to three ways to finish it show over the left of the status bar,
// and Tab types the first one. The words come from ui/suggest.h.
//
// It works on whatever text box has the focus, in ZeroOS's own screens and in
// installed apps alike, so no app needs changing. Password boxes are left
// alone: nothing is offered and nothing typed there is learned.
#pragma once

#include <cstdint>

#include "lvgl.h"

namespace zs {

// A text box for something that is no prose and should not be remembered (a
// code, a serial number) sets this flag to opt out:
//   lv_obj_add_flag(ta, kNoSuggestFlag);
// A plain LVGL flag, so an installed app needs nothing from ZeroShell for it.
constexpr lv_obj_flag_t kNoSuggestFlag = LV_OBJ_FLAG_USER_4;

void suggest_build(lv_obj_t* screen);   // once, after the status bar
// Every key press, before anything else uses it. `textbox` is the text box in
// focus, or nullptr. True if the key was used here (Tab took a suggestion).
bool suggest_key(uint32_t key, lv_obj_t* textbox);
void suggest_tick();                    // ~10 Hz: saves learned words, at most twice a minute
void suggest_flush();                   // save them now (before power off)

bool suggest_enabled();
void suggest_set_enabled(bool on);
void suggest_forget();                  // forget the learned words

}  // namespace zs
