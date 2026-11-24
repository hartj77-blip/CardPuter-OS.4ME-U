// Look and feel: "Monochrome" (docs/UX.md, UI 3). Black, white and greys;
// the item in focus is lit white with black text; menus and dialogs are
// lifted sheets with a soft grey gradient. Colour only marks state (green,
// orange, blue, red) or belongs to content (app icons, album art). Square
// corners on system chrome. The Ubuntu typeface, for now.
#pragma once

#include "lvgl.h"

// Ubuntu typeface with LVGL's symbols; see tools/fonts.sh.
LV_FONT_DECLARE(zs_font_12)   // 12 and 16 also carry the ZS_ICON_* app icons
LV_FONT_DECLARE(zs_font_14)
LV_FONT_DECLARE(zs_font_16)
LV_FONT_DECLARE(zs_font_20)
LV_FONT_DECLARE(zs_font_28)

// App icons (Font Awesome), in zs_font_12 and zs_font_16.
#define ZS_ICON_TERMINAL   "\xEF\x84\xA0"   // U+F120
#define ZS_ICON_COMPASS    "\xEF\x85\x8E"   // U+F14E
#define ZS_ICON_CLOCK      "\xEF\x80\x97"   // U+F017
#define ZS_ICON_DESKTOP    "\xEF\x84\x88"   // U+F108
#define ZS_ICON_CHAT       "\xEF\x82\x86"   // U+F086
#define ZS_ICON_CHIP       "\xEF\x8B\x9B"   // U+F2DB
#define ZS_ICON_NOTE       "\xEF\x89\x89"   // U+F249
#define ZS_ICON_GRID       "\xEF\x80\x8A"   // U+F00A
#define ZS_ICON_SATELLITE  "\xEF\x9F\x80"   // U+F7C0
#define ZS_ICON_NETWORK    "\xEF\x9B\xBF"   // U+F6FF
#define ZS_ICON_SYNC       "\xEF\x8B\xB1"   // U+F2F1
#define ZS_ICON_GLOBE      "\xEF\x82\xAC"   // U+F0AC
#define ZS_ICON_SEARCH     "\xEF\x80\x82"   // U+F002
#define ZS_ICON_PIN        "\xEF\x8F\x85"   // U+F3C5
#define ZS_ICON_TOWER      "\xEF\x94\x99"   // U+F519
#define ZS_ICON_SLIDERS    "\xEF\x87\x9E"   // U+F1DE
#define ZS_ICON_GAUGE      "\xEF\x8F\xBD"   // U+F3FD
#define ZS_ICON_LAPTOP     "\xEF\x84\x89"   // U+F109
#define ZS_ICON_CAMERA     "\xEF\x80\xB0"   // U+F030
#define ZS_ICON_CAR        "\xEF\x86\xB9"   // U+F1B9
#define ZS_ICON_PHONE      "\xEF\x8F\x8D"   // U+F3CD

namespace zs::theme {

constexpr int kScreenW = 320;
constexpr int kScreenH = 170;
constexpr int kStatusH = 20;
constexpr int kContentH = kScreenH - kStatusH;
constexpr int kSoftkeyH = 14;                    // soft-key bar (docs/UX.md)
constexpr int kBodyH = kContentH - kSoftkeyH;    // an app area with the bar

// The grey ramp. Every grey in the interface is one of these.
constexpr uint32_t kGrey0 = 0x000000;   // the screen
constexpr uint32_t kGrey1 = 0x0d0d0d;   // sheets, the soft-key bar
constexpr uint32_t kGrey2 = 0x171717;   // rows, cards
constexpr uint32_t kGrey3 = 0x242424;   // dividers, raised controls
constexpr uint32_t kGrey4 = 0x3a3a3a;   // borders
constexpr uint32_t kGrey5 = 0x6b6b6b;   // disabled
constexpr uint32_t kGrey6 = 0xa3a3a3;   // secondary text
constexpr uint32_t kGrey7 = 0xd4d4d4;   // the bottom of the focus gradient
constexpr uint32_t kGrey8 = 0xffffff;   // text, focus

// Roles. Use these, not the ramp, in screens.
constexpr uint32_t kBg = kGrey0;          // window background
constexpr uint32_t kSurface = kGrey2;     // cards, list rows
constexpr uint32_t kSurface2 = kGrey3;    // raised: buttons, troughs, badges
constexpr uint32_t kBorder = kGrey4;
constexpr uint32_t kHairline = kGrey3;
constexpr uint32_t kText = kGrey8;
constexpr uint32_t kMuted = kGrey6;
constexpr uint32_t kDisabled = kGrey5;
constexpr uint32_t kFocus = kGrey8;       // the lit row: top of its gradient
constexpr uint32_t kFocusLow = kGrey7;    // ... and its bottom
constexpr uint32_t kOnFocus = kGrey0;     // text on the lit row
constexpr uint32_t kRaise = 0x232323;     // menus, dialogs: top of their gradient
constexpr uint32_t kRaiseLow = 0x0a0a0a;  // ... and its bottom

// State. Only for what something is (on, needs an update, failed), never decoration.
constexpr uint32_t kGood = 0x3fb950;   // on, connected, installed
constexpr uint32_t kWarn = 0xf0a020;   // update ready, low battery
constexpr uint32_t kBad = 0xe5484d;    // destructive, error
constexpr uint32_t kInfo = 0x3b9eff;   // in progress, searching

// UI 2 names, kept so screens and installable apps keep building. The accent
// is white now: it marks focus, cursors and highlights, and text on it is black.
constexpr uint32_t kAccent = kFocus;
constexpr uint32_t kOnAccent = kOnFocus;
constexpr uint32_t kTopBar = kGrey0;
constexpr uint32_t kAubergine = kGrey0;        // home wallpaper until photos land
constexpr uint32_t kAubergineLight = kGrey3;
constexpr uint32_t kTerminalBg = kGrey0;

inline lv_color_t c(uint32_t hex) { return lv_color_hex(hex); }

// Common styling helpers.
void style_panel(lv_obj_t* o);            // rounded surface card
void style_plain(lv_obj_t* o);            // transparent, no border/padding
lv_obj_t* label(lv_obj_t* parent, const char* text, const lv_font_t* font = &zs_font_12,
                uint32_t color = kText);
void style_focus_list(lv_obj_t* list);    // list buttons with the lit focus
// The lit white gradient of the item in focus, on `selector` (a state such as
// LV_STATE_FOCUSED). Text colour is the caller's: kOnFocus.
void style_focus_fill(lv_obj_t* o, lv_style_selector_t selector);
// A lifted sheet: menus, dialogs, pop-ups. Grey gradient, 1 px border with a
// light top edge, a drop shadow. Square.
void style_raised(lv_obj_t* o);

}  // namespace zs::theme
