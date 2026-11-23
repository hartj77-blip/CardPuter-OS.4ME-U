#include "ui/actions.h"

#include <algorithm>
#include <cctype>
#include <functional>

#include "platform/keys.h"
#include "ui/theme.h"

namespace zs {

using namespace theme;

namespace {

constexpr uint32_t kBarBg = 0x101010, kMenuBg = 0x262626, kMenuBorder = 0x4a4a4a, kDisabled = 0x6a6a6a;
constexpr int kMenuW = 168, kRowH = 18, kMaxRows = 6;

std::vector<Action> g_actions;
lv_obj_t* g_bar = nullptr;
lv_obj_t* g_dim = nullptr;      // dims the screen behind the menu
lv_obj_t* g_menu = nullptr;
lv_obj_t* g_rows = nullptr;     // the menu's rows (scrolls past kMaxRows)
std::vector<int> g_menu_items;  // g_actions index of each menu row
std::vector<lv_obj_t*> g_menu_rows;
int g_sel = -1;

bool in_menu(const Action& a) { return a.letter && a.run; }

// One "key label" pair in the bar: the key in a raised chip, then its label.
void bar_item(const char* key, const std::string& text) {
    lv_obj_t* item = lv_obj_create(g_bar);
    style_plain(item);
    lv_obj_set_size(item, LV_SIZE_CONTENT, kSoftkeyH);
    lv_obj_set_flex_flow(item, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(item, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_column(item, 3, 0);
    lv_obj_remove_flag(item, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_t* chip = lv_obj_create(item);
    style_plain(chip);
    lv_obj_set_size(chip, LV_SIZE_CONTENT, 12);
    lv_obj_set_style_pad_hor(chip, 3, 0);
    lv_obj_set_style_radius(chip, 3, 0);
    lv_obj_set_style_bg_color(chip, c(kSurface2), 0);
    lv_obj_set_style_bg_opa(chip, LV_OPA_COVER, 0);
    lv_obj_remove_flag(chip, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_center(label(chip, key, &zs_font_12, kText));
    label(item, text.c_str(), &zs_font_12, kMuted);
}

void bar_spacer() {
    lv_obj_t* s = lv_obj_create(g_bar);
    style_plain(s);
    lv_obj_set_size(s, 1, 1);
    lv_obj_set_flex_grow(s, 1);
}

void render_bar() {
    if (!g_bar) return;
    lv_obj_clean(g_bar);
    if (g_menu) {
        bar_item("a-z", "Run");
        bar_item("Enter", "Run");
        bar_spacer();
        bar_item("Esc", "Close");
        return;
    }
    int shown = 0;
    for (const Action& a : g_actions) {
        if (!a.key || shown == 3) continue;
        bar_item(a.key, a.label);
        ++shown;
    }
    if (actions_have_menu()) {
        bar_spacer();
        bar_item("Tab", "Menu");
    }
}

// A row and its labels share the highlight state (labels take white text).
void row_state(int i, bool on) {
    if (i < 0 || i >= int(g_menu_rows.size())) return;
    lv_obj_t* row = g_menu_rows[size_t(i)];
    if (on) lv_obj_add_state(row, LV_STATE_USER_1);
    else lv_obj_remove_state(row, LV_STATE_USER_1);
    for (uint32_t k = 0; k < lv_obj_get_child_count(row); ++k) {
        if (on) lv_obj_add_state(lv_obj_get_child(row, int32_t(k)), LV_STATE_USER_1);
        else lv_obj_remove_state(lv_obj_get_child(row, int32_t(k)), LV_STATE_USER_1);
    }
}

void menu_select(int i) {
    row_state(g_sel, false);
    g_sel = i;
    if (g_sel < 0 || g_sel >= int(g_menu_rows.size())) return;
    row_state(g_sel, true);
    lv_obj_scroll_to_view(g_menu_rows[g_sel], LV_ANIM_OFF);
}

// The next row that can run, from `from` in direction `dir` (wrapping).
int next_enabled(int from, int dir) {
    int n = int(g_menu_items.size());
    for (int step = 1; step <= n; ++step) {
        int j = ((from + dir * step) % n + n) % n;
        if (g_actions[size_t(g_menu_items[size_t(j)])].enabled) return j;
    }
    return -1;
}

// Close the menu, then run the action (it may change the screen, and set new
// actions, so it runs from a copy).
void run_action(size_t i) {
    if (i >= g_actions.size() || !g_actions[i].enabled || !g_actions[i].run) return;
    std::function<void()> fn = g_actions[i].run;
    actions_menu_close();
    fn();
}

// The name of what's in focus: a list row's text, or a widget's own label.
std::string focused_name(lv_obj_t* focused) {
    if (!focused) return "";
    lv_obj_t* parent = lv_obj_get_parent(focused);
    if (parent && lv_obj_check_type(parent, &lv_list_class) && lv_obj_check_type(focused, &lv_list_button_class))
        return lv_list_get_button_text(parent, focused);
    if (lv_obj_check_type(focused, &lv_label_class)) return lv_label_get_text(focused);
    uint32_t n = lv_obj_get_child_count(focused);
    for (uint32_t i = n; i-- > 0;) {
        lv_obj_t* ch = lv_obj_get_child(focused, int32_t(i));
        if (lv_obj_check_type(ch, &lv_label_class)) return lv_label_get_text(ch);
    }
    return "";
}

}  // namespace

void actions_build(lv_obj_t* screen) {
    g_bar = lv_obj_create(screen);
    style_plain(g_bar);
    lv_obj_set_size(g_bar, kScreenW, kSoftkeyH);
    lv_obj_set_pos(g_bar, 0, kScreenH - kSoftkeyH);
    lv_obj_set_style_bg_color(g_bar, c(kBarBg), 0);
    lv_obj_set_style_bg_opa(g_bar, LV_OPA_COVER, 0);
    lv_obj_set_style_pad_hor(g_bar, 4, 0);
    lv_obj_set_flex_flow(g_bar, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(g_bar, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_column(g_bar, 9, 0);
    lv_obj_remove_flag(g_bar, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(g_bar, LV_OBJ_FLAG_HIDDEN);
}

void actions_set(std::vector<Action> actions) {
    actions_menu_close();
    g_actions = std::move(actions);
    if (!g_bar) return;
    if (g_actions.empty()) lv_obj_add_flag(g_bar, LV_OBJ_FLAG_HIDDEN);
    else lv_obj_remove_flag(g_bar, LV_OBJ_FLAG_HIDDEN);
    lv_obj_move_foreground(g_bar);
    render_bar();
}

void actions_clear() { actions_set({}); }

bool actions_bar_shown() { return g_bar && !lv_obj_has_flag(g_bar, LV_OBJ_FLAG_HIDDEN); }

bool actions_have_menu() {
    for (const Action& a : g_actions)
        if (in_menu(a)) return true;
    return false;
}

void actions_menu_open(lv_obj_t* focused, const std::string& title) {
    actions_menu_close();
    g_menu_items.clear();
    for (int pass = 0; pass < 2; ++pass)   // the dangerous ones last
        for (size_t i = 0; i < g_actions.size(); ++i)
            if (in_menu(g_actions[i]) && g_actions[i].danger == (pass == 1)) g_menu_items.push_back(int(i));
    if (g_menu_items.empty()) return;

    g_dim = lv_obj_create(lv_layer_top());
    style_plain(g_dim);
    lv_obj_set_size(g_dim, kScreenW, kScreenH - kStatusH - kSoftkeyH);
    lv_obj_set_pos(g_dim, 0, kStatusH);
    lv_obj_set_style_bg_color(g_dim, c(0x000000), 0);
    lv_obj_set_style_bg_opa(g_dim, LV_OPA_40, 0);
    lv_obj_remove_flag(g_dim, LV_OBJ_FLAG_CLICKABLE);

    g_menu = lv_obj_create(lv_layer_top());
    style_plain(g_menu);
    lv_obj_set_width(g_menu, kMenuW);
    lv_obj_set_height(g_menu, LV_SIZE_CONTENT);
    lv_obj_set_style_bg_color(g_menu, c(kMenuBg), 0);
    lv_obj_set_style_bg_opa(g_menu, LV_OPA_COVER, 0);
    lv_obj_set_style_border_color(g_menu, c(kMenuBorder), 0);
    lv_obj_set_style_border_width(g_menu, 1, 0);
    lv_obj_set_style_radius(g_menu, 6, 0);
    lv_obj_set_style_pad_all(g_menu, 3, 0);
    lv_obj_set_flex_flow(g_menu, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_row(g_menu, 0, 0);
    lv_obj_remove_flag(g_menu, LV_OBJ_FLAG_SCROLLABLE);

    std::string name = focused_name(focused);
    lv_obj_t* head = label(g_menu, (name.empty() ? title : name).c_str(), &zs_font_12, kMuted);
    lv_obj_set_width(head, kMenuW - 14);
    lv_label_set_long_mode(head, LV_LABEL_LONG_DOT);
    lv_obj_set_style_pad_left(head, 4, 0);
    lv_obj_set_style_pad_bottom(head, 2, 0);

    g_rows = lv_obj_create(g_menu);
    style_plain(g_rows);
    lv_obj_set_width(g_rows, kMenuW - 8);
    int n = int(g_menu_items.size());
    bool divider = false;
    for (int k = 0; k < n; ++k) divider = divider || g_actions[size_t(g_menu_items[size_t(k)])].danger;
    bool split = divider && !g_actions[size_t(g_menu_items[0])].danger;
    lv_obj_set_height(g_rows, std::min(n, kMaxRows) * kRowH + (split ? 5 : 0));
    lv_obj_set_flex_flow(g_rows, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_scrollbar_mode(g_rows, LV_SCROLLBAR_MODE_OFF);
    lv_obj_set_style_pad_row(g_rows, 0, 0);

    g_menu_rows.clear();
    for (int k = 0; k < n; ++k) {
        const Action& a = g_actions[size_t(g_menu_items[size_t(k)])];
        if (split && a.danger && !g_actions[size_t(g_menu_items[size_t(k - 1)])].danger) {
            lv_obj_t* line = lv_obj_create(g_rows);
            style_plain(line);
            lv_obj_set_size(line, kMenuW - 20, 1);
            lv_obj_set_style_margin_ver(line, 2, 0);
            lv_obj_set_style_margin_left(line, 4, 0);
            lv_obj_set_style_bg_color(line, c(0x3c3c3c), 0);
            lv_obj_set_style_bg_opa(line, LV_OPA_COVER, 0);
        }
        lv_obj_t* row = lv_obj_create(g_rows);
        style_plain(row);
        lv_obj_set_size(row, kMenuW - 8, kRowH);
        lv_obj_set_style_radius(row, 3, 0);
        lv_obj_set_style_pad_hor(row, 5, 0);
        lv_obj_set_style_bg_color(row, c(kAccent), LV_STATE_USER_1);
        lv_obj_set_style_bg_opa(row, LV_OPA_COVER, LV_STATE_USER_1);
        lv_obj_remove_flag(row, LV_OBJ_FLAG_SCROLLABLE);
        uint32_t text = !a.enabled ? kDisabled : a.danger ? kBad : kText;
        char letter[2] = {char(std::toupper(int(a.letter))), 0};
        lv_obj_t* l = label(row, letter, &zs_font_12, a.enabled ? (a.danger ? kBad : kAccent) : kDisabled);
        lv_obj_set_style_text_color(l, c(kOnAccent), LV_STATE_USER_1);
        lv_obj_align(l, LV_ALIGN_LEFT_MID, 0, 0);
        lv_obj_t* t = label(row, a.label.c_str(), &zs_font_12, text);
        lv_obj_set_style_text_color(t, c(kOnAccent), LV_STATE_USER_1);
        lv_obj_align(t, LV_ALIGN_LEFT_MID, 14, 0);
        g_menu_rows.push_back(row);
    }
    lv_obj_update_layout(g_menu);
    lv_obj_align(g_menu, LV_ALIGN_BOTTOM_RIGHT, -4, -(kSoftkeyH + 2));
    g_sel = -1;
    menu_select(next_enabled(-1, +1));
    render_bar();
}

void actions_menu_close() {
    if (!g_menu) return;
    lv_obj_delete(g_menu);
    lv_obj_delete(g_dim);
    g_menu = g_dim = g_rows = nullptr;
    g_menu_rows.clear();
    g_sel = -1;
    render_bar();
}

bool actions_menu_is_open() { return g_menu != nullptr; }

bool actions_menu_key(uint32_t key) {
    if (!g_menu) return false;
    switch (key) {
        case LV_KEY_ESC:
        case kKeyNext:   // Tab again
            actions_menu_close();
            return true;
        case LV_KEY_UP:
            if (int j = next_enabled(g_sel, -1); j >= 0) menu_select(j);
            return true;
        case LV_KEY_DOWN:
            if (int j = next_enabled(g_sel, +1); j >= 0) menu_select(j);
            return true;
        case LV_KEY_ENTER:
        case '\r':
            if (g_sel >= 0) run_action(size_t(g_menu_items[size_t(g_sel)]));
            return true;
        default:
            break;
    }
    char letter = 0;
    if (key < 128 && std::isalpha(int(key))) letter = char(std::tolower(int(key)));
    else if (is_ctrl_key(key) && key != ctrl_key(' ')) letter = char(key - kKeyCtrl);
    if (letter) actions_run_letter(letter);
    // Everything else stays with the menu: nothing reaches the screen behind.
    return !is_shell_key(key) && key != ctrl_key(' ');
}

bool actions_run_letter(char letter) {
    for (size_t i = 0; i < g_actions.size(); ++i)
        if (in_menu(g_actions[i]) && g_actions[i].letter == letter) {
            run_action(i);
            return true;
        }
    return false;
}

}  // namespace zs
