// Tests for reading the launcher entries of apps made for M5Stack's launcher
// (ui/desktop_entry.h; no LVGL): cmake ... && ctest, or run desktop_entry_test.
#include <cstdio>
#include <string>

#include "ui/desktop_entry.h"

using namespace zs;

static int fails = 0;
#define CHECK(cond)                                                       \
    do {                                                                  \
        if (!(cond)) {                                                    \
            std::printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond);   \
            ++fails;                                                      \
        }                                                                 \
    } while (0)

int main() {
    // As M5's store packages write them (Ghost Hunter, Battery Saver).
    auto e = parse_desktop_entry(
        "[Desktop Entry]\nName=Ghost Hunter\nExec=/usr/share/APPLaunch/bin/ghost-hunter\nTerminal=false\n"
        "Sysplause=true\nIcon=share/images/ghost-hunter.png\nType=Application\n");
    CHECK(e && e->name == "Ghost Hunter" && e->exec == "/usr/share/APPLaunch/bin/ghost-hunter");
    CHECK(e && !e->terminal && e->sysplause && e->icon == "share/images/ghost-hunter.png");

    e = parse_desktop_entry("# made by hand\r\n[Desktop Entry]\r\nType=Application\r\nName = Tool \r\n"
                            "Exec=/usr/bin/python3 /usr/share/APPLaunch/apps/t/app.py %f %%x\r\nTerminal=true\r\n"
                            "Sysplause=false\r\n");
    CHECK(e && e->name == "Tool" && e->exec == "/usr/bin/python3 /usr/share/APPLaunch/apps/t/app.py  %x");
    CHECK(e && e->terminal && !e->sysplause);

    // Only the [Desktop Entry] group counts.
    e = parse_desktop_entry("[Desktop Action x]\nName=Other\nExec=other\n[Desktop Entry]\nName=Mine\nExec=mine\n");
    CHECK(e && e->name == "Mine" && e->exec == "mine");

    // Not apps.
    CHECK(!parse_desktop_entry("[Desktop Entry]\nName=Hidden\nExec=x\nNoDisplay=true\n"));
    CHECK(!parse_desktop_entry("[Desktop Entry]\nName=Hidden\nExec=x\nHidden=true\n"));
    CHECK(!parse_desktop_entry("[Desktop Entry]\nName=Link\nExec=x\nType=Link\n"));
    CHECK(!parse_desktop_entry("[Desktop Entry]\nName=No command\n"));
    CHECK(!parse_desktop_entry("[Desktop Entry]\nExec=no-name\n"));
    CHECK(!parse_desktop_entry("Name=Outside\nExec=x\n"));
    CHECK(!parse_desktop_entry(std::string("[Desktop Entry]\nName=Bad\x01name\nExec=x\n")));
    CHECK(!parse_desktop_entry(""));

    CHECK(desktop_app_id("zero-weather") == "m5-zero-weather");
    CHECK(desktop_app_id("Camera_App.v2") == "m5-camera-app-v2");

    if (fails) {
        std::printf("%d failed\n", fails);
        return 1;
    }
    std::printf("all passed\n");
    return 0;
}
