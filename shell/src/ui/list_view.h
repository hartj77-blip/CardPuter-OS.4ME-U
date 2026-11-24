// A list screen (docs/UX.md): rows with their number key showing (1 to 9 pick
// the first nine rows, like a phone menu), and typing filters them. The app
// passes its keys to key() first; Up/Down, Page Up/Down, Enter and the
// numbers are handled there.
//
//   ListView list_;                       // an App member
//   list_.build(root, 4, 4, kScreenW - 8, kBodyH - 8);
//   list_.add(LV_SYMBOL_WIFI, "HomeNet", "72%", [this] { join(0); });
//   list_.finish();
//   bool on_key(uint32_t k) override { return list_.key(k); }
//
// Only the rows that fit on the screen have widgets. add() only stores a
// row's text and action; build() makes one widget per visible line (about
// six on the 170 px screen) and moving through the list puts other rows'
// text into those same widgets. A list of 200 apps costs the same LVGL
// objects and memory as a list of six, and opening it takes the same time.
#pragma once

#include <cstdint>
#include <functional>
#include <string>
#include <vector>

#include "lvgl.h"

namespace zs {

class ListView {
public:
    ListView() = default;
    ListView(const ListView&) = delete;
    ListView& operator=(const ListView&) = delete;
    ~ListView();

    // Forget the rows (their widgets were deleted with the screen), and any
    // row action still waiting to run.
    void reset();
    // The list, in `parent` at x, y, w x h. Building again starts it afresh.
    void build(lv_obj_t* parent, int x, int y, int w, int h);
    // A row: an icon (LV_SYMBOL_* / ZS_ICON_* or nullptr), its text, a value
    // on the right (muted, may be ""), and what Enter on it does. on_enter
    // runs after the key is handled, so it may rebuild the screen. Returns
    // the row's index, the one focused() and set_value() use.
    int add(const char* icon, const std::string& text, const std::string& value, std::function<void()> on_enter);
    // A value row: Left/Right step through `choices` (Enter steps forward),
    // on_change(index) follows each step. A switch is {"Off", "On"}.
    int add_choice(const char* icon, const std::string& text, std::vector<std::string> choices, int at,
                   std::function<void(int)> on_change);
    // Show a row's value anew (its state changed elsewhere), or in a colour
    // of its own (theme kGood for "Installed", kAccent for "Update").
    void set_value(int row, const std::string& value);
    void set_value_color(int row, uint32_t color);
    // Focus a row (and scroll to it), as the index add() returned it in.
    void focus(int row);
    // The focused row is a value row (Left/Right change it).
    bool focused_is_choice() const;
    // What shows when there are no rows, or none match the filter.
    void set_empty_text(const std::string& text) { empty_text_ = text; }
    // After the rows: focus the first and number them.
    void finish();

    // Typing: 1 to 9 pick a row; letters, space and punctuation filter the
    // rows, Backspace takes the last one off. Up/Down and Page Up/Down move
    // through the rows. True if it used the key; Backspace with no filter
    // is left to the app (Files: up a folder).
    bool key(uint32_t key);
    const std::string& filter() const { return filter_; }
    void clear_filter();
    // The row in focus, as the index add() returned it in; -1 if none.
    int focused() const;
    int size() const { return int(rows_.size()); }

private:
    // A row's data. No widgets: those belong to the slots.
    struct Row {
        std::string icon;                    // "" for none
        std::string text;
        std::string value;
        uint32_t value_color = 0;
        bool own_color = false;              // set_value_color() was called
        int value_room = 0;                  // px kept for the value (widest choice)
        std::function<void()> on_enter;
        std::vector<std::string> choices;    // a value row's values
        int at = 0;
        std::function<void(int)> on_change;
    };
    // One line of widgets on the screen. It shows the row at shown_[top_ + its index].
    struct Slot {
        lv_obj_t* obj = nullptr;
        lv_obj_t* badge = nullptr;
        lv_obj_t* num = nullptr;
        lv_obj_t* icon = nullptr;
        lv_obj_t* text = nullptr;
        lv_obj_t* value = nullptr;
    };
    void make_slot(int i, bool focusable);
    void bind(int slot);
    void bind_all();
    int slot_of(int row) const;          // -1 when the row isn't on the screen
    int focused_slot() const;
    void move_to(int pos);               // focus shown_[pos], scrolling as needed
    void scroll_bar();
    bool only_list_in_group() const;
    void step(int row, int dir);
    void apply_filter();
    void run_later(int row);
    static void clicked_cb(lv_event_t* e);
    static void focused_cb(lv_event_t* e);
    static void run_cb(void* self);

    lv_obj_t* list_ = nullptr;
    lv_obj_t* chip_ = nullptr;    // the filter, top right
    lv_obj_t* empty_ = nullptr;
    lv_obj_t* bar_ = nullptr;     // where the screen is in a long list
    std::vector<Row> rows_;
    std::vector<int> shown_;      // the rows the filter leaves, in order
    std::vector<Slot> slots_;
    int page_ = 1;                // whole rows that fit (focusable slots)
    int top_ = 0;                 // index in shown_ of the first slot's row
    int cursor_ = -1;             // index in shown_ of the focused row
    int width_ = 0;
    std::string filter_;
    std::string empty_text_ = "Nothing here";
    int pending_ = -1;            // row whose on_enter runs next
};

}  // namespace zs
