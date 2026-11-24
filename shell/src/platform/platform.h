#pragma once

#include <string>

#include "lvgl.h"

namespace zs {

// Create the display and the keyboard input device for this backend.
lv_indev_t* platform_init(int w, int h, int zoom);

// Main loop. Returns the process exit code.
int platform_run();

// Screen off: the device backend stops polling the keyboard on a timer and
// sleeps until a key arrives. No-op elsewhere.
void platform_set_low_power(bool on);

// Terminal handoff: let the kernel switch away from our console. A console in
// graphics mode with automatic VT switching refuses every switch (chvt waits
// forever), so it goes back to text mode while the Terminal has the screen.
// No-op except on the device.
void platform_console_handoff(bool away);
// Ctrl was tapped and waits for the next key (status bar shows it).
bool platform_ctrl_pending();

// Call fn(arg) when fd has something to read, from the main loop (the UI
// thread). On the device the loop's poll() waits on fd along with the
// keyboards, so an idle fd costs nothing. In the other builds fn is called
// once a second, so fd must be non-blocking. Used for inotify (ui/app_watch.cpp).
void platform_watch_fd(int fd, void (*fn)(void*), void* arg);

// Headless only: run a scripted session (see main.cpp). Returns exit code.
int platform_run_script(const std::string& script);

}  // namespace zs
