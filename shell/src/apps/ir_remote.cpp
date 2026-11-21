// IR Remote: a universal remote built on Flipper-IRDB (docs/IR.md).
//
//   My remotes   ~/IR/*.ir: saved from the database, learned from your own
//                remotes (L relearns a button, A adds one, D deletes), or
//                .ir files copied from a Flipper Zero
//   Universal    every Power (Vol +, Mute, ...) code for TVs, projectors,
//                audio, ACs, fans and lights, most common first: Enter sends
//                them one after another; press any key when the device
//                reacts, then O opens that brand's remotes
//   Browse       2,000 remotes by kind, brand and model; type to search
#include <algorithm>
#include <functional>

#include "apps/common.h"
#include "ir/db.h"

namespace zs {
namespace {
using namespace theme;

std::string thousands(size_t n) {
    std::string s = std::to_string(n);
    for (int i = int(s.size()) - 3; i > 0; i -= 3) s.insert(size_t(i), ",");
    return s;
}

bool printable(uint32_t key) { return key >= 32 && key < 127 && key != '`'; }

class IrApp : public App {
public:
    void build(lv_obj_t* root) override {
        root_ = root;
        db_ = ir::db_root();
        index_ = ir::load_index(db_);
        universals_ = ir::universal_names(db_);
        show();
    }

    void tick() override {
        if (answered_) {   // a text box's answer, acted on outside its key event
            answered_ = false;
            Done d = std::move(done_);
            if (d) d(answer_);
            return;
        }
        if (page_.kind != Page::Universal || run_ != Run::Sending) return;
        // One code every 150 ms: time for the device to react, and for you
        // to press a key while the code that worked is still the last one.
        if (lv_tick_elaps(last_send_) < 150) return;
        last_send_ = lv_tick_get();
        auto& codes = uni_.codes[size_t(page_.focus)];
        if (pos_ >= int(codes.size())) {
            run_ = Run::Done;
            update_universal();
            return;
        }
        send(codes[size_t(pos_)]);
        update_universal();
        ++pos_;
    }

    bool on_key(uint32_t key) override {
        if (input_) return false;   // the text box has the keys
        switch (page_.kind) {
            case Page::Remote: return remote_key(key);
            case Page::Universal: return universal_key(key);
            case Page::Kinds:
            case Page::Brands:
            case Page::Models:
                if (printable(key) && (key != ' ' || !filter_.empty())) {
                    filter_ += char(key);
                    show();
                    return true;
                }
                if (key == LV_KEY_BACKSPACE && !filter_.empty()) {
                    filter_.pop_back();
                    show();
                    return true;
                }
                return false;
            default: return false;
        }
    }

    bool on_back() override {
        if (input_) {
            close_input();
            return true;
        }
        if (page_.kind == Page::Universal && run_ == Run::Sending) {
            run_ = Run::Stopped;
            update_universal();
            return true;
        }
        if (!filter_.empty()) {
            filter_.clear();
            show();
            return true;
        }
        if (stack_.empty()) return false;
        page_ = stack_.back();
        stack_.pop_back();
        show();
        return true;
    }

private:
    enum class Page { Home, Kinds, Brands, Models, Remote, Universal };
    enum class Run { Idle, Sending, Stopped, Done };
    struct Nav {
        Page kind = Page::Home;
        std::string category, brand;
        std::string path;        // Remote: full path; Universal: kind name
        bool mine = false;       // Remote: in ~/IR, so it can be changed
        int focus = 0;           // focused row or button, restored on Back
    };

    // --- navigation -------------------------------------------------------
    static Nav nav(Page kind, std::string category = "", std::string brand = "", std::string path = "",
                   bool mine = false) {
        Nav n;
        n.kind = kind;
        n.category = std::move(category);
        n.brand = std::move(brand);
        n.path = std::move(path);
        n.mine = mine;
        return n;
    }

    void go(Nav next) {
        page_.focus = focused_index();
        stack_.push_back(page_);
        page_ = std::move(next);
        filter_.clear();
        show();
    }

    int focused_index() {
        lv_obj_t* f = group ? lv_group_get_focused(group) : nullptr;
        for (size_t i = 0; i < focusables_.size(); ++i)
            if (focusables_[i] == f) return int(i);
        return 0;
    }

    void show() {
        run_ = Run::Idle;
        int want = page_.focus;   // building fires focus events that change it
        if (group) lv_group_set_editing(group, false);
        lv_obj_clean(root_);
        lv_group_remove_all_objs(group);
        actions_.clear();
        focusables_.clear();
        list_ = nullptr;
        info_ = status_ = bar_ = nullptr;
        input_ = ta_ = nullptr;   // deleted with the page
        switch (page_.kind) {
            case Page::Home: build_home(); break;
            case Page::Kinds: build_kinds(); break;
            case Page::Brands: build_brands(); break;
            case Page::Models: build_models(); break;
            case Page::Remote: build_remote(); break;
            case Page::Universal: build_universal(); break;
        }
        page_.focus = want;
        if (!focusables_.empty()) {
            int f = std::clamp(want, 0, int(focusables_.size()) - 1);
            lv_group_focus_obj(focusables_[size_t(f)]);
        }
        if (page_.kind == Page::Remote) show_info(std::clamp(want, 0, std::max(0, int(sigs_.size()) - 1)));
        if (page_.kind == Page::Universal) update_universal();
    }

    // A header line and a list under it.
    void list_page(const std::string& title, const std::string& right) {
        lv_obj_t* t = label(root_, title.c_str(), &zs_font_12, kText);
        lv_obj_set_pos(t, 6, 3);
        lv_obj_set_width(t, right.empty() ? 308 : 220);
        lv_label_set_long_mode(t, LV_LABEL_LONG_DOT);
        if (!right.empty()) {
            lv_obj_t* r = label(root_, right.c_str(), &zs_font_12, kMuted);
            lv_obj_align(r, LV_ALIGN_TOP_RIGHT, -6, 3);
        }
        list_ = make_list(root_, 0, 20, 320, kContentH - 20);
    }

    lv_obj_t* row(const char* symbol, const std::string& text, std::function<void()> action) {
        actions_.push_back(std::move(action));
        lv_obj_t* b = list_button(list_, symbol, text.c_str(), on_row, this);
        lv_obj_set_user_data(b, (void*)(intptr_t)(actions_.size() - 1));
        focusables_.push_back(b);
        return b;
    }

    void heading(const char* text) {
        lv_obj_t* t = lv_list_add_text(list_, text);
        heads_.push_back(t);
    }

    void finish() {
        finish_list(list_);
        for (auto* t : heads_) {   // section headings: plain muted text, not rows
            lv_obj_set_style_bg_opa(t, LV_OPA_TRANSP, 0);
            lv_obj_set_style_text_color(t, c(kMuted), 0);
            lv_obj_set_style_pad_ver(t, 2, 0);
        }
        heads_.clear();
    }

    std::string search_title(const std::string& normal) const {
        return filter_.empty() ? normal : std::string(ZS_ICON_SEARCH " ") + filter_ + "_";
    }

    // --- pages ------------------------------------------------------------
    void build_home() {
        list_page("IR Remote", index_.empty() ? "" : thousands(index_.size()) + " remotes");
        heading("My remotes");
        for (auto& p : ir::user_remotes())
            row(LV_SYMBOL_POWER, ir::stem(p), [this, p] { go(nav(Page::Remote, "", "", p, true)); });
        row(LV_SYMBOL_PLUS, "New remote (learn buttons)", [this] {
            ask("Name for the new remote", "", [this](const std::string& name) {
                std::string p = ir::user_remote_path(name);
                if (!ir::save(p, {})) {
                    shell_toast("Couldn't save to ~/IR");
                    return;
                }
                go(nav(Page::Remote, "", "", p, true));
            });
        });
        if (!universals_.empty()) heading("Universal: send every code");
        for (auto& u : universals_)
            row(LV_SYMBOL_SHUFFLE, ir::display(u), [this, u] { go(nav(Page::Universal, "", "", u, false)); });
        heading("Find your remote");
        if (index_.empty()) {
            heading("No code database installed (docs/IR.md)");
        } else {
            row(ZS_ICON_SEARCH, "Browse or search " + thousands(index_.size()) + " remotes",
                [this] { go(nav(Page::Kinds)); });
        }
        finish();
    }

    void build_kinds() {
        if (!filter_.empty()) {   // search everything: brand, model or kind
            int found = 0;
            list_page(search_title(""), "");
            for (auto& r : index_) {
                if (!ir::contains(r.brand + " " + r.model, filter_) && !ir::contains(ir::display(r.category), filter_))
                    continue;
                if (++found > 150) break;
                row(nullptr, r.brand + "  " + r.model + "  · " + ir::display(r.category),
                    [this, r] { go(nav(Page::Remote, r.category, r.brand, db_ + "/" + r.path, false)); });
            }
            if (!found) heading("Nothing found");
            finish();
            return;
        }
        list_page("Browse: type to search", "");
        std::vector<std::pair<std::string, int>> kinds;
        for (auto& r : index_) {
            if (kinds.empty() || kinds.back().first != r.category) kinds.push_back({r.category, 0});
            kinds.back().second++;
        }
        for (auto& k : kinds) {
            std::string cat = k.first;
            row(LV_SYMBOL_DIRECTORY, ir::display(cat) + "  (" + std::to_string(k.second) + ")",
                [this, cat] { go(nav(Page::Brands, cat)); });
        }
        finish();
    }

    void build_brands() {
        std::vector<std::pair<std::string, int>> brands;
        for (auto& r : index_) {
            if (r.category != page_.category) continue;
            if (brands.empty() || brands.back().first != r.brand) brands.push_back({r.brand, 0});
            brands.back().second++;
        }
        list_page(search_title(ir::display(page_.category)), std::to_string(brands.size()) + " brands");
        int shown = 0;
        for (auto& b : brands) {
            if (!filter_.empty() && !ir::contains(b.first, filter_)) continue;
            ++shown;
            std::string brand = b.first;
            row(nullptr, brand + (b.second > 1 ? "  (" + std::to_string(b.second) + ")" : ""),
                [this, brand] { go(nav(Page::Models, page_.category, brand)); });
        }
        if (!shown) heading("No brand matches; Backspace to edit");
        finish();
    }

    void build_models() {
        list_page(search_title(ir::display(page_.category) + "  " LV_SYMBOL_RIGHT "  " + page_.brand), "");
        int shown = 0;
        for (auto& r : index_) {
            if (r.category != page_.category || r.brand != page_.brand) continue;
            if (!filter_.empty() && !ir::contains(r.model, filter_)) continue;
            ++shown;
            row(nullptr, r.model + "  (" + std::to_string(r.buttons) + " buttons)",
                [this, r] { go(nav(Page::Remote, r.category, r.brand, db_ + "/" + r.path, false)); });
        }
        if (!shown) heading("No model matches; Backspace to edit");
        finish();
    }

    // The remote: a pad of its buttons, and a side panel.
    void build_remote() {
        sigs_.clear();
        ir::load(page_.path, sigs_);
        lv_obj_t* grid = lv_obj_create(root_);
        style_plain(grid);
        lv_obj_set_size(grid, 220, kContentH);
        lv_obj_set_pos(grid, 0, 0);
        lv_obj_set_flex_flow(grid, LV_FLEX_FLOW_ROW_WRAP);
        lv_obj_set_style_pad_all(grid, 4, 0);
        lv_obj_set_style_pad_gap(grid, 4, 0);
        lv_obj_set_scrollbar_mode(grid, LV_SCROLLBAR_MODE_ACTIVE);
        for (size_t i = 0; i < sigs_.size(); ++i) {
            lv_obj_t* b = lv_button_create(grid);
            lv_obj_set_size(b, 68, 30);
            lv_obj_set_style_radius(b, 8, 0);
            lv_obj_set_style_shadow_width(b, 0, 0);
            bool power = ir::contains(sigs_[i].name, "power") || ir::contains(sigs_[i].name, "off");
            lv_obj_set_style_bg_color(b, c(power ? 0x5a1d1d : kSurface), 0);
            lv_obj_set_style_bg_color(b, c(kAccent), LV_STATE_FOCUSED);
            lv_obj_set_style_text_color(b, c(kText), 0);
            lv_obj_set_style_text_color(b, c(kOnAccent), LV_STATE_FOCUSED);
            lv_obj_set_style_pad_hor(b, 3, 0);
            lv_obj_t* l = lv_label_create(b);
            lv_label_set_text(l, ir::pretty_name(sigs_[i].name).c_str());
            lv_obj_set_width(l, 62);
            lv_obj_set_style_text_align(l, LV_TEXT_ALIGN_CENTER, 0);
            lv_label_set_long_mode(l, LV_LABEL_LONG_DOT);
            lv_obj_center(l);
            lv_obj_set_user_data(b, (void*)(intptr_t)i);
            lv_obj_add_event_cb(b, on_pad, LV_EVENT_CLICKED, this);
            lv_obj_add_event_cb(b, on_pad_focus, LV_EVENT_FOCUSED, this);
            shell_add_focus(b);
            focusables_.push_back(b);
        }
        if (sigs_.empty()) {
            lv_obj_t* e = label(grid, page_.mine ? "No buttons yet.\n\nPress A to add one:\nname it, then point\nyour old remote at\nthe Cardputer and\npress its button."
                                                 : "This remote has no\nbuttons ZeroOS can send.",
                                &zs_font_12, kMuted);
            lv_obj_set_width(e, 200);
        }

        lv_obj_t* side = lv_obj_create(root_);
        style_panel(side);
        lv_obj_set_pos(side, 222, 4);
        lv_obj_set_size(side, 94, kContentH - 8);
        lv_obj_remove_flag(side, LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_set_style_pad_all(side, 5, 0);
        lv_obj_t* t = label(side, page_.mine ? ir::stem(page_.path).c_str() : page_.brand.c_str(), &zs_font_12, kMuted);
        lv_obj_set_width(t, 84);
        lv_label_set_long_mode(t, LV_LABEL_LONG_DOT);
        info_ = label(side, "", &zs_font_12, kText);
        lv_obj_set_width(info_, 84);
        lv_obj_set_pos(info_, 0, 16);
        lv_label_set_long_mode(info_, LV_LABEL_LONG_WRAP);
        status_ = label(side, "", &zs_font_12, kGood);
        lv_obj_set_pos(status_, 0, 64);
        const char* hint = page_.mine ? "Enter send\nL learn  A add\nD delete" : "Enter send\nS save to\nMy remotes";
        lv_obj_t* h = label(side, hint, &zs_font_12, kMuted);
        lv_obj_align(h, LV_ALIGN_BOTTOM_LEFT, 0, 0);
        show_info(std::clamp(page_.focus, 0, std::max(0, int(sigs_.size()) - 1)));
    }

    void show_info(int i) {
        if (!info_) return;   // focus events fire while the pad is being built
        if (i < 0 || i >= int(sigs_.size())) {
            lv_label_set_text(info_, "");
            return;
        }
        auto& s = sigs_[size_t(i)];
        lv_label_set_text(info_, (ir::pretty_name(s.name) + "\n" + ir::describe(s)).c_str());
        if (status_) lv_label_set_text(status_, "");
    }

    bool remote_key(uint32_t key) {
        int n = int(focusables_.size());
        int i = focused_index();
        const int cols = 3;
        switch (key) {
            case LV_KEY_RIGHT: if (n) lv_group_focus_obj(focusables_[size_t((i + 1) % n)]); return true;
            case LV_KEY_LEFT: if (n) lv_group_focus_obj(focusables_[size_t((i + n - 1) % n)]); return true;
            case LV_KEY_DOWN: if (n) lv_group_focus_obj(focusables_[size_t(std::min(n - 1, i + cols))]); return true;
            case LV_KEY_UP: if (n) lv_group_focus_obj(focusables_[size_t(std::max(0, i - cols))]); return true;
        }
        if (key != 'd' && key != 'D' && key != LV_KEY_DEL && key != LV_KEY_BACKSPACE) delete_armed_ = -1;
        if (!page_.mine) {
            if (key == 's' || key == 'S') {
                std::string name = page_.brand + " " + ir::stem(page_.path);
                std::string p = ir::user_remote_path(name);
                std::string rel = page_.path.rfind(db_ + "/", 0) == 0 ? page_.path.substr(db_.size() + 1) : page_.path;
                if (ir::save(p, sigs_, "From Flipper-IRDB: " + rel)) shell_toast("Saved to My remotes: " + ir::stem(p));
                else shell_toast("Couldn't save to ~/IR");
                return true;
            }
            return false;
        }
        switch (key) {
            case 'l':
            case 'L':
                if (n) learn(i, sigs_[size_t(i)].name);
                return true;
            case 'a':
            case 'A':
                ask("Name for the new button", "", [this](const std::string& name) {
                    if (!name.empty()) learn(int(sigs_.size()), name);
                });
                return true;
            case 'd':
            case 'D':
            case LV_KEY_DEL:
            case LV_KEY_BACKSPACE:
                if (!n) return true;
                if (delete_armed_ != i) {
                    delete_armed_ = i;
                    shell_toast("Press D again to delete " + ir::pretty_name(sigs_[size_t(i)].name));
                    return true;
                }
                delete_armed_ = -1;
                sigs_.erase(sigs_.begin() + i);
                save_mine(std::max(0, i - 1));
                return true;
        }
        return false;
    }

    // Learn into button i (a new one when i == size).
    void learn(int i, const std::string& name) {
        shell_toast("Point your remote at the Cardputer and press " + ir::pretty_name(name));
        lv_refr_now(nullptr);
        std::vector<uint32_t> t;
        if (!hal().ir_learn(t, 5000)) {
            shell_toast("Nothing received");
            return;
        }
        ir::Signal s = ir::from_capture(t);
        s.name = name;
        if (i < int(sigs_.size())) sigs_[size_t(i)] = s;
        else sigs_.push_back(s);
        save_mine(i);
        shell_toast("Learned " + ir::pretty_name(name) + (s.raw ? "" : " (" + s.protocol + ")"));
    }

    void save_mine(int focus) {
        if (!ir::save(page_.path, sigs_)) shell_toast("Couldn't save " + page_.path);
        page_.focus = focus;
        show();
    }

    bool send(const ir::Signal& s) {
        ir::Wave w;
        return ir::encode(s, w) && hal().ir_send(w);
    }

    // Universal: the buttons on the left, what is being sent on the right.
    void build_universal() {
        ir::load_universal(db_, page_.path, uni_);
        list_ = make_list(root_, 0, 0, 132, kContentH);
        for (size_t i = 0; i < uni_.buttons.size(); ++i) {
            row(nullptr, ir::pretty_name(uni_.buttons[i]) + "  " + std::to_string(uni_.codes[i].size()), [this, i] {
                if (page_.focus != int(i) || run_ == Run::Idle || run_ == Run::Done) {
                    page_.focus = int(i);
                    pos_ = 0;
                    run_ = Run::Sending;
                } else if (run_ == Run::Stopped) {   // Enter again: resend the one it stopped on
                    send(uni_.codes[i][size_t(pos_)]);
                } else {
                    run_ = Run::Stopped;
                }
                update_universal();
            });
            lv_obj_add_event_cb(focusables_.back(), on_uni_focus, LV_EVENT_FOCUSED, this);
        }
        finish();
        lv_obj_t* side = lv_obj_create(root_);
        style_panel(side);
        lv_obj_set_pos(side, 136, 4);
        lv_obj_set_size(side, 180, kContentH - 8);
        lv_obj_remove_flag(side, LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_set_style_pad_all(side, 6, 0);
        info_ = label(side, "", &zs_font_16, kText);
        lv_obj_set_width(info_, 166);
        lv_label_set_long_mode(info_, LV_LABEL_LONG_DOT);
        bar_ = thin_bar(side, 166, 6, kAccent);
        lv_obj_set_pos(bar_, 0, 24);
        status_ = label(side, "", &zs_font_12, kMuted);
        lv_obj_set_pos(status_, 0, 36);
        lv_obj_set_width(status_, 166);
        lv_label_set_long_mode(status_, LV_LABEL_LONG_WRAP);
        update_universal();
    }

    void update_universal() {
        if (!info_ || uni_.buttons.empty()) return;
        size_t b = size_t(std::clamp(page_.focus, 0, int(uni_.buttons.size()) - 1));
        auto& codes = uni_.codes[b];
        std::string button = ir::pretty_name(uni_.buttons[b]);
        int n = int(codes.size());
        int at = n ? std::clamp(pos_, 0, n - 1) : 0;
        std::string brand = n ? ir::source_brand(codes[size_t(at)].source) : "";
        lv_bar_set_range(bar_, 0, std::max(1, n));
        switch (run_) {
            case Run::Idle:
                lv_bar_set_value(bar_, 0, LV_ANIM_OFF);
                lv_label_set_text(info_, (ir::display(uni_.name) + ": " + button).c_str());
                lv_label_set_text(status_, fmt("Point at the device and press Enter: %d codes go out, most common "
                                               "first. Press any key as soon as it reacts.", n).c_str());
                break;
            case Run::Sending:
                lv_bar_set_value(bar_, pos_ + 1, LV_ANIM_OFF);
                lv_label_set_text(info_, fmt("%s  %d / %d", button.c_str(), at + 1, n).c_str());
                lv_label_set_text(status_, (brand + "\n\nAny key: stop").c_str());
                break;
            case Run::Stopped:
                lv_bar_set_value(bar_, at + 1, LV_ANIM_OFF);
                lv_label_set_text(info_, fmt("Stopped at %d / %d", at + 1, n).c_str());
                lv_label_set_text(status_, (brand + "\nLeft / Right try the codes around it, Enter sends again, O "
                                            "opens " + brand + " remotes").c_str());
                break;
            case Run::Done:
                lv_bar_set_value(bar_, n, LV_ANIM_OFF);
                lv_label_set_text(info_, fmt("Sent all %d", n).c_str());
                lv_label_set_text(status_, "No reaction? Go closer and aim at the device's front, or find its "
                                           "remote under Browse.");
                break;
        }
    }

    bool universal_key(uint32_t key) {
        if (run_ == Run::Sending) {   // any key stops; the last code sent is probably the one
            if (key == LV_KEY_UP || key == LV_KEY_DOWN || printable(key) || key == LV_KEY_ENTER ||
                key == LV_KEY_LEFT || key == LV_KEY_RIGHT) {
                pos_ = std::max(0, pos_ - 1);
                run_ = Run::Stopped;
                update_universal();
                return true;
            }
            return false;
        }
        if (run_ != Run::Stopped || uni_.buttons.empty()) return false;
        auto& codes = uni_.codes[size_t(page_.focus)];
        int n = int(codes.size());
        if (key == LV_KEY_LEFT || key == LV_KEY_RIGHT) {
            pos_ = std::clamp(pos_ + (key == LV_KEY_RIGHT ? 1 : -1), 0, n - 1);
            send(codes[size_t(pos_)]);
            update_universal();
            return true;
        }
        if ((key == 'o' || key == 'O') && n) {
            const std::string& src = codes[size_t(pos_)].source;
            std::string cat = src.substr(0, src.find('/'));
            std::string brand = ir::source_brand(src);
            if (!brand.empty()) go(nav(Page::Models, cat, brand));
            return true;
        }
        return false;
    }

    // --- text input (as in Media): Enter accepts, Esc cancels -------------
    using Done = std::function<void(const std::string&)>;
    void ask(const char* title, const std::string& initial, Done done) {
        done_ = std::move(done);
        input_ = lv_obj_create(root_);
        style_panel(input_);
        lv_obj_set_size(input_, 300, 74);
        lv_obj_center(input_);
        lv_obj_set_style_border_color(input_, c(kAccent), 0);
        lv_obj_set_style_border_width(input_, 1, 0);
        label(input_, title, &zs_font_12, kMuted);
        lv_obj_t* ta = ta_ = lv_textarea_create(input_);
        lv_textarea_set_one_line(ta, true);
        lv_textarea_set_max_length(ta, 40);
        lv_textarea_set_text(ta, initial.c_str());
        lv_obj_set_width(ta, 280);
        lv_obj_align(ta, LV_ALIGN_BOTTOM_MID, 0, 0);
        lv_obj_add_event_cb(ta, [](lv_event_t* e) {
            auto* self = static_cast<IrApp*>(lv_event_get_user_data(e));
            self->answer_ = lv_textarea_get_text(lv_event_get_target_obj(e));
            self->answered_ = true;   // tick() acts on it: that may rebuild the page,
                                      // which can't happen inside this key event
            // Enter is still down: without this its release would also
            // "click" whichever button gets the focus next.
            if (lv_indev_t* in = lv_indev_active()) lv_indev_wait_release(in);
            self->close_input();
        }, LV_EVENT_READY, this);
        lv_group_add_obj(group, ta);
        lv_group_focus_obj(ta);
        lv_group_set_editing(group, true);
    }

    // The box is hidden, not deleted, as its own Enter event is still
    // running; the next page rebuild deletes it with the rest.
    void close_input() {
        if (!input_) return;
        if (!answered_) done_ = nullptr;   // cancelled
        lv_group_set_editing(group, false);
        if (ta_) lv_group_remove_obj(ta_);
        lv_obj_add_flag(input_, LV_OBJ_FLAG_HIDDEN);
        input_ = ta_ = nullptr;
        if (!focusables_.empty())
            lv_group_focus_obj(focusables_[size_t(std::clamp(page_.focus, 0, int(focusables_.size()) - 1))]);
    }

    // --- events -----------------------------------------------------------
    static void on_row(lv_event_t* e) {
        auto* self = static_cast<IrApp*>(lv_event_get_user_data(e));
        size_t i = size_t((intptr_t)lv_obj_get_user_data(lv_event_get_target_obj(e)));
        if (i < self->actions_.size()) {
            auto action = self->actions_[i];   // the page may be rebuilt inside
            action();
        }
    }
    static void on_pad(lv_event_t* e) {
        auto* self = static_cast<IrApp*>(lv_event_get_user_data(e));
        size_t i = size_t((intptr_t)lv_obj_get_user_data(lv_event_get_target_obj(e)));
        if (i >= self->sigs_.size()) return;
        bool ok = self->send(self->sigs_[i]);
        if (self->status_) {
            lv_label_set_text(self->status_, ok ? LV_SYMBOL_OK " Sent" : LV_SYMBOL_WARNING " Not sent");
            lv_obj_set_style_text_color(self->status_, c(ok ? kGood : kBad), 0);
        }
    }
    static void on_pad_focus(lv_event_t* e) {
        auto* self = static_cast<IrApp*>(lv_event_get_user_data(e));
        self->page_.focus = int((intptr_t)lv_obj_get_user_data(lv_event_get_target_obj(e)));
        self->show_info(self->page_.focus);
    }
    static void on_uni_focus(lv_event_t* e) {
        auto* self = static_cast<IrApp*>(lv_event_get_user_data(e));
        int i = self->focused_index();
        if (i == self->page_.focus && self->run_ != Run::Idle) return;
        self->page_.focus = i;
        self->run_ = Run::Idle;
        self->pos_ = 0;
        self->update_universal();
    }

    lv_obj_t* root_ = nullptr;
    std::string db_;
    std::vector<ir::RemoteRef> index_;
    std::vector<std::string> universals_;

    Nav page_;
    std::vector<Nav> stack_;
    std::string filter_;

    lv_obj_t* list_ = nullptr;
    std::vector<lv_obj_t*> heads_;
    std::vector<lv_obj_t*> focusables_;
    std::vector<std::function<void()>> actions_;
    lv_obj_t* info_ = nullptr;
    lv_obj_t* status_ = nullptr;
    lv_obj_t* bar_ = nullptr;

    std::vector<ir::Signal> sigs_;   // the open remote
    int delete_armed_ = -1;

    ir::Universal uni_;
    Run run_ = Run::Idle;
    int pos_ = 0;

    uint32_t last_send_ = 0;

    lv_obj_t* input_ = nullptr;
    lv_obj_t* ta_ = nullptr;
    Done done_;
    std::string answer_;
    bool answered_ = false;
};
}  // namespace

std::unique_ptr<App> make_ir_app() { return std::make_unique<IrApp>(); }

}  // namespace zs
