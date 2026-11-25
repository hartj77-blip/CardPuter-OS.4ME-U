// Checks how ZeroShell reads a phone's Bluetooth music player (hal/bt_player.h).
// Build and run: shell/tests/run.sh
#include <cstdio>
#include <string>

#include "hal/bt_player.h"

using namespace zs::btplayer;

namespace {
int g_failed = 0;

void check(const std::string& got, const std::string& want, const char* what) {
    if (got == want) return;
    std::printf("FAIL %s\n  got:  %s\n  want: %s\n", what, got.c_str(), want.c_str());
    ++g_failed;
}
}  // namespace

int main() {
    // busctl --list tree org.bluez, a phone and a car connected
    std::string tree =
        "/\n/org\n/org/bluez\n/org/bluez/hci0\n"
        "/org/bluez/hci0/dev_11_22_33_44_55_66\n"
        "/org/bluez/hci0/dev_11_22_33_44_55_66/fd0\n"
        "/org/bluez/hci0/dev_AA_BB_CC_DD_EE_FF\n"
        "/org/bluez/hci0/dev_AA_BB_CC_DD_EE_FF/player0\n"
        "/org/bluez/hci0/dev_AA_BB_CC_DD_EE_FF/player0/Filesystem\n"
        "/org/bluez/hci0/dev_AA_BB_CC_DD_EE_FF/sep1\n"
        "  /org/bluez/hci0/dev_AA_BB_CC_DD_EE_FF/player12  \n"
        "/org/bluez/hci0/dev_AA_BB_CC_DD_EE_FF/player0;rm -rf /\n"
        "/org/bluez/hci0/dev_AA_BB/player0\n";
    auto p = player_paths(tree);
    check(std::to_string(p.size()), "2", "two players");
    if (p.size() == 2) {
        check(p[0], "/org/bluez/hci0/dev_AA_BB_CC_DD_EE_FF/player0", "first player");
        check(p[1], "/org/bluez/hci0/dev_AA_BB_CC_DD_EE_FF/player12", "trimmed");
    }
    check(address_of("/org/bluez/hci0/dev_AA_BB_CC_DD_EE_FF/player0"), "AA:BB:CC:DD:EE:FF", "address");
    check(address_of("/org/bluez/hci0"), "", "no address");

    std::string phone =
        "Device AA:BB:CC:DD:EE:FF (public)\n\tName: Pixel 8\n\tIcon: phone\n"
        "\tUUID: Audio Source              (0000110a-0000-1000-8000-00805f9b34fb)\n"
        "\tUUID: A/V Remote Control Target (0000110c-0000-1000-8000-00805f9b34fb)\n";
    std::string car =
        "Device 11:22:33:44:55:66 (public)\n\tName: CAR MULTIMEDIA\n\tIcon: audio-card\n"
        "\tUUID: Audio Sink                (0000110b-0000-1000-8000-00805f9b34fb)\n"
        "\tUUID: A/V Remote Control Target (0000110c-0000-1000-8000-00805f9b34fb)\n";
    check(is_music_source(phone) ? "yes" : "no", "yes", "a phone sends music");
    check(is_music_source(car) ? "yes" : "no", "no", "a car doesn't");
    check(is_music_source("\tIcon: phone\n") ? "yes" : "no", "yes", "a phone before its services are known");

    auto w = words(R"(a{sv} 2 "Title" s "Caf\xc3\xa9 \"Live\"" "Artist" s "A\\B\303\251")");
    check(std::to_string(w.size()), "8", "words");
    if (w.size() == 8) {
        check(w[4], "Caf\xc3\xa9 \"Live\"", "hex escapes and quotes");
        check(w[7], "A\\B\xc3\xa9", "backslash and octal");
    }

    Info i = parse_info(
        "s \"playing\"\n"
        "a{sv} 5 \"Title\" s \"Song Two\" \"TrackNumber\" u 2 \"Artist\" s \"The Band\" \"Album\" s \"LP\" "
        "\"Duration\" u 215000\n");
    check(i.status, "playing", "status");
    check(i.title, "Song Two", "title");
    check(i.artist, "The Band", "artist");
    check(i.playing() ? "yes" : "no", "yes", "playing");
    Info paused = parse_info("s \"paused\"\na{sv} 0\n");
    check(paused.playing() ? "yes" : "no", "no", "paused");
    check(paused.title, "", "no track");
    check(parse_info("").status, "", "busctl failed");

    check(method_for(Key::PlayPause, true), "Pause", "play/pause while playing");
    check(method_for(Key::PlayPause, false), "Play", "play/pause while paused");
    check(method_for(Key::Stop, true), "Pause", "stop pauses");
    check(method_for(Key::Next, false), "Next", "next");
    check(method_for(Key::Previous, false), "Previous", "previous");

    if (g_failed) return 1;
    std::printf("bt_player_test: ok\n");
    return 0;
}
