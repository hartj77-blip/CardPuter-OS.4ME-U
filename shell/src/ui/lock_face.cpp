#include "ui/lock_face.h"

#include <ctime>
#include <string>

#include "hal/hal.h"
#include "ui/player.h"
#include "ui/theme.h"
#include "ui/wallpaper.h"

namespace zs {

using namespace theme;

namespace {
lv_obj_t* g_face = nullptr;
lv_obj_t* g_time = nullptr;
lv_obj_t* g_date = nullptr;
lv_obj_t* g_playing = nullptr;   // the track playing or paused, under the date
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
    g_playing = label(g_face, "", &zs_font_14, kText);
    lv_obj_set_size(g_playing, kScreenW - 30, 18);
    lv_label_set_long_mode(g_playing, LV_LABEL_LONG_DOT);
    lv_obj_set_pos(g_playing, 15, 84);
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
    PlaybackState pb = hal().playback();
    std::string playing;
    if (!pb.path.empty()) {
        const QueueItem* q = player_current();
        playing = std::string(pb.paused ? LV_SYMBOL_PAUSE "  " : LV_SYMBOL_PLAY "  ") +
                  (q ? q->title : pb.path.substr(pb.path.rfind('/') + 1));
        if (q && !q->artist.empty()) playing += "  \xC2\xB7  " + q->artist;
    }
    if (playing != lv_label_get_text(g_playing)) lv_label_set_text(g_playing, playing.c_str());
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
