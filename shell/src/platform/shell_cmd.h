// Building and running shell command lines, in one place.
//
// Anything that comes from outside (a Wi-Fi name, a Bluetooth address, a
// file name, a Sync ID) goes into a command line only through shell_quote().
#pragma once

#include <string>

namespace zs {

// s as one single-quoted shell word: 'it'\''s' for it's. Safe for any bytes.
std::string shell_quote(const std::string& s);

// Runs cmd with /bin/sh, stderr discarded, and returns what it printed ("" if
// it could not start). Blocks until it ends, under a WatchdogGrace, so a slow
// command on the UI thread is a wait, not a hang (platform/watchdog.h).
std::string run_command(const std::string& cmd);

}  // namespace zs
