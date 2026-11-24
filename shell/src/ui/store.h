// The Apps store's view of the world (docs/APPS.md): which apps can be
// installed, which are, and how an install is going.
//
// On the device, zeroos-apps does the work as root (started through
// zeroos-apps@<action>.service) and writes:
//   /var/lib/zeroos/apps/catalog   every app on offer, from every source
//   /run/zeroos/apps.status        how the current job is going
// Installed apps are Debian packages called zeroos-app-<id>, or, for apps
// from M5Stack's app store (id m5-<x>), the package that
// /var/lib/zeroos/apps/m5/m5-<x> names.
// In the simulator a made-up catalog stands in, and jobs pretend to run.
#pragma once

#include <string>
#include <vector>

namespace zs::store {

struct Entry {
    std::string id, name, summary;
    std::string version;     // on offer ("" if no source offers it any more)
    std::string installed;   // installed version, "" if not installed
    std::string source;      // where it comes from (owner/repo or a URL)
    std::string needs;       // Debian packages it brings along, for the details page
    std::string warn;        // M5Stack's store: what to know first ("Runs in the background")
    bool m5 = false;         // from M5Stack's app store (made for M5's launcher)
    long size_kb = 0;        // download
    bool restore = false;    // taken off by a ZeroOS update (new app ABI), not put back yet
    bool update() const;     // installed, and a newer version is on offer
    bool to_restore() const { return restore && installed.empty() && !version.empty(); }
};

struct Status {
    enum State { Idle, Refreshing, Downloading, Installing, Removing, Done, Failed, Offline };
    State state = Idle;
    std::string action;      // refresh, install, remove, upgrade, add-source, remove-source
    std::string app;         // the app it's about, if any
    int percent = 0;
    std::string message;
    bool busy() const { return state == Refreshing || state == Downloading || state == Installing || state == Removing; }
};

// Choose the simulated store (the simulator, or the device with --mock).
void set_mock(bool mock);

// Read the catalog and what's installed (a fork for dpkg-query: call on
// opening a page, not every tick). Sorted by name.
std::vector<Entry> catalog();
// Seconds since the catalog was last refreshed, or -1 if never.
long catalog_age();
Status status();
// Where apps come from: "owner/repo" for GitHub, or an https:// address.
std::vector<std::string> sources();

// These start a job and return at once; watch status().
void refresh();
void install(const std::string& id);   // also upgrades
void remove(const std::string& id);
void upgrade_all();
// Adding a source decides what may run as root, so it takes the user's
// password: the job is started through sudo, which reads the password from a
// pipe (never the command line). Started, or the password was refused.
enum class AddResult { Started, Refused };
AddResult add_source(const std::string& source, const std::string& password);
void remove_source(const std::string& source);
// Move a source one place up (-1) or down (+1). Higher sources win when two
// offer the same app.
void move_source(const std::string& source, int dir);

// How to show a source: "M5Stack's app store" for M5's, else as it is.
std::string source_label(const std::string& source);

// "owner/repo", "github.com/owner/repo" or https://... : the form
// add_source() takes, or "" if it doesn't look like a source.
std::string normalise_source(const std::string& text);

}  // namespace zs::store
