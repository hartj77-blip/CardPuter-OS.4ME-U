// The taskbar panel: press 2 (away from a text box), or Ctrl + Space anywhere, and a row of tiles drops
// from the status bar. Each tile has a number key:
//   3 Wi-Fi  4 Bluetooth  5 Volume  6 Brightness  7 Battery  8 Clock  9 Settings
// Left/Right move between tiles, Up/Down change volume or brightness, Enter
// (or the tile's number) switches Wi-Fi/Bluetooth/mute or opens the app.
// Esc, 2 or Ctrl + Space closes it.
#pragma once

#include <cstdint>

#include "lvgl.h"

namespace zs {

// Build it (hidden) on the shell's screen. Once, at start.
void quick_panel_build(lv_obj_t* screen);
void quick_panel_open();
void quick_panel_close();
bool quick_panel_is_open();
// A key while it's open. It takes every key but the shell's own ones
// (volume, brightness, media: false, so the shell handles them).
bool quick_panel_key(uint32_t key);
// Redraw the values (the shell's timer, about once a second).
void quick_panel_refresh();

// Volume and brightness up or down a step, as the Fn keys and the panel do.
// Returns the new level, 0..100.
int step_volume(int dir);
int step_brightness(int dir);

}  // namespace zs
