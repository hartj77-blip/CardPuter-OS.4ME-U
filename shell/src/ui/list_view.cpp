#include "ui/list_view.h"

#include <algorithm>
#include <cctype>

#include "platform/keys.h"
#include "ui/app.h"
#include "ui/theme.h"

namespace zs {

using namespace theme;

namespace {
constexpr int kRowH = 22;

std::string lower(std::string s) {
    for (char& ch : s) ch = char(std::tolower((unsigned char)ch));
    return s;
}

// Text a row can be filtered by: printable, not a digit (digits pick rows).
bool filter_char(uint32_t key) { return key >= 32 && key < 127 && !std::isdigit(int(key)); }
}  // namespace

ListView::~ListView() { lv_async_call_cancel(run_cb, this); }

void ListView::build(lv_obj_t* parent, int x, int y, int w, int h) {
    lv_async_call_cancel(run_cb, this);
    rows_.clear();
    filter_.clear();
    pending_ = -1;

    list_ = lv_obj_create(parent);
    style_plain(list_);
    lv_obj_set_pos(list_, x, y);
    lv_obj_set_size(list_, w, h);
    lv_obj_set_flex_flow(list_, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_row(list_, 2, 0);
    lv_obj_set_scrollbar_mode(list_, LV_SCROLLBAR_MODE_ACTIVE);
    lv_obj_set_scroll_dir(list_, LV_DIR_VER);
    lv_obj_add_event_cb(list_, scrolled_cb, LV_EVENT_SCROLL, this);
    lv_obj_update_layout(list_);

    empty_ = label(parent, "", &zs_font_12, kMuted);
    lv_obj_align_to(empty_, list_, LV_ALIGN_TOP_LEFT, 8, 6);
    lv_obj_add_flag(empty_, LV_OBJ_FLAG_HIDDEN);

    // The filter as typed, in a chip at the top right of the list.
    chip_ = lv_obj_create(parent);
    style_plain(chip_);
    lv_obj_set_size(chip_, LV_SIZE_CONTENT, 16);
    lv_obj_set_style_pad_hor(chip_, 5, 0);
    lv_obj_set_style_radius(chip_, 4, 0);
    lv_obj_set_style_bg_color(chip_, c(kSurface2), 0);
    lv_obj_set_style_bg_opa(chip_, LV_OPA_COVER, 0);
    lv_obj_set_style_border_color(chip_, c(kAccent), 0);
    lv_obj_set_style_border_width(chip_, 1, 0);
    lv_obj_remove_flag(chip_, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_center(label(chip_, "", &zs_font_12, kText));
    lv_obj_add_flag(chip_, LV_OBJ_FLAG_HIDDEN);
}

lv_obj_t* ListView::add(const char* icon, const std::string& text, const std::string& value,
                        std::function<void()> on_enter) {
    const int w = lv_obj_get_width(list_) - 4;
    lv_obj_t* row = lv_obj_create(list_);
    style_plain(row);
    lv_obj_set_size(row, w, kRowH);
    lv_obj_set_style_radius(row, 4, 0);
    lv_obj_set_style_bg_color(row, c(kSurface), 0);
    lv_obj_set_style_bg_opa(row, LV_OPA_COVER, 0);
    lv_obj_set_style_bg_color(row, c(kAccent), LV_STATE_FOCUSED);
    lv_obj_set_style_outline_width(row, 0, LV_STATE_FOCUS_KEY);
    lv_obj_remove_flag(row, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(row, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_flag(row, LV_OBJ_FLAG_SCROLL_ON_FOCUS);
    lv_obj_set_user_data(row, (void*)(intptr_t)rows_.size());
    lv_obj_add_event_cb(row, clicked_cb, LV_EVENT_CLICKED, this);

    // Children follow the row's focus (white on the accent).
    auto follow = [](lv_obj_t* o) { lv_obj_remove_flag(o, LV_OBJ_FLAG_CLICKABLE); };

    lv_obj_t* badge = lv_obj_create(row);
    style_plain(badge);
    lv_obj_set_size(badge, 12, 12);
    lv_obj_set_style_radius(badge, 3, 0);
    lv_obj_set_style_bg_color(badge, c(kSurface2), 0);
    lv_obj_set_style_bg_opa(badge, LV_OPA_COVER, 0);
    lv_obj_remove_flag(badge, LV_OBJ_FLAG_SCROLLABLE);
    follow(badge);
    lv_obj_t* num = label(badge, "", &zs_font_12, kMuted);
    lv_obj_center(num);
    lv_obj_align(badge, LV_ALIGN_LEFT_MID, 4, 0);

    int tx = 22;
    if (icon) {
        lv_obj_t* ic = label(row, icon, &zs_font_12, kMuted);
        follow(ic);
        lv_obj_align(ic, LV_ALIGN_LEFT_MID, 22, 0);
        tx = 40;
    }
    lv_obj_t* v = nullptr;
    int vw = 0;
    if (!value.empty()) {
        v = label(row, value.c_str(), &zs_font_12, kMuted);
        follow(v);
        lv_obj_align(v, LV_ALIGN_RIGHT_MID, -6, 0);
        lv_obj_update_layout(v);
        vw = lv_obj_get_width(v) + 10;
    }
    lv_obj_t* t = label(row, text.c_str(), &zs_font_14, kText);
    follow(t);
    lv_obj_set_width(t, w - tx - vw - 6);
    lv_label_set_long_mode(t, LV_LABEL_LONG_DOT);
    lv_obj_align(t, LV_ALIGN_LEFT_MID, tx, 0);

    // Focused: everything on the row turns white.
    for (lv_obj_t* o : {t, v}) {
        if (!o) continue;
        lv_obj_set_style_text_color(o, c(kOnAccent), LV_STATE_FOCUSED);
    }
    shell_add_focus(row);
    rows_.push_back({row, num, text, std::move(on_enter), v});
    return row;
}

lv_obj_t* ListView::add_choice(const char* icon, const std::string& text, std::vector<std::string> choices, int at,
                               std::function<void(int)> on_change) {
    if (choices.empty()) choices.push_back("");
    at = std::clamp(at, 0, int(choices.size()) - 1);
    // Room for the widest value, so the text doesn't move as it changes.
    std::string widest;
    for (auto& ch : choices)
        if (lv_text_get_width(ch.c_str(), uint32_t(ch.size()), &zs_font_12, 0) >
            lv_text_get_width(widest.c_str(), uint32_t(widest.size()), &zs_font_12, 0))
            widest = ch;
    lv_obj_t* row = add(icon, text, LV_SYMBOL_LEFT " " + widest + " " LV_SYMBOL_RIGHT, nullptr);
    Row& r = rows_.back();
    r.choices = std::move(choices);
    r.at = at;
    r.on_change = std::move(on_change);
    lv_label_set_text(r.value, r.choices[size_t(at)].c_str());
    lv_obj_set_style_text_color(r.value, c(kText), 0);
    return row;
}

void ListView::set_value(int i, const std::string& value) {
    if (i < 0 || i >= int(rows_.size()) || !rows_[size_t(i)].value) return;
    lv_label_set_text(rows_[size_t(i)].value, value.c_str());
}

bool ListView::focused_is_choice() const {
    int i = focused();
    return i >= 0 && !rows_[size_t(i)].choices.empty();
}

void ListView::step(int i, int dir) {
    Row& r = rows_[size_t(i)];
    int n = int(r.choices.size());
    r.at = ((r.at + dir) % n + n) % n;
    lv_label_set_text(r.value, r.choices[size_t(r.at)].c_str());
    if (r.on_change) {
        std::function<void(int)> fn = r.on_change;
        fn(r.at);
    }
}

void ListView::finish() {
    // Labels can't see their row's state: mirror focus onto them.
    auto mirror = [](lv_event_t* e) {
        lv_obj_t* row = lv_event_get_target_obj(e);
        bool on = lv_event_get_code(e) == LV_EVENT_FOCUSED;
        for (uint32_t i = 0; i < lv_obj_get_child_count(row); ++i) {
            lv_obj_t* ch = lv_obj_get_child(row, int32_t(i));
            if (on) lv_obj_add_state(ch, LV_STATE_FOCUSED);
            else lv_obj_remove_state(ch, LV_STATE_FOCUSED);
        }
    };
    for (Row& r : rows_) {
        lv_obj_add_event_cb(r.obj, mirror, LV_EVENT_FOCUSED, nullptr);
        lv_obj_add_event_cb(r.obj, mirror, LV_EVENT_DEFOCUSED, nullptr);
    }
    apply_filter();
}

void ListView::apply_filter() {
    std::string f = lower(filter_);
    lv_obj_t* first = nullptr;
    int shown = 0;
    for (Row& r : rows_) {
        bool match = f.empty() || lower(r.text).find(f) != std::string::npos;
        if (match) {
            lv_obj_remove_flag(r.obj, LV_OBJ_FLAG_HIDDEN);
            if (!first) first = r.obj;
            ++shown;
        } else {
            lv_obj_add_flag(r.obj, LV_OBJ_FLAG_HIDDEN);
        }
    }
    if (filter_.empty()) {
        lv_obj_add_flag(chip_, LV_OBJ_FLAG_HIDDEN);
    } else {
        lv_label_set_text(lv_obj_get_child(chip_, 0), (ZS_ICON_SEARCH " " + filter_).c_str());
        lv_obj_remove_flag(chip_, LV_OBJ_FLAG_HIDDEN);
        lv_obj_update_layout(chip_);
        lv_obj_align_to(chip_, list_, LV_ALIGN_BOTTOM_RIGHT, -6, -4);
        lv_obj_move_foreground(chip_);
    }
    if (shown == 0) {
        lv_label_set_text(empty_, filter_.empty() ? empty_text_.c_str() : ("Nothing matches \"" + filter_ + "\"").c_str());
        lv_obj_remove_flag(empty_, LV_OBJ_FLAG_HIDDEN);
    } else {
        lv_obj_add_flag(empty_, LV_OBJ_FLAG_HIDDEN);
    }
    // Keep the focus on a row that's still there.
    lv_group_t* g = first ? lv_obj_get_group(first) : nullptr;
    lv_obj_t* f_obj = g ? lv_group_get_focused(g) : nullptr;
    bool focus_ok = false;
    for (Row& r : rows_)
        if (r.obj == f_obj && !lv_obj_has_flag(r.obj, LV_OBJ_FLAG_HIDDEN)) focus_ok = true;
    if (first && (!focus_ok || !filter_.empty())) lv_group_focus_obj(first);
    if (first) lv_obj_scroll_to_view(first, LV_ANIM_OFF);
    lv_obj_update_layout(list_);
    renumber();
}

// The rows' badges: the number key that picks each. Like a phone menu, 1 to
// 9 are the first nine rows the filter leaves, wherever the list is
// scrolled, so "Settings, 9" is always the same row.
void ListView::renumber() {
    int shown = 0;
    for (Row& r : rows_) {
        int n = 0;
        if (!lv_obj_has_flag(r.obj, LV_OBJ_FLAG_HIDDEN) && ++shown <= 9) n = shown;
        char t[2] = {char(n ? '0' + n : 0), 0};
        lv_label_set_text(r.badge, t);
        lv_obj_t* box = lv_obj_get_parent(r.badge);
        lv_obj_set_style_bg_opa(box, n ? LV_OPA_COVER : LV_OPA_TRANSP, 0);
    }
}

bool ListView::key(uint32_t key) {
    if (!list_) return false;
    if (key >= '1' && key <= '9') {   // pick the row with that number
        int want = int(key - '0'), shown = 0;
        for (size_t i = 0; i < rows_.size(); ++i) {
            if (lv_obj_has_flag(rows_[i].obj, LV_OBJ_FLAG_HIDDEN) || ++shown != want) continue;
            lv_group_focus_obj(rows_[i].obj);
            lv_obj_scroll_to_view(rows_[i].obj, LV_ANIM_OFF);
            pending_ = int(i);
            lv_async_call_cancel(run_cb, this);
            lv_async_call(run_cb, this);
            break;
        }
        return true;
    }
    if ((key == LV_KEY_LEFT || key == LV_KEY_RIGHT) && focused_is_choice()) {
        step(focused(), key == LV_KEY_RIGHT ? +1 : -1);
        return true;
    }
    if (key == LV_KEY_BACKSPACE || key == LV_KEY_DEL) {
        if (filter_.empty()) return false;
        filter_.pop_back();
        apply_filter();
        return true;
    }
    if (key == LV_KEY_ESC && !filter_.empty()) {   // Esc clears the filter first
        clear_filter();
        return true;
    }
    if (!filter_char(key) || (key == ' ' && filter_.empty())) return false;
    if (filter_.size() >= 24) return true;
    filter_ += char(key);
    apply_filter();
    return true;
}

void ListView::clear_filter() {
    filter_.clear();
    apply_filter();
}

int ListView::focused() const {
    if (rows_.empty()) return -1;
    lv_group_t* g = lv_obj_get_group(rows_.front().obj);
    lv_obj_t* f = g ? lv_group_get_focused(g) : nullptr;
    for (size_t i = 0; i < rows_.size(); ++i)
        if (rows_[i].obj == f) return int(i);
    return -1;
}

void ListView::clicked_cb(lv_event_t* e) {
    auto* self = static_cast<ListView*>(lv_event_get_user_data(e));
    lv_obj_t* row = lv_event_get_current_target_obj(e);
    self->pending_ = int((intptr_t)lv_obj_get_user_data(row));
    // Not now: on_enter may rebuild the screen, deleting this row mid-event.
    lv_async_call_cancel(run_cb, self);
    lv_async_call(run_cb, self);
}

void ListView::scrolled_cb(lv_event_t* e) { static_cast<ListView*>(lv_event_get_user_data(e))->renumber(); }

void ListView::run_cb(void* p) {
    auto* self = static_cast<ListView*>(p);
    int i = self->pending_;
    self->pending_ = -1;
    if (i < 0 || i >= int(self->rows_.size())) return;
    if (!self->rows_[size_t(i)].choices.empty()) {   // a value row: Enter steps it
        self->step(i, +1);
        return;
    }
    if (!self->rows_[size_t(i)].on_enter) return;
    std::function<void()> fn = self->rows_[size_t(i)].on_enter;   // it may rebuild the list
    fn();
}

}  // namespace zs
