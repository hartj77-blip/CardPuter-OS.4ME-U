// The photographs behind home and the lock screen (docs/UX.md, "Look:
// Monochrome"). Built in; shell/tools/wallpaper.py makes them.
#pragma once

#include "lvgl.h"

namespace zs {

// 320 x 170 RGB565, the whole screen, each with its scrim baked in.
const lv_image_dsc_t* wallpaper_home();
const lv_image_dsc_t* wallpaper_lock();

}  // namespace zs
