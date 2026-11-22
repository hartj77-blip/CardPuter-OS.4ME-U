#include "ui/numkeys.h"

#include <string>

#include "ui/theme.h"

namespace zs {

using namespace theme;

namespace {
std::vector<lv_obj_t*> g_badges;
uint32_t g_shown_at = 0;
constexpr uint32_t kHintMs = 3000;
}  // namespace

std::vector<lv_obj_t*> number_targets(lv_group_t* group) {
    std::vector<lv_obj_t*> out;
    if (!group) return out;
    uint32_t n = lv_group_get_obj_count(group);
    for (uint32_t i = 0; i < n && int(out.size()) < kNumberSlots; ++i) {
        lv_obj_t* o = lv_group_get_obj_by_index(group, i);
        if (!o || lv_obj_check_type(o, &lv_textarea_class)) continue;
        if (lv_obj_has_state(o, LV_STATE_DISABLED) || !lv_obj_is_visible(o)) continue;
        out.push_back(o);
    }
    return out;
}

lv_obj_t* number_badge(int digit, int x, int y) {
    lv_obj_t* b = lv_obj_create(lv_layer_top());
    style_plain(b);
    lv_obj_set_size(b, 13, 13);
    lv_obj_set_style_radius(b, 4, 0);
    lv_obj_set_style_bg_color(b, c(kAccent), 0);
    lv_obj_set_style_bg_opa(b, LV_OPA_COVER, 0);
    lv_obj_remove_flag(b, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_remove_flag(b, LV_OBJ_FLAG_CLICKABLE);
    char t[2] = {char('0' + digit), 0};
    lv_obj_center(label(b, t, &zs_font_12, 0xffffff));
    // Keep it on the screen.
    if (x > kScreenW - 13) x = kScreenW - 13;
    if (y > kScreenH - 13) y = kScreenH - 13;
    lv_obj_set_pos(b, x < 0 ? 0 : x, y < 0 ? 0 : y);
    g_badges.push_back(b);
    g_shown_at = lv_tick_get();
    return b;
}

int number_hints_show(const std::vector<lv_obj_t*>& targets) {
    number_hints_hide();
    for (size_t i = 0; i < targets.size() && int(i) < kNumberSlots; ++i) {
        lv_area_t a;
        lv_obj_get_coords(targets[i], &a);
        // A row: on its left edge (values sit on the right). A small control
        // (a switch, a slider): just left of it, on its row's label side.
        int h = lv_area_get_height(&a);
        int x = lv_area_get_width(&a) < kScreenW / 2 ? a.x1 - 16 : a.x1 + 2;
        number_badge(int(i) + 3, x, a.y1 + (h > 13 ? (h - 13) / 2 : 0));
    }
    return int(targets.size());
}

void number_hints_hide() {
    for (lv_obj_t* b : g_badges) lv_obj_delete(b);
    g_badges.clear();
}

bool number_hints_shown() { return !g_badges.empty(); }

void number_hints_tick() {
    if (!g_badges.empty() && lv_tick_elaps(g_shown_at) > kHintMs) number_hints_hide();
}

}  // namespace zs
