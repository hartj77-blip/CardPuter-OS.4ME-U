// New apps on the home screen without a restart, however they were installed
// (docs/APPS.md, "How the home screen finds new apps").
//
// The Apps screen reloads the app list itself after its own installs. An
// install from the command line (sudo zeroos-apps install x, apt, dpkg -i)
// goes around it, so ZeroShell also watches the folders the app list is
// read from, with inotify:
//
//   /usr/lib/zeroos/apps                 app modules (<id>.so)
//   /usr/share/APPLaunch/applications    M5Stack launcher entries (.desktop)
//   /var/lib/zeroos/apps/m5              zeroos-apps' notes on M5 apps
//
// A change in any of them waits for things to settle (1.5 s with no further
// change, as one apt run touches files many times), then:
//   - on the home screen: the app list is read again and home is redrawn,
//     keeping the search or All apps page that was open;
//   - in any other app: nothing yet. The list is read again on the way back
//     home, since an open app may hold entries from the old list.
#pragma once

#include <string>
#include <vector>

namespace zs {

// Once, after the shell is up. Does nothing if inotify isn't there.
void app_watch_start();
// Going home: true if the app list changed on disk since it was last read
// (and forgets it). The shell then reads it again before drawing home.
bool app_watch_take_changed();

// apps/registry.cpp: the folders the app list is read from.
std::vector<std::string> app_registry_dirs();

}  // namespace zs
