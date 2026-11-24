// What typed text finds, and in which order (Home search and list filters).
// Build and run: shell/tests/run.sh
#include <cstdio>
#include <string>

#include "ui/search_match.h"

using namespace zs;

namespace {
int g_failed = 0;

void expect(bool ok, const char* what) {
    if (!ok) {
        std::printf("FAIL: %s\n", what);
        ++g_failed;
    }
}

int s(const char* name, const char* kw, const char* q) { return search_score(name, kw, q); }
}  // namespace

int main() {
    // Nothing typed finds nothing.
    expect(s("Files", "", "") == 0, "empty query");
    expect(s("Files", "", "   ") == 0, "spaces only");

    // The levels.
    expect(s("Files", "", "files") == 100, "whole name");
    expect(s("Wi-Fi", "", "wifi") == 100, "whole name across a dash");
    expect(s("Wi-Fi", "", "wi-fi") == 100, "whole name with the dash typed");
    expect(s("Bluetooth", "", "blu") == 80, "start of the name");
    expect(s("Battery & power", "", "battery p") == 80, "start of the name, two words");
    expect(s("Wi-Fi networks", "", "net") == 60, "start of a later word");
    expect(s("Dim screen after", "", "after dim") == 60, "word starts, any order");
    expect(s("Bluetooth", "", "tooth") == 40, "inside the name");
    expect(s("Bluetooth", "bt pair", "bt") == 20, "keyword only");
    expect(s("Turn Bluetooth off", "bt", "bt off") == 20, "name and keyword words mixed");

    // Every word has to be found somewhere.
    expect(s("Bluetooth", "bt", "bt zz") == 0, "one word missing");
    expect(s("Files", "", "notes") == 0, "no match");
    expect(s("", "", "a") == 0, "empty name");

    // Capitals and punctuation don't matter.
    expect(s("NOTES", "", "notes") == 100, "capitals");
    expect(s("Remote & sync", "", "remote sync") == 100, "ampersand");
    expect(s("Remote & sync", "", "remote s") == 80, "ampersand, start");

    // Ranking: a name that starts with the text beats one that only contains it.
    expect(s("Settings", "", "set") > s("Reset network", "", "set"), "prefix beats a word start");
    expect(s("Reset network", "", "set") > 0, "contained still found");
    expect(s("Sound", "", "so") > s("Sound", "", "un"), "prefix beats inside");

    expect(search_norm("Wi-Fi Net") == "wifi net", "norm");

    if (g_failed) {
        std::printf("%d failed\n", g_failed);
        return 1;
    }
    std::printf("search_match_test: all passed\n");
    return 0;
}
