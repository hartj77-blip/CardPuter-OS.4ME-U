#include "apps/net/net_ui.h"

#include "apps/common.h"

namespace zs::net {
namespace {
using namespace theme;

struct Cb {
    std::function<void(const std::string&)> fn;
};

constexpr int kMaxLines = 200;   // keep the backlog bounded

}  // namespace

lv_obj_t* input_screen(lv_obj_t* root, lv_group_t* group, const char* title, const char* placeholder,
                       const std::string& initial, std::function<void(const std::string&)> on_submit) {
    lv_obj_t* page = lv_obj_create(root);
    style_plain(page);
    lv_obj_set_size(page, kScreenW, kContentH);
    lv_obj_remove_flag(page, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t* t = label(page, title, &zs_font_14);
    lv_obj_set_pos(t, 10, 12);

    lv_obj_t* ta = lv_textarea_create(page);
    lv_textarea_set_one_line(ta, true);
    lv_textarea_set_text(ta, initial.c_str());
    lv_textarea_set_placeholder_text(ta, placeholder);
    lv_obj_set_size(ta, kScreenW - 20, 36);
    lv_obj_set_pos(ta, 10, 42);
    lv_obj_set_style_bg_color(ta, c(kSurface), 0);
    lv_obj_set_style_border_color(ta, c(kAccent), 0);
    lv_obj_set_style_border_width(ta, 1, 0);

    Cb* cb = new Cb{std::move(on_submit)};
    lv_obj_set_user_data(ta, cb);
    lv_obj_add_event_cb(ta, [](lv_event_t* e) {
        lv_obj_t* ta = lv_event_get_target_obj(e);
        auto* cb = static_cast<Cb*>(lv_obj_get_user_data(ta));
        if (cb) cb->fn(lv_textarea_get_text(ta));
    }, LV_EVENT_READY, nullptr);
    lv_obj_add_event_cb(ta, [](lv_event_t* e) {
        delete static_cast<Cb*>(lv_obj_get_user_data(lv_event_get_target_obj(e)));
    }, LV_EVENT_DELETE, nullptr);

    lv_obj_t* h = label(page, "Enter to run     Esc to go back", &zs_font_12, kMuted);
    lv_obj_set_pos(h, 10, 88);

    shell_add_focus(ta);
    lv_group_focus_obj(ta);
    lv_group_set_editing(group, true);
    return page;
}

void OutputView::build(lv_obj_t* root, int top) {
    panel_ = lv_obj_create(root);
    style_panel(panel_);
    lv_obj_set_pos(panel_, 4, top);
    lv_obj_set_size(panel_, kScreenW - 8, kContentH - top - 2);
    lv_obj_set_scrollbar_mode(panel_, LV_SCROLLBAR_MODE_ACTIVE);
    lv_obj_set_style_pad_all(panel_, 6, 0);

    label_ = lv_label_create(panel_);
    lv_obj_set_style_text_font(label_, &zs_font_12, 0);
    lv_obj_set_style_text_color(label_, c(kText), 0);
    lv_label_set_long_mode(label_, LV_LABEL_LONG_WRAP);
    lv_obj_set_width(label_, kScreenW - 24);
    lv_label_set_text(label_, "");

    placeholder_ = label(panel_, "", &zs_font_12, kMuted);
    lv_obj_center(placeholder_);
}

void OutputView::set_placeholder(const std::string& text) {
    if (!placeholder_ || !lv_obj_is_valid(placeholder_)) {
        if (!panel_) return;
        placeholder_ = label(panel_, "", &zs_font_12, kMuted);
        lv_obj_center(placeholder_);
    }
    lv_label_set_text(placeholder_, text.c_str());
    lv_obj_set_style_text_align(placeholder_, LV_TEXT_ALIGN_CENTER, 0);
}

void OutputView::append(const std::string& line) {
    if (placeholder_ && lv_obj_is_valid(placeholder_)) {
        lv_obj_delete(placeholder_);
        placeholder_ = nullptr;
    }
    if (!text_.empty()) text_ += "\n";
    for (char ch : line) text_ += ch == '\t' ? std::string("  ") : std::string(1, ch);
    if (++lines_ > kMaxLines) {
        size_t nl = text_.find('\n');
        if (nl != std::string::npos) { text_.erase(0, nl + 1); --lines_; }
    }
    render();
    lv_obj_scroll_to_y(panel_, LV_COORD_MAX, LV_ANIM_OFF);
}

void OutputView::clear() {
    text_.clear();
    lines_ = 0;
    render();
}

void OutputView::render() {
    if (label_) lv_label_set_text(label_, text_.c_str());
}

}  // namespace zs::net
