#include "ui/list_view.h"

#include <algorithm>
#include <cctype>

#include "platform/keys.h"
#include "ui/app.h"
#include "ui/search_match.h"
#include "ui/theme.h"

namespace zs {

using namespace theme;

namespace {
constexpr int kRowH = 22;
constexpr int kPitch = kRowH + 2;   // a row and the gap under it
constexpr int kPageStep = 5;        // Page Up / Down (docs/KEYS.md)

std::string lower(std::string s) {
    for (char& ch : s) ch = char(std::tolower((unsigned char)ch));
    return s;
}

// Text a row can be filtered by: printable, not a digit (digits pick rows).
bool filter_char(uint32_t key) { return key >= 32 && key < 127 && !std::isdigit(int(key)); }

int text_w(const std::string& s) {
    return int(lv_text_get_width(s.c_str(), uint32_t(s.size()), &zs_font_12, 0));
}
}  // namespace

ListView::~ListView() { lv_async_call_cancel(run_cb, this); }

void ListView::reset() {
    lv_async_call_cancel(run_cb, this);
    rows_.clear();
    shown_.clear();
    slots_.clear();
    filter_.clear();
    pending_ = -1;
    top_ = 0;
    cursor_ = -1;
    list_ = chip_ = empty_ = bar_ = nullptr;
}

void ListView::build(lv_obj_t* parent, int x, int y, int w, int h) {
    reset();

    // A plain box: it never scrolls. The slots inside it stay where they
    // are and show different rows instead (bind()).
    list_ = lv_obj_create(parent);
    style_plain(list_);
    lv_obj_set_pos(list_, x, y);
    lv_obj_set_size(list_, w, h);
    lv_obj_remove_flag(list_, LV_OBJ_FLAG_SCROLLABLE);
    width_ = w - 4;   // room on the right for the position bar

    // Whole rows that fit, and one more, cut off at the bottom, if there is
    // room for part of it: it shows there is more below, as before. Only the
    // whole rows can take the focus.
    page_ = std::max(1, (h + 2) / kPitch);
    int n = page_ + (h > page_ * kPitch ? 1 : 0);
    for (int i = 0; i < n; ++i) make_slot(i, i < page_);

    bar_ = lv_obj_create(list_);
    style_plain(bar_);
    lv_obj_set_style_radius(bar_, 1, 0);
    lv_obj_set_style_bg_color(bar_, c(kMuted), 0);
    lv_obj_set_style_bg_opa(bar_, LV_OPA_60, 0);
    lv_obj_remove_flag(bar_, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_flag(bar_, LV_OBJ_FLAG_HIDDEN);

    empty_ = label(parent, "", &zs_font_12, kMuted);
    lv_obj_align_to(empty_, list_, LV_ALIGN_TOP_LEFT, 8, 6);
    lv_obj_add_flag(empty_, LV_OBJ_FLAG_HIDDEN);

    // The filter as typed, in a chip at the bottom right of the list.
    chip_ = lv_obj_create(parent);
    style_plain(chip_);
    lv_obj_set_size(chip_, LV_SIZE_CONTENT, 16);
    lv_obj_set_style_pad_hor(chip_, 5, 0);
    lv_obj_set_style_radius(chip_, 0, 0);
    lv_obj_set_style_bg_color(chip_, c(kSurface2), 0);
    lv_obj_set_style_bg_opa(chip_, LV_OPA_COVER, 0);
    lv_obj_set_style_border_color(chip_, c(kAccent), 0);
    lv_obj_set_style_border_width(chip_, 1, 0);
    lv_obj_remove_flag(chip_, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_center(label(chip_, "", &zs_font_12, kText));
    lv_obj_add_flag(chip_, LV_OBJ_FLAG_HIDDEN);
}

// The widgets of one line, made once per build(). bind() fills them in.
void ListView::make_slot(int i, bool focusable) {
    Slot s;
    lv_obj_t* row = s.obj = lv_obj_create(list_);
    style_plain(row);
    lv_obj_set_pos(row, 0, i * kPitch);
    lv_obj_set_size(row, width_, kRowH);
    lv_obj_set_style_radius(row, 0, 0);
    lv_obj_set_style_bg_color(row, c(kSurface), 0);
    lv_obj_set_style_bg_opa(row, LV_OPA_COVER, 0);
    style_focus_fill(row, LV_STATE_FOCUSED);
    lv_obj_set_style_outline_width(row, 0, LV_STATE_FOCUS_KEY);
    lv_obj_remove_flag(row, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_user_data(row, (void*)(intptr_t)i);
    lv_obj_add_flag(row, LV_OBJ_FLAG_HIDDEN);
    if (focusable) {
        lv_obj_add_flag(row, LV_OBJ_FLAG_CLICKABLE);
        lv_obj_add_event_cb(row, clicked_cb, LV_EVENT_CLICKED, this);
        lv_obj_add_event_cb(row, focused_cb, LV_EVENT_FOCUSED, this);
        lv_obj_add_event_cb(row, focused_cb, LV_EVENT_DEFOCUSED, this);
        shell_add_focus(row);
    } else {
        lv_obj_remove_flag(row, LV_OBJ_FLAG_CLICKABLE);
    }

    s.badge = lv_obj_create(row);
    style_plain(s.badge);
    lv_obj_set_size(s.badge, 12, 12);
    lv_obj_set_style_radius(s.badge, 0, 0);
    lv_obj_set_style_bg_color(s.badge, c(kSurface2), 0);
    lv_obj_remove_flag(s.badge, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_remove_flag(s.badge, LV_OBJ_FLAG_CLICKABLE);
    s.num = label(s.badge, "", &zs_font_12, kMuted);
    lv_obj_center(s.num);
    lv_obj_align(s.badge, LV_ALIGN_LEFT_MID, 4, 0);

    s.icon = label(row, "", &zs_font_12, kMuted);
    lv_obj_align(s.icon, LV_ALIGN_LEFT_MID, 22, 0);

    s.value = label(row, "", &zs_font_12, kMuted);
    lv_obj_align(s.value, LV_ALIGN_RIGHT_MID, -6, 0);

    s.text = label(row, "", &zs_font_14, kText);
    lv_label_set_long_mode(s.text, LV_LABEL_LONG_DOT);

    // Focused: the text and value turn black on the lit row.
    for (lv_obj_t* o : {s.text, s.value}) lv_obj_set_style_text_color(o, c(kOnFocus), LV_STATE_FOCUSED);
    slots_.push_back(s);
}

// Put the row at shown_[top_ + k] into slot k, or hide the slot if the list
// ends above it. Only labels change: no widget is made or deleted.
void ListView::bind(int k) {
    Slot& s = slots_[size_t(k)];
    int pos = top_ + k;
    if (pos >= int(shown_.size())) {
        lv_obj_add_flag(s.obj, LV_OBJ_FLAG_HIDDEN);
        return;
    }
    lv_obj_remove_flag(s.obj, LV_OBJ_FLAG_HIDDEN);
    const Row& r = rows_[size_t(shown_[size_t(pos)])];

    // The number key that picks it. Like a phone menu, 1 to 9 are the first
    // nine rows the filter leaves, wherever the list is scrolled, so
    // "Settings, 9" is always the same row.
    char t[2] = {char(pos < 9 ? '1' + pos : 0), 0};
    lv_label_set_text(s.num, t);
    lv_obj_set_style_bg_opa(s.badge, pos < 9 ? LV_OPA_COVER : LV_OPA_TRANSP, 0);

    int tx = 22;
    if (r.icon.empty()) {
        lv_obj_add_flag(s.icon, LV_OBJ_FLAG_HIDDEN);
    } else {
        lv_label_set_text(s.icon, r.icon.c_str());
        lv_obj_remove_flag(s.icon, LV_OBJ_FLAG_HIDDEN);
        tx = 40;
    }

    lv_label_set_text(s.value, r.value.c_str());
    uint32_t vc = r.own_color ? r.value_color : kMuted;
    lv_obj_set_style_text_color(s.value, c(vc), 0);
    int vw = r.value_room ? r.value_room : (r.value.empty() ? 0 : text_w(r.value) + 10);

    lv_label_set_text(s.text, r.text.c_str());
    lv_obj_set_width(s.text, width_ - tx - vw - 6);
    lv_obj_align(s.text, LV_ALIGN_LEFT_MID, tx, 0);
}

void ListView::bind_all() {
    for (int k = 0; k < int(slots_.size()); ++k) bind(k);
    scroll_bar();
}

// A thin bar on the right: how much of the list is on screen, and where.
void ListView::scroll_bar() {
    if (!bar_) return;
    int n = int(shown_.size());
    if (n <= page_) {
        lv_obj_add_flag(bar_, LV_OBJ_FLAG_HIDDEN);
        return;
    }
    int h = lv_obj_get_height(list_);
    int bh = std::max(8, h * page_ / n);
    int y = (h - bh) * top_ / std::max(1, n - page_);
    lv_obj_set_size(bar_, 2, bh);
    lv_obj_set_pos(bar_, width_ + 1, y);
    lv_obj_remove_flag(bar_, LV_OBJ_FLAG_HIDDEN);
}

int ListView::add(const char* icon, const std::string& text, const std::string& value,
                  std::function<void()> on_enter) {
    Row r;
    if (icon) r.icon = icon;
    r.text = text;
    r.value = value;
    r.on_enter = std::move(on_enter);
    rows_.push_back(std::move(r));
    return int(rows_.size()) - 1;
}

int ListView::add_choice(const char* icon, const std::string& text, std::vector<std::string> choices, int at,
                         std::function<void(int)> on_change) {
    if (choices.empty()) choices.push_back("");
    at = std::clamp(at, 0, int(choices.size()) - 1);
    int i = add(icon, text, "", nullptr);
    Row& r = rows_.back();
    // Room for the widest value and the arrows, so the text doesn't move as
    // the value changes.
    int widest = 0;
    for (auto& ch : choices) widest = std::max(widest, text_w(ch));
    r.value_room = text_w(LV_SYMBOL_LEFT "  " LV_SYMBOL_RIGHT) + widest + 10;
    r.value = choices[size_t(at)];
    r.value_color = kText;
    r.own_color = true;
    r.choices = std::move(choices);
    r.at = at;
    r.on_change = std::move(on_change);
    return i;
}

int ListView::slot_of(int row) const {
    for (int k = 0; k < int(slots_.size()); ++k) {
        int pos = top_ + k;
        if (pos < int(shown_.size()) && shown_[size_t(pos)] == row) return k;
    }
    return -1;
}

void ListView::set_value(int i, const std::string& value) {
    if (i < 0 || i >= int(rows_.size())) return;
    rows_[size_t(i)].value = value;
    int k = slot_of(i);
    if (k >= 0) bind(k);
}

void ListView::set_value_color(int i, uint32_t color) {
    if (i < 0 || i >= int(rows_.size())) return;
    rows_[size_t(i)].value_color = color;
    rows_[size_t(i)].own_color = true;
    int k = slot_of(i);
    if (k >= 0) bind(k);
}

// Focus shown_[pos]. If it is off the screen, the rows move under the slots
// (top_ changes) and the slots are filled in again; the focus stays on a slot.
void ListView::move_to(int pos) {
    int n = int(shown_.size());
    if (n == 0 || slots_.empty()) return;
    pos = std::clamp(pos, 0, n - 1);
    int top = top_;
    if (pos < top) top = pos;
    if (pos >= top + page_) top = pos - page_ + 1;
    top = std::clamp(top, 0, std::max(0, n - page_));
    cursor_ = pos;
    if (top != top_) {
        top_ = top;
        bind_all();
    }
    lv_group_focus_obj(slots_[size_t(pos - top_)].obj);
}

void ListView::focus(int i) {
    auto it = std::find(shown_.begin(), shown_.end(), i);
    if (it == shown_.end()) return;   // filtered out
    move_to(int(it - shown_.begin()));
}

bool ListView::focused_is_choice() const {
    int i = focused();
    return i >= 0 && !rows_[size_t(i)].choices.empty();
}

void ListView::step(int i, int dir) {
    Row& r = rows_[size_t(i)];
    int n = int(r.choices.size());
    r.at = ((r.at + dir) % n + n) % n;
    r.value = r.choices[size_t(r.at)];
    int k = slot_of(i);
    if (k >= 0) bind(k);
    if (r.on_change) {
        std::function<void(int)> fn = r.on_change;
        fn(r.at);
    }
}

void ListView::finish() { apply_filter(); }

void ListView::apply_filter() {
    if (!list_) return;
    int was = focused();
    std::string f = lower(filter_);
    shown_.clear();
    // The same rule as Home search (ui/search_match.h): every typed word
    // somewhere in the row, best matches first, ties in the list's order.
    // Text with no letters or digits (".") is looked for as it is.
    bool words = search_norm(filter_).find_first_not_of(' ') != std::string::npos;
    std::vector<int> score(rows_.size(), 0);
    for (int i = 0; i < int(rows_.size()); ++i) {
        const std::string& t = rows_[size_t(i)].text;
        if (f.empty()) score[size_t(i)] = 1;
        else if (words) score[size_t(i)] = search_score(t, "", filter_);
        else score[size_t(i)] = lower(t).find(f) != std::string::npos ? 1 : 0;
        if (score[size_t(i)]) shown_.push_back(i);
    }
    std::stable_sort(shown_.begin(), shown_.end(), [&](int a, int b) { return score[size_t(a)] > score[size_t(b)]; });

    if (filter_.empty()) {
        lv_obj_add_flag(chip_, LV_OBJ_FLAG_HIDDEN);
    } else {
        lv_label_set_text(lv_obj_get_child(chip_, 0), (ZS_ICON_SEARCH " " + filter_).c_str());
        lv_obj_remove_flag(chip_, LV_OBJ_FLAG_HIDDEN);
        lv_obj_update_layout(chip_);
        lv_obj_align_to(chip_, list_, LV_ALIGN_BOTTOM_RIGHT, -6, -4);
        lv_obj_move_foreground(chip_);
    }
    if (shown_.empty()) {
        lv_label_set_text(empty_, filter_.empty() ? empty_text_.c_str() : ("Nothing matches \"" + filter_ + "\"").c_str());
        lv_obj_remove_flag(empty_, LV_OBJ_FLAG_HIDDEN);
    } else {
        lv_obj_add_flag(empty_, LV_OBJ_FLAG_HIDDEN);
    }

    // Keep the focus on the row it was on if that row is still there and
    // nothing is typed; otherwise the first row (the best match).
    int want = 0;
    if (filter_.empty() && was >= 0) {
        auto it = std::find(shown_.begin(), shown_.end(), was);
        if (it != shown_.end()) want = int(it - shown_.begin());
    }
    top_ = 0;
    cursor_ = shown_.empty() ? -1 : 0;
    bind_all();
    move_to(want);
}

bool ListView::only_list_in_group() const {
    lv_group_t* g = slots_.empty() ? nullptr : lv_obj_get_group(slots_.front().obj);
    if (!g) return true;
    uint32_t n = lv_group_get_obj_count(g);
    for (uint32_t i = 0; i < n; ++i) {
        lv_obj_t* o = lv_group_get_obj_by_index(g, i);
        if (!o || lv_obj_has_flag(o, LV_OBJ_FLAG_HIDDEN)) continue;
        bool ours = false;
        for (const Slot& s : slots_) ours = ours || s.obj == o;
        if (!ours) return false;
    }
    return true;
}

void ListView::run_later(int row) {
    pending_ = row;
    // Not now: on_enter may rebuild the screen, deleting the slot mid-event.
    lv_async_call_cancel(run_cb, this);
    lv_async_call(run_cb, this);
}

bool ListView::key(uint32_t key) {
    if (!list_) return false;
    const int n = int(shown_.size());
    if (key >= '1' && key <= '9') {   // pick the row with that number
        int want = int(key - '1');
        if (want < n) {
            move_to(want);
            run_later(shown_[size_t(want)]);
        }
        return true;
    }
    if ((key == LV_KEY_LEFT || key == LV_KEY_RIGHT) && focused_is_choice()) {
        step(focused(), key == LV_KEY_RIGHT ? +1 : -1);
        return true;
    }
    // Moving. Only while a row has the focus: on a screen with other widgets
    // (a text box above the list) the shell moves between them.
    if (key == LV_KEY_UP || key == LV_KEY_DOWN || key == kKeyPageUp || key == kKeyPageDown ||
        key == LV_KEY_HOME || key == LV_KEY_END) {
        if (focused_slot() < 0) return false;
        if (n == 0) return true;
        int at = std::clamp(cursor_, 0, n - 1);
        if (key == LV_KEY_DOWN) {
            // At the end: round to the top if the list is all there is to
            // move to, else on to the next widget (the shell does that).
            if (at + 1 < n) move_to(at + 1);
            else if (only_list_in_group()) move_to(0);
            else return false;
        } else if (key == LV_KEY_UP) {
            if (at > 0) move_to(at - 1);
            else if (only_list_in_group()) move_to(n - 1);
            else return false;
        } else if (key == kKeyPageDown) {
            move_to(at + kPageStep);
        } else if (key == kKeyPageUp) {
            move_to(at - kPageStep);
        } else {
            move_to(key == LV_KEY_HOME ? 0 : n - 1);
        }
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

int ListView::focused_slot() const {
    if (slots_.empty()) return -1;
    lv_group_t* g = lv_obj_get_group(slots_.front().obj);
    lv_obj_t* f = g ? lv_group_get_focused(g) : nullptr;
    for (int k = 0; k < int(slots_.size()); ++k)
        if (slots_[size_t(k)].obj == f) return k;
    return -1;
}

int ListView::focused() const {
    int k = focused_slot();
    int pos = k < 0 ? -1 : top_ + k;
    return pos >= 0 && pos < int(shown_.size()) ? shown_[size_t(pos)] : -1;
}

void ListView::clicked_cb(lv_event_t* e) {
    auto* self = static_cast<ListView*>(lv_event_get_user_data(e));
    int k = int((intptr_t)lv_obj_get_user_data(lv_event_get_current_target_obj(e)));
    int pos = self->top_ + k;
    if (pos < int(self->shown_.size())) self->run_later(self->shown_[size_t(pos)]);
}

// A slot gained or lost the focus. Its labels can't see the slot's state, so
// it is mirrored onto them. Focus that came from outside (the shell moving
// from a text box into the list) also moves the cursor to that slot's row.
void ListView::focused_cb(lv_event_t* e) {
    auto* self = static_cast<ListView*>(lv_event_get_user_data(e));
    lv_obj_t* row = lv_event_get_current_target_obj(e);
    bool on = lv_event_get_code(e) == LV_EVENT_FOCUSED;
    for (uint32_t i = 0; i < lv_obj_get_child_count(row); ++i) {
        lv_obj_t* ch = lv_obj_get_child(row, int32_t(i));
        if (on) lv_obj_add_state(ch, LV_STATE_FOCUSED);
        else lv_obj_remove_state(ch, LV_STATE_FOCUSED);
    }
    if (on) {
        int pos = self->top_ + int((intptr_t)lv_obj_get_user_data(row));
        if (pos < int(self->shown_.size())) self->cursor_ = pos;
    }
}

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
