#include "ui/suggest_bar.h"

#include <sys/stat.h>

#include <cstdlib>
#include <cstring>
#include <string>

#include "platform/keys.h"
#include "ui/prefs.h"
#include "ui/suggest.h"
#include "ui/theme.h"

namespace zs {
namespace {
using namespace theme;

constexpr uint32_t kSaveAfterMs = 30000;   // learned words reach the card this long after a change

lv_obj_t* g_bar = nullptr;
lv_obj_t* g_label[suggest::kMaxResults] = {};

lv_obj_t* g_ta = nullptr;          // the text box the suggestions are for
std::string g_prefix;              // what is typed of the word so far
std::string g_words[suggest::kMaxResults];
int g_n = 0;                       // suggestions showing
int g_sel = 0;                     // the one Tab takes next
size_t g_accept_len = 0;           // bytes the last Tab typed (Tab again swaps them, Backspace removes them)
std::string g_pending;             // the word Tab typed, learned once it is kept
bool g_auto_space = false;         // the space before the cursor was typed by Tab
bool g_internal = false;           // this file is changing the text: its own hook stays out
std::string g_prev_word;           // the word before the cursor after the last change
size_t g_prev_len = 0;             // and how long the text was
int g_enabled = -1;                // -1: not read from the settings yet
uint32_t g_dirty_since = 0;

std::string config_dir() {
    const char* h = std::getenv("HOME");
    return std::string(h ? h : "/tmp") + "/.config/zeroos";
}

size_t cursor_bytes(lv_obj_t* ta, const char* text) {
    uint32_t chars = lv_textarea_get_cursor_pos(ta);
    size_t i = 0;
    while (text[i] && chars > 0) {
        ++i;
        while ((static_cast<unsigned char>(text[i]) & 0xC0) == 0x80) ++i;
        --chars;
    }
    return i;
}

void render() {
    if (!g_bar) return;
    if (g_n == 0) {
        lv_obj_add_flag(g_bar, LV_OBJ_FLAG_HIDDEN);
        return;
    }
    for (int i = 0; i < suggest::kMaxResults; ++i) {
        if (i < g_n) {
            // As it will be typed: the letters already there, then the rest.
            lv_label_set_text(g_label[i], (g_prefix + suggest::remainder(g_prefix, g_words[i])).c_str());
            lv_obj_set_style_text_color(g_label[i], c(i == g_sel ? kText : kMuted), 0);
            lv_obj_remove_flag(g_label[i], LV_OBJ_FLAG_HIDDEN);
        } else {
            lv_obj_add_flag(g_label[i], LV_OBJ_FLAG_HIDDEN);
        }
    }
    lv_obj_remove_flag(g_bar, LV_OBJ_FLAG_HIDDEN);
    lv_obj_move_foreground(g_bar);
}

// The word Tab typed stays: count it.
void commit() {
    if (!g_pending.empty()) suggest::learn(g_pending);
    g_pending.clear();
    g_accept_len = 0;
}

void clear() {
    commit();
    g_n = 0;
    g_auto_space = false;
    render();
}

// The text or the cursor is somewhere new: what could the word be now?
void refresh(lv_obj_t* ta) {
    const char* text = lv_textarea_get_text(ta);
    g_ta = ta;
    g_prev_len = std::strlen(text);
    g_prev_word = suggest::word_before(text, cursor_bytes(ta, text));
    g_prefix = g_prev_word;
    g_n = suggest::lookup(g_prefix, g_words);
    g_sel = 0;
    render();
}

void on_event(lv_event_t* e) {
    lv_obj_t* ta = lv_event_get_target_obj(e);
    switch (lv_event_get_code(e)) {
        case LV_EVENT_VALUE_CHANGED: {
            if (g_internal || !suggest_enabled()) break;
            commit();
            g_auto_space = false;
            // One character more, and it ended a word: that word was typed
            // in full, so count it.
            const char* text = lv_textarea_get_text(ta);
            size_t cur = cursor_bytes(ta, text);
            if (ta == g_ta && !g_prev_word.empty() && std::strlen(text) == g_prev_len + 1 && cur > 0 &&
                suggest::word_before(text, cur).empty())
                suggest::learn(g_prev_word);
            refresh(ta);
            break;
        }
        case LV_EVENT_READY:   // Enter in a one-line box: the last word has no space after it
            if (suggest_enabled()) {
                const char* text = lv_textarea_get_text(ta);
                commit();
                suggest::learn(suggest::word_before(text, cursor_bytes(ta, text)));
            }
            clear();
            break;
        case LV_EVENT_DEFOCUSED:
        case LV_EVENT_DELETE:
            if (ta == g_ta) {
                clear();
                g_ta = nullptr;
                g_prev_word.clear();
            }
            break;
        default: break;
    }
}

// Listen to a text box the first time it is typed in. It costs one event
// entry on the box, freed with it.
void hook(lv_obj_t* ta) {
    uint32_t n = lv_obj_get_event_count(ta);
    for (uint32_t i = 0; i < n; ++i)
        if (lv_event_dsc_get_cb(lv_obj_get_event_dsc(ta, i)) == on_event) return;
    lv_obj_add_event_cb(ta, on_event, LV_EVENT_ALL, nullptr);
}

void erase(lv_obj_t* ta, size_t bytes) {
    for (size_t i = 0; i < bytes; ++i) lv_textarea_delete_char(ta);   // what Tab types is plain ASCII
}

void accept(lv_obj_t* ta) {
    std::string rest = suggest::remainder(g_prefix, g_words[g_sel]);
    g_internal = true;
    erase(ta, g_accept_len);   // Tab again: the next word replaces the one just typed
    size_t before = std::strlen(lv_textarea_get_text(ta));
    lv_textarea_add_text(ta, (rest + " ").c_str());
    size_t after = std::strlen(lv_textarea_get_text(ta));
    g_internal = false;
    g_accept_len = after > before ? after - before : 0;   // a box can refuse characters or be full
    g_pending = g_prefix + rest;
    g_auto_space = true;
    g_prev_word.clear();
    g_prev_len = after;
    if (g_n == 1) {
        // Nothing else to offer: Tab is the menu again straight away.
        g_n = 0;
        size_t typed = g_accept_len;
        commit();
        g_accept_len = typed;   // Backspace still takes it back
    } else {
        g_sel = (g_sel + 1) % g_n;
    }
    render();
    // The app's own listeners (Notes saves) heard the change from LVGL.
}

}  // namespace

void suggest_build(lv_obj_t* screen) {
    g_bar = lv_obj_create(screen);
    style_plain(g_bar);
    lv_obj_set_size(g_bar, LV_SIZE_CONTENT, kStatusH);
    lv_obj_set_style_max_width(g_bar, kScreenW, 0);
    lv_obj_set_pos(g_bar, 0, 0);
    lv_obj_set_style_bg_color(g_bar, c(kTopBar), 0);
    lv_obj_set_style_bg_opa(g_bar, LV_OPA_COVER, 0);
    lv_obj_set_style_pad_left(g_bar, 5, 0);
    lv_obj_set_style_pad_right(g_bar, 10, 0);
    lv_obj_set_style_pad_column(g_bar, 9, 0);
    lv_obj_set_flex_flow(g_bar, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(g_bar, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_remove_flag(g_bar, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_remove_flag(g_bar, LV_OBJ_FLAG_CLICKABLE);

    lv_obj_t* chip = lv_obj_create(g_bar);
    style_plain(chip);
    lv_obj_set_size(chip, LV_SIZE_CONTENT, 14);
    lv_obj_set_style_pad_hor(chip, 4, 0);
    lv_obj_set_style_radius(chip, 3, 0);
    lv_obj_set_style_bg_color(chip, c(kAccent), 0);
    lv_obj_set_style_bg_opa(chip, LV_OPA_COVER, 0);
    lv_obj_remove_flag(chip, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_center(label(chip, "Tab", &zs_font_12, kOnAccent));

    for (auto& l : g_label) l = label(g_bar, "", &zs_font_12, kMuted);
    lv_obj_add_flag(g_bar, LV_OBJ_FLAG_HIDDEN);

    suggest::load(config_dir() + "/words.learned");
}

bool suggest_key(uint32_t key, lv_obj_t* ta) {
    if (!ta || !suggest_enabled() || lv_textarea_get_password_mode(ta) || lv_obj_has_flag(ta, kNoSuggestFlag)) {
        clear();
        return false;
    }
    hook(ta);
    if (key == kKeyNext && g_n > 0 && ta == g_ta) {
        accept(ta);
        return true;
    }
    if (key == kKeyBackspace && g_accept_len > 0 && ta == g_ta) {
        // Backspace straight after Tab: take the whole suggestion back.
        g_internal = true;
        erase(ta, g_accept_len);
        g_internal = false;
        g_pending.clear();
        g_accept_len = 0;
        g_auto_space = false;
        refresh(ta);
        return true;
    }
    if (g_auto_space && key > 32 && key < 128 && std::strchr(".,!?;:", int(key))) {
        // "word " then a full stop: the space Tab typed goes, as on a phone.
        const char* text = lv_textarea_get_text(ta);
        size_t cur = cursor_bytes(ta, text);
        if (cur > 0 && text[cur - 1] == ' ') {
            g_internal = true;
            lv_textarea_delete_char(ta);
            g_internal = false;
        }
        clear();
        return false;   // the text box types the key itself
    }
    // The cursor is about to move, or the key isn't typing: what is offered
    // no longer fits. Typing brings it back.
    if ((key < 32 && key != kKeyBackspace) || key == kKeyDel || key >= 0xE000) clear();
    return false;
}

void suggest_tick() {
    if (!suggest::dirty()) {
        g_dirty_since = 0;
    } else if (!g_dirty_since) {
        g_dirty_since = lv_tick_get() | 1;   // 0 means "nothing to save"
    } else if (lv_tick_elaps(g_dirty_since) > kSaveAfterMs) {
        suggest_flush();
    }
}

void suggest_flush() {
    g_dirty_since = 0;
    if (!suggest::dirty()) return;
    std::string d = config_dir();
    mkdir(d.substr(0, d.rfind('/')).c_str(), 0755);
    mkdir(d.c_str(), 0755);
    suggest::save();
}

bool suggest_enabled() {
    if (g_enabled < 0) g_enabled = prefs::get_int("suggest", 1) ? 1 : 0;
    return g_enabled == 1;
}

void suggest_set_enabled(bool on) {
    g_enabled = on ? 1 : 0;
    prefs::set_int("suggest", g_enabled);
    if (!on) clear();
}

void suggest_forget() {
    clear();
    suggest::forget();
    suggest_flush();
}

}  // namespace zs
