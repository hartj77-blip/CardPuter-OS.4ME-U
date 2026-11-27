// The sections of Settings that have a file of their own: Wi-Fi
// (apps/wifi.cpp), Bluetooth (apps/bluetooth.cpp) and Date & time
// (apps/datetime.cpp). They are not apps:
// Settings builds one in its own screen, hands it keys and ticks, and takes
// the screen back when the section's on_back() has nowhere further to go.
#pragma once

#include <memory>
#include <string>

#include "ui/app.h"

namespace zs {

// `back` is what Esc is called in the soft-key bar: "Settings", or "Back"
// when Settings was opened straight at the section (from search, or Tether).
std::unique_ptr<App> make_wifi_section(const char* back);
std::unique_ptr<App> make_bluetooth_section(const char* back);
std::unique_ptr<App> make_datetime_section(const char* back);

// The line over a section's list: "Settings > Wi-Fi" and, on the right, what
// the section has to say for itself ("Connected to HomeNet"), in `color`.
// Returns that label on the right (nullptr without a `state`), for a section
// that keeps it up to date. The list goes under the line:
// list_.build(root, 2, 18, kScreenW - 4, kBodyH - 20).
lv_obj_t* settings_heading(lv_obj_t* root, const char* section, const std::string& state = "", uint32_t color = 0);

}  // namespace zs
