// Screenshots: the PRSC key (Fn + J), or PrintScreen on a USB keyboard.
#pragma once

#include <string>

namespace zs {

// Saves what is on the LCD now as a PNG in ~/Pictures/Screenshots, named
// after the date and time. On the device the picture comes from the LCD's
// framebuffer, so it also catches the Terminal and M5Stack apps. Returns the
// file's path, or "" if it couldn't be saved.
std::string screenshot_save();

}  // namespace zs
