// The photograph behind home (docs/UX.md, "Look: Monochrome"). Built in;
// shell/tools/wallpaper.py makes it.
#pragma once

#include "lvgl.h"

namespace zs {

// 320 x 150 RGB565, the area under the status bar, with its scrim baked in.
const lv_image_dsc_t* wallpaper_home();

}  // namespace zs
