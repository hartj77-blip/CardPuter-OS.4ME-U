#include "ui/lock_face.h"

#include <ctime>

#include "ui/theme.h"
#include "ui/wallpaper.h"

namespace zs {

using namespace theme;

namespace {
lv_obj_t* g_face = nullptr;
lv_obj_t* g_time = nullptr;
lv_obj_t* g_date = nullptr;
}  // namespace

void lock_face_build() {
    g_face = lv_obj_create(lv_layer_top());
    style_plain(g_face);
    lv_obj_set_size(g_face, kScreenW, kScreenH);
    lv_obj_set_style_bg_color(g_face, c(kBg), 0);
    lv_obj_set_style_bg_opa(g_face, LV_OPA_COVER, 0);
    lv_obj_set_style_bg_image_src(g_face, wallpaper_lock(), 0);
    lv_obj_remove_flag(g_face, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_remove_flag(g_face, LV_OBJ_FLAG_CLICKABLE);

    g_time = label(g_face, "--:--", &zs_font_28, kText);
    lv_obj_set_pos(g_time, 14, 24);
    g_date = label(g_face, "", &zs_font_12, kGrey7);
    lv_obj_set_style_text_letter_space(g_date, 1, 0);
    lv_obj_set_pos(g_date, 15, 60);
    lv_obj_t* hint = label(g_face, "Any key unlocks", &zs_font_12, kMuted);
    lv_obj_align(hint, LV_ALIGN_BOTTOM_LEFT, 15, -8);
    lv_obj_add_flag(g_face, LV_OBJ_FLAG_HIDDEN);
}

void lock_face_tick() {
    if (!lock_face_shown()) return;
    time_t now = time(nullptr);
    tm lt{};
    localtime_r(&now, &lt);
    char buf[48];
    strftime(buf, sizeof buf, "%H:%M", &lt);
    lv_label_set_text(g_time, buf);
    strftime(buf, sizeof buf, "%A %-d %B", &lt);
    lv_label_set_text(g_date, buf);
}

void lock_face_show() {
    if (!g_face) return;
    lv_obj_remove_flag(g_face, LV_OBJ_FLAG_HIDDEN);
    lock_face_tick();
}

void lock_face_hide() {
    if (g_face) lv_obj_add_flag(g_face, LV_OBJ_FLAG_HIDDEN);
}

bool lock_face_shown() { return g_face && !lv_obj_has_flag(g_face, LV_OBJ_FLAG_HIDDEN); }

}  // namespace zs
