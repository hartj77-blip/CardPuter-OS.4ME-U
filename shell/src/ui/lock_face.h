// The lock screen (docs/UX.md, "Look: Monochrome"): after hold Tab, the key
// that wakes the screen shows the photograph with a large clock; the next key
// puts it away and goes back to what was open. The idle timeout doesn't lock:
// it wakes straight to the app, as before.
#pragma once

#include "lvgl.h"

namespace zs {

void lock_face_build();   // once, on the top layer, under the backlight stand-in
void lock_face_show();    // hold Tab locked the screen
void lock_face_hide();
bool lock_face_shown();
void lock_face_tick();    // keep the clock current while it's up

}  // namespace zs
