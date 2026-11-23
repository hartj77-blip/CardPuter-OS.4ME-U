#include "ui/quick_panel.h"

#include <algorithm>
#include <cstdio>
#include <ctime>
#include <string>

#include "hal/hal.h"
#include "platform/keys.h"
#include "ui/app.h"
#include "ui/numkeys.h"
#include "ui/power.h"
#include "ui/theme.h"

namespace zs {

using namespace theme;

namespace {

enum Tile { Wifi, Bluetooth, Volume, Brightness, Battery, Clock, Settings, kTiles };
static_assert(kTiles <= kNumberSlots, "one tile per number key 1..7");

constexpr int kStep = 10;
constexpr int kPanelH = 56;
constexpr int kTileW = kScreenW / kTiles;   // 45

lv_obj_t* g_panel = nullptr;
lv_obj_t* g_tiles[kTiles] = {};
lv_obj_t* g_icons[kTiles] = {};
lv_obj_t* g_values[kTiles] = {};
int g_sel = 0;

void select(int t) {
    lv_obj_remove_state(g_tiles[g_sel], LV_STATE_USER_1);
    g_sel = t;
    lv_obj_add_state(g_tiles[g_sel], LV_STATE_USER_1);
}

void open(const char* id) {
    quick_panel_close();
    shell_open_app(id);
}

// Enter, or the tile's number key.
void activate(int t) {
    Hal& h = hal();
    switch (t) {
        case Wifi: h.set_wifi_enabled(!h.net().wifi_enabled); break;
        case Bluetooth: h.set_bt_enabled(!h.net().bt_enabled); break;
        case Volume: h.set_muted(!h.muted()); break;
        case Brightness: break;
        case Battery: open("battery"); return;
        case Clock: open("clock"); return;
        case Settings: open("settings"); return;
        default: return;
    }
    quick_panel_refresh();
}

}  // namespace

int step_volume(int dir) {
    Hal& h = hal();
    int v = std::clamp(h.volume() + dir * kStep, 0, 100);
    h.set_volume(v);
    if (dir > 0 && h.muted()) h.set_muted(false);
    return v;
}

int step_brightness(int dir) {
    // Not below 10%: the screen must stay readable to turn it back up.
    int v = std::clamp(power().user_brightness() + dir * kStep, 10, 100);
    power().set_user_brightness(v);
    return v;
}

void quick_panel_build(lv_obj_t* screen) {
    g_panel = lv_obj_create(screen);
    style_plain(g_panel);
    lv_obj_set_size(g_panel, kScreenW, kPanelH);
    lv_obj_set_pos(g_panel, 0, kStatusH);
    lv_obj_set_style_bg_color(g_panel, c(kSurface), 0);
    lv_obj_set_style_bg_opa(g_panel, LV_OPA_COVER, 0);
    lv_obj_set_style_border_side(g_panel, LV_BORDER_SIDE_BOTTOM, 0);
    lv_obj_set_style_border_color(g_panel, c(kAccent), 0);
    lv_obj_set_style_border_width(g_panel, 1, 0);
    lv_obj_remove_flag(g_panel, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(g_panel, LV_OBJ_FLAG_HIDDEN);

    for (int t = 0; t < kTiles; ++t) {
        lv_obj_t* tile = lv_obj_create(g_panel);
        style_plain(tile);
        lv_obj_set_size(tile, kTileW - 2, kPanelH - 6);
        lv_obj_set_pos(tile, 1 + t * kTileW + (kScreenW - kTiles * kTileW) / 2, 2);
        lv_obj_set_style_radius(tile, 6, 0);
        lv_obj_set_style_bg_color(tile, c(kSurface2), LV_STATE_USER_1);
        lv_obj_set_style_bg_opa(tile, LV_OPA_COVER, LV_STATE_USER_1);
        lv_obj_set_style_border_color(tile, c(kAccent), LV_STATE_USER_1);
        lv_obj_set_style_border_width(tile, 1, LV_STATE_USER_1);
        lv_obj_remove_flag(tile, LV_OBJ_FLAG_SCROLLABLE);
        // The tile's number key, top left.
        char num[2] = {char('1' + t), 0};
        lv_obj_align(label(tile, num, &zs_font_12, kMuted), LV_ALIGN_TOP_LEFT, 3, 1);
        g_icons[t] = label(tile, "", &zs_font_16, kText);
        lv_obj_align(g_icons[t], LV_ALIGN_TOP_MID, 0, 8);
        g_values[t] = label(tile, "", &zs_font_12, kText);
        lv_obj_align(g_values[t], LV_ALIGN_BOTTOM_MID, 0, -2);
        g_tiles[t] = tile;
    }
}

void quick_panel_refresh() {
    if (!quick_panel_is_open()) return;
    Hal& h = hal();
    NetStatus n = h.net();
    BatteryInfo b = h.battery();
    char buf[16];
    auto set = [](int t, const char* icon, const std::string& value, bool on = true) {
        lv_label_set_text(g_icons[t], icon);
        lv_obj_set_style_text_color(g_icons[t], c(on ? kText : kMuted), 0);
        lv_label_set_text(g_values[t], value.c_str());
        lv_obj_align(g_values[t], LV_ALIGN_BOTTOM_MID, 0, -2);
    };
    set(Wifi, LV_SYMBOL_WIFI, n.wifi_enabled ? (n.wifi_connected ? "On" : "No net") : "Off", n.wifi_enabled);
    set(Bluetooth, LV_SYMBOL_BLUETOOTH, n.bt_enabled ? "On" : "Off", n.bt_enabled);
    bool muted = h.muted();
    std::snprintf(buf, sizeof buf, "%d%%", h.volume());
    set(Volume, muted ? LV_SYMBOL_MUTE : LV_SYMBOL_VOLUME_MAX, muted ? "Muted" : buf, !muted);
    std::snprintf(buf, sizeof buf, "%d%%", power().user_brightness());
    set(Brightness, LV_SYMBOL_EYE_OPEN, buf);
    if (b.percent >= 0) std::snprintf(buf, sizeof buf, "%d%%", b.percent);
    else std::snprintf(buf, sizeof buf, "--");
    set(Battery, b.charging ? LV_SYMBOL_CHARGE : LV_SYMBOL_BATTERY_FULL, buf);
    time_t now = time(nullptr);
    tm lt{};
    localtime_r(&now, &lt);
    strftime(buf, sizeof buf, "%H:%M", &lt);
    set(Clock, ZS_ICON_CLOCK, buf);
    set(Settings, LV_SYMBOL_SETTINGS, "More");
}

void quick_panel_open() {
    if (!g_panel) return;
    lv_obj_remove_flag(g_panel, LV_OBJ_FLAG_HIDDEN);
    lv_obj_move_foreground(g_panel);
    select(g_sel);
    quick_panel_refresh();
}

void quick_panel_close() {
    if (g_panel) lv_obj_add_flag(g_panel, LV_OBJ_FLAG_HIDDEN);
}

bool quick_panel_is_open() { return g_panel && !lv_obj_has_flag(g_panel, LV_OBJ_FLAG_HIDDEN); }

bool quick_panel_key(uint32_t key) {
    if (is_shell_key(key)) {
        // Volume and brightness keys: the shell changes them, the tiles follow.
        return false;
    }
    switch (key) {
        case kKeyLeft: select((g_sel + kTiles - 1) % kTiles); break;
        case kKeyRight: select((g_sel + 1) % kTiles); break;
        case kKeyUp:
        case kKeyDown: {
            int dir = key == kKeyUp ? +1 : -1;
            if (g_sel == Volume) step_volume(dir);
            else if (g_sel == Brightness) step_brightness(dir);
            quick_panel_refresh();
            break;
        }
        case kKeyEnter:
        case '\r': activate(g_sel); break;
        case kKeyEsc:
        case '`': quick_panel_close(); break;
        default:
            if (key >= '1' && key < '1' + kTiles) {
                // Volume and brightness just take the arrows; the rest act.
                select(int(key - '1'));
                if (g_sel != Volume && g_sel != Brightness) activate(g_sel);
            }
            break;
    }
    return true;
}

}  // namespace zs
