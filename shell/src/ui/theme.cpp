#include "ui/theme.h"

namespace zs::theme {

void style_plain(lv_obj_t* o) {
    lv_obj_set_style_bg_opa(o, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(o, 0, 0);
    lv_obj_set_style_pad_all(o, 0, 0);
    lv_obj_set_style_radius(o, 0, 0);
    lv_obj_set_style_shadow_width(o, 0, 0);
}

void style_panel(lv_obj_t* o) {
    lv_obj_set_style_bg_color(o, c(kSurface), 0);
    lv_obj_set_style_bg_opa(o, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(o, 0, 0);
    lv_obj_set_style_radius(o, 0, 0);
    lv_obj_set_style_pad_all(o, 6, 0);
    lv_obj_set_style_shadow_width(o, 0, 0);
}

lv_obj_t* label(lv_obj_t* parent, const char* text, const lv_font_t* font, uint32_t color) {
    lv_obj_t* l = lv_label_create(parent);
    lv_label_set_text(l, text);
    lv_obj_set_style_text_font(l, font, 0);
    lv_obj_set_style_text_color(l, c(color), 0);
    return l;
}

void style_focus_list(lv_obj_t* list) {
    lv_obj_set_style_bg_color(list, c(kBg), 0);
    lv_obj_set_style_border_width(list, 0, 0);
    lv_obj_set_style_pad_all(list, 2, 0);
    lv_obj_set_style_pad_row(list, 2, 0);
    lv_obj_set_style_radius(list, 0, 0);
    uint32_t n = lv_obj_get_child_count(list);
    for (uint32_t i = 0; i < n; ++i) {
        lv_obj_t* b = lv_obj_get_child(list, i);
        lv_obj_set_style_bg_color(b, c(kSurface), 0);
        lv_obj_set_style_bg_opa(b, LV_OPA_COVER, 0);
        lv_obj_set_style_text_color(b, c(kText), 0);
        lv_obj_set_style_radius(b, 0, 0);
        lv_obj_set_style_pad_ver(b, 5, 0);
        lv_obj_set_style_pad_hor(b, 8, 0);
        lv_obj_set_style_border_width(b, 0, 0);
        style_focus_fill(b, LV_STATE_FOCUSED);
        lv_obj_set_style_text_color(b, c(kOnFocus), LV_STATE_FOCUSED);
        lv_obj_set_style_outline_width(b, 0, LV_STATE_FOCUS_KEY);
    }
}

void style_focus_fill(lv_obj_t* o, lv_style_selector_t selector) {
    lv_obj_set_style_bg_color(o, c(kFocus), selector);
    lv_obj_set_style_bg_grad_color(o, c(kFocusLow), selector);
    lv_obj_set_style_bg_grad_dir(o, LV_GRAD_DIR_VER, selector);
    lv_obj_set_style_bg_opa(o, LV_OPA_COVER, selector);
}

void style_raised(lv_obj_t* o) {
    lv_obj_set_style_bg_color(o, c(kRaise), 0);
    lv_obj_set_style_bg_grad_color(o, c(kRaiseLow), 0);
    lv_obj_set_style_bg_grad_dir(o, LV_GRAD_DIR_VER, 0);
    lv_obj_set_style_bg_opa(o, LV_OPA_COVER, 0);
    lv_obj_set_style_border_color(o, c(0x404040), 0);
    lv_obj_set_style_border_width(o, 1, 0);
    lv_obj_set_style_radius(o, 0, 0);
    lv_obj_set_style_shadow_color(o, c(kGrey0), 0);
    lv_obj_set_style_shadow_width(o, 14, 0);
    lv_obj_set_style_shadow_offset_y(o, 4, 0);
    lv_obj_set_style_shadow_opa(o, LV_OPA_80, 0);
}

}  // namespace zs::theme
