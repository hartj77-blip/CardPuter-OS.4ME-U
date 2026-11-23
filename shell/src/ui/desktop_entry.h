// Launcher entries of apps made for M5Stack's launcher (APPLaunch): the
// .desktop files in /usr/share/APPLaunch/applications (docs/APPS.md,
// "M5Stack's app store"). Read the way APPLaunch reads them: the
// [Desktop Entry] group's Name, Exec, Icon, Terminal and Sysplause; an entry
// with Hidden or NoDisplay set, another Type, or no Name or Exec is no app.
#pragma once

#include <optional>
#include <string>

namespace zs {

struct DesktopEntry {
    std::string name;
    std::string exec;          // the command, field codes (%f, %U...) taken out
    std::string icon;          // relative to /usr/share/APPLaunch, or absolute
    bool terminal = false;     // runs in a terminal
    bool sysplause = true;     // terminal: wait for a key after it ends
};

std::optional<DesktopEntry> parse_desktop_entry(const std::string& contents);

// "m5-<x>" for an entry: lower case letters, digits and "-" only.
std::string desktop_app_id(const std::string& base);

}  // namespace zs
