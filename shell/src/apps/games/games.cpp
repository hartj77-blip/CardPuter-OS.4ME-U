// Libretro: play your own games for classic consoles on the built-in screen, with
// libretro emulator cores (the ones RetroArch uses). See docs/GAMES.md.
//
//   Library   Enter a system      C cores
//   Games     Enter play
//   Cores     Enter download (from libretro)      Del remove a download
//   Playing   W A S D / arrows, K = A, J = B, I = X, U = Y, Q/E = L/R,
//             Enter = Start, Space = Select, Esc = menu (save, load, quit)
//
// Games go in ~/Games/<system>/ (nes, gb, gbc, gba, snes, md...): copy them
// with Files, Sync, Taildrop or scp. Only use games you own.
#include <string>
#include <vector>

#include "apps/common.h"
#include "apps/games/emu.h"
#include "apps/games/systems.h"
#include "apps/kit/run.h"
#include "apps/kit/ui.h"
#include "ui/power.h"

namespace zs {
namespace {
using namespace theme;
using namespace games;

class GamesApp : public App {
public:
    ~GamesApp() override { stop_game(); }

    void build(lv_obj_t* root) override {
        root_ = root;
        kit::make_dirs(games_dir());
        for (auto& s : systems())
            if (!s.hidden) kit::make_dirs(games_dir() + "/" + s.folder);
        show_library();
    }

    void tick() override {
        if (pending_load_) {   // "Starting..." is on screen: now load
            pending_load_ = false;
            start_game();
            return;
        }
        if (pending_row_ >= 0) {
            int i = pending_row_;
            pending_row_ = -1;
            picked(i);
            return;
        }
        if (page_ == Page::Playing) {
            if (emu_.quit_requested()) { stop_game(); return; }
            // Face down, or the screen went off: pause, so nothing runs unseen.
            if (!menu_ && power().screen_off()) open_menu();
            if (emu_.pad().take_menu()) {
                if (menu_) close_menu();
                else open_menu();
            }
        }
        if (page_ == Page::Cores && dl_.started()) {
            std::vector<std::string> lines;
            dl_.drain(lines);
            if (!dl_.running()) {
                bool ok = dl_.status() == kit::Done::Ok;
                std::string name = dl_name_;
                dl_.reset();
                if (kit::simulated() && ok) sim_downloaded_.push_back(name);
                shell_toast(ok ? name + " is ready" : "Download failed: online? (needs curl and unzip)");
                show_cores();
            }
        }
    }

    bool on_key(uint32_t key) override {
        if (page_ == Page::Playing) {
            if (key == LV_KEY_ESC) {
                if (menu_) close_menu();
                else open_menu();
                return true;
            }
            if (menu_) return false;   // the menu list takes Up/Down/Enter
            if (!emu_.pad().has_devices()) emu_.pad().press_key(key);
            return true;               // everything else is the game's
        }
        if (page_ == Page::Library || page_ == Page::Games) {
            if (key == 'c' || key == 'C') { show_cores(); return true; }
        }
        if (page_ == Page::Cores && (key == LV_KEY_DEL || key == LV_KEY_BACKSPACE)) {
            remove_core();
            return true;
        }
        return false;
    }

    bool on_back() override {
        switch (page_) {
            case Page::Library: return false;
            case Page::Games: show_library(); return true;
            case Page::Cores: dl_.reset(); show_library(); return true;
            case Page::Playing: return true;   // handled in on_key
        }
        return false;
    }

private:
    enum class Page { Library, Games, Cores, Playing };

    void clear() {
        lv_obj_clean(root_);
        if (group) lv_group_remove_all_objs(group);
        rows_.clear();
    }

    static void on_click(lv_event_t* e) {
        auto* self = static_cast<GamesApp*>(lv_event_get_user_data(e));
        self->pending_row_ = int(lv_obj_get_index(lv_event_get_target_obj(e)));
    }

    // ---- Library: the systems that have games
    void show_library() {
        page_ = Page::Library;
        clear();
        kit::title_row(root_, "Libretro", "C cores");
        std::vector<const System*> have;
        for (auto& s : systems())
            if (!games_for(s).empty()) have.push_back(&s);
        if (have.empty()) {
            lv_obj_t* l = label(root_,
                                "No games yet.\n\n"
                                "Copy your games into ~/Games/<system>/ with Files,\n"
                                "Sync, Taildrop or scp: nes, gb, gbc, gba, snes,\n"
                                "md, sms, pce, a26, psx, ws.\n\n"
                                "Press C to get emulator cores for more systems.",
                                &zs_font_12, kMuted);
            lv_obj_set_pos(l, 10, 26);
            return;
        }
        lv_obj_t* list = make_list(root_, 0, 18, kScreenW, kContentH - 18);
        for (const System* s : have) {
            size_t n = games_for(*s).size();
            bool core = !core_for(*s).empty();
            std::string text = std::string(s->name) + fmt("   %d game%s", int(n), n == 1 ? "" : "s") +
                               (core ? "" : "  - needs a core (C)");
            lv_obj_t* b = list_button(list, LV_SYMBOL_PLAY, text.c_str(), on_click, this);
            if (!core) lv_obj_set_style_text_color(b, c(kMuted), 0);
            rows_.push_back(s->id);
        }
        finish_list(list);
    }

    // ---- The games for one system
    void show_games(const System& s) {
        page_ = Page::Games;
        sys_ = &s;
        clear();
        std::string file;
        std::string core = core_for(s, &file);
        const CoreDef* def = core_def(file);
        kit::title_row(root_, s.name, core.empty() ? "no core: C" : (def ? def->name : file.c_str()));
        lv_obj_t* list = make_list(root_, 0, 18, kScreenW, kContentH - 18);
        for (auto& g : games_for(s)) {
            list_button(list, LV_SYMBOL_PLAY, game_title(g).c_str(), on_click, this);
            rows_.push_back(g);
        }
        finish_list(list);
    }

    // ---- Cores: what's installed, download more
    bool installed(const CoreDef& c) {
        if (kit::simulated())
            for (auto& n : sim_downloaded_)
                if (n == c.name) return true;
        return !find_core(c.file).empty();
    }

    void show_cores() {
        page_ = Page::Cores;
        clear();
        kit::title_row(root_, "Emulator cores", dl_.started() ? "downloading..." : "Enter get  Del remove");
        lv_obj_t* list = make_list(root_, 0, 18, kScreenW, kContentH - 18);
        for (auto& core : cores()) {
            if (std::string(core.file) == "zstest") continue;
            std::string for_what;
            for (auto& s : systems())
                for (const char* f : s.cores)
                    if (std::string(f) == core.file && !s.hidden)
                        for_what += (for_what.empty() ? "" : ", ") + std::string(s.id);
            bool in = installed(core);
            std::string state = in ? "installed"
                                : dl_.started() && dl_name_ == core.name ? "downloading..."
                                : core.download ? fmt("get, %.1f MB", core.size_kb / 1024.0)
                                                : std::string("apt: ") + core.package;
            std::string text = std::string(core.name) + "  (" + for_what + ")   " + state;
            lv_obj_t* b = list_button(list, in ? LV_SYMBOL_OK : LV_SYMBOL_DOWNLOAD, text.c_str(), on_click, this);
            if (!in) lv_obj_set_style_text_color(b, c(kMuted), 0);
            rows_.push_back(core.file);
        }
        finish_list(list);
    }

    void download(const CoreDef& c) {
        if (dl_.started()) {
            shell_toast("One download at a time");
            return;
        }
        if (!c.download) {
            shell_toast(std::string("Install it with: sudo apt install ") + c.package);
            return;
        }
        std::string dir = user_cores_dir();
        kit::make_dirs(dir);
        dl_name_ = c.name;
        kit::RunOpts o;
        o.cwd = dir;
        o.sim = {"downloading", "unzipping"};
        // Into ~/Games/cores: the app's own folder, nothing system-wide changes.
        dl_.start({"sh", "-c",
                   "set -e; f=\"$1_libretro.so\"; curl -fsSL --retry 2 -o \"$f.zip.part\" \"$2\"; "
                   "unzip -o -q \"$f.zip.part\" \"$f\"; rm -f \"$f.zip.part\"; test -s \"$f\"",
                   "sh", c.file, core_url(c)},
                  std::move(o));
        show_cores();
    }

    void remove_core() {
        lv_obj_t* f = group ? lv_group_get_focused(group) : nullptr;
        if (!f) return;
        int i = int(lv_obj_get_index(f));
        if (i < 0 || size_t(i) >= rows_.size()) return;
        std::string path = user_cores_dir() + "/" + rows_[size_t(i)] + "_libretro.so";
        if (!kit::exists(path)) {
            shell_toast("Only downloaded cores can be removed here");
            return;
        }
        std::remove(path.c_str());
        shell_toast("Removed");
        show_cores();
    }

    void picked(int i) {
        if (i < 0 || size_t(i) >= rows_.size()) return;
        const std::string& row = rows_[size_t(i)];
        if (page_ == Page::Library) {
            for (auto& s : systems())
                if (row == s.id) show_games(s);
        } else if (page_ == Page::Games) {
            game_ = row;
            if (core_for(*sys_).empty()) {
                shell_toast("No core for this system yet: press C to get one");
                return;
            }
            show_starting();
        } else if (page_ == Page::Cores) {
            const CoreDef* c = core_def(row);
            if (!c) return;
            if (installed(*c)) shell_toast(std::string(c->name) + " is installed");
            else download(*c);
        }
    }

    // ---- Playing
    void show_starting() {
        page_ = Page::Playing;
        menu_ = nullptr;
        stage_ = lv_obj_create(lv_layer_top());
        style_plain(stage_);
        lv_obj_set_size(stage_, kScreenW, kScreenH);
        lv_obj_set_pos(stage_, 0, 0);
        lv_obj_set_style_bg_color(stage_, lv_color_black(), 0);
        lv_obj_set_style_bg_opa(stage_, LV_OPA_COVER, 0);
        lv_obj_remove_flag(stage_, LV_OBJ_FLAG_SCROLLABLE);
        note_ = label(stage_, ("Starting " + game_title(game_) + "...").c_str(), &zs_font_12, kMuted);
        lv_obj_center(note_);
        pending_load_ = true;
    }

    void start_game() {
        std::string core = core_for(*sys_);
        std::string error;
        if (!emu_.load(core, game_, kScreenW, kScreenH, error)) {
            close_stage();
            show_games(*sys_);
            shell_toast(error);
            return;
        }
        if (note_) lv_obj_delete(note_);
        note_ = nullptr;
        pix_.assign(size_t(kScreenW) * kScreenH, 0);
        img_w_ = img_h_ = 0;
        img_ = lv_image_create(stage_);
        // On the device the emulator keeps its own time; the simulator's clock
        // can be virtual (headless), so there it runs in step with LVGL.
        emu_.start(!kit::simulated());
        frame_timer_ = lv_timer_create(on_frame, 15, this);
        hint(emu_.pad().gamepads() ? "Esc or Home: menu" : "Esc menu   WASD move   K/J A/B   Enter start");
    }

    void hint(const std::string& text) {
        if (!stage_) return;
        if (!toast_) {
            toast_ = label(stage_, "", &zs_font_12, kText);
            lv_obj_set_style_bg_color(toast_, c(kSurface), 0);
            lv_obj_set_style_bg_opa(toast_, LV_OPA_80, 0);
            lv_obj_set_style_pad_hor(toast_, 6, 0);
            lv_obj_set_style_pad_ver(toast_, 2, 0);
            lv_obj_set_style_radius(toast_, 4, 0);
            lv_obj_align(toast_, LV_ALIGN_BOTTOM_MID, 0, -4);
        }
        lv_label_set_text(toast_, text.c_str());
        lv_obj_remove_flag(toast_, LV_OBJ_FLAG_HIDDEN);
        lv_obj_move_foreground(toast_);
        toast_at_ = lv_tick_get();
    }

    static void on_frame(lv_timer_t* t) { static_cast<GamesApp*>(lv_timer_get_user_data(t))->frame(); }

    void frame() {
        emu_.step(lv_tick_get());
        if (menu_pick_ >= 0) {
            menu_action();
            menu_pick_ = -1;
            if (!stage_) return;   // quit
        }
        int w = 0, h = 0;
        if (emu_.take_frame(pix_, w, h) && w > 0 && h > 0) {
            if (w != img_w_ || h != img_h_) {   // first frame, or the core changed size
                img_w_ = w;
                img_h_ = h;
                dsc_ = {};
                dsc_.header.magic = LV_IMAGE_HEADER_MAGIC;
                dsc_.header.cf = LV_COLOR_FORMAT_RGB565;
                dsc_.header.w = uint32_t(w);
                dsc_.header.h = uint32_t(h);
                dsc_.header.stride = uint32_t(w * 2);
                lv_obj_center(img_);
            }
            dsc_.data = reinterpret_cast<const uint8_t*>(pix_.data());   // pix_ may have moved
            dsc_.data_size = uint32_t(size_t(w) * size_t(h) * 2);
            lv_image_set_src(img_, &dsc_);
            lv_obj_invalidate(img_);
        }
        std::string m = emu_.take_message();
        if (!m.empty()) hint(m);
        if (toast_ && !lv_obj_has_flag(toast_, LV_OBJ_FLAG_HIDDEN) && lv_tick_elaps(toast_at_) > 2500)
            lv_obj_add_flag(toast_, LV_OBJ_FLAG_HIDDEN);
        // A gamepad isn't a ZeroShell key: tell the power manager someone is playing.
        if (emu_.pad().take_activity() && lv_tick_elaps(active_at_) > 1000) {
            active_at_ = lv_tick_get();
            power().on_key(active_at_);
        }
    }

    static void on_menu(lv_event_t* e) {
        auto* self = static_cast<GamesApp*>(lv_event_get_user_data(e));
        // Acted on from the frame timer: not inside the list's own event.
        self->menu_pick_ = int(lv_obj_get_index(lv_event_get_target_obj(e)));
    }

    void open_menu() {
        if (!stage_ || menu_) return;
        emu_.set_paused(true);
        menu_ = lv_obj_create(stage_);
        style_panel(menu_);
        lv_obj_set_size(menu_, 180, 148);
        lv_obj_center(menu_);
        lv_obj_set_style_pad_all(menu_, 4, 0);
        lv_obj_t* list = make_list(menu_, 0, 0, 170, 138);
        if (group) lv_group_remove_all_objs(group);
        list_button(list, LV_SYMBOL_PLAY, "Resume", on_menu, this);
        list_button(list, LV_SYMBOL_SAVE, "Save state", on_menu, this);
        lv_obj_t* l = list_button(list, LV_SYMBOL_UPLOAD, "Load state", on_menu, this);
        if (!emu_.has_state()) lv_obj_set_style_text_color(l, c(kMuted), 0);
        list_button(list, LV_SYMBOL_REFRESH, "Reset", on_menu, this);
        list_button(list, LV_SYMBOL_CLOSE, "Quit game", on_menu, this);
        finish_list(list);
    }

    void close_menu() {
        if (!menu_) return;
        lv_obj_delete(menu_);
        menu_ = nullptr;
        if (group) lv_group_remove_all_objs(group);
        emu_.hold_until_release();   // the Enter that chose Resume is not Start
        emu_.set_paused(false);
    }

    void menu_action() {
        if (!menu_) return;
        switch (menu_pick_) {
            case 0: close_menu(); break;
            case 1:
                close_menu();
                hint(emu_.save_state() ? "State saved" : "This core can't save states");
                break;
            case 2:
                close_menu();
                hint(emu_.load_state() ? "State loaded" : "No saved state");
                break;
            case 3: close_menu(); emu_.reset(); hint("Reset"); break;
            case 4: stop_game(); break;
            default: break;
        }
    }

    void close_stage() {
        if (frame_timer_) lv_timer_delete(frame_timer_);
        frame_timer_ = nullptr;
        if (stage_) lv_obj_delete(stage_);
        stage_ = img_ = note_ = menu_ = toast_ = nullptr;
    }

    void stop_game() {
        emu_.stop();   // saves the game's battery save
        close_stage();
        if (page_ == Page::Playing && sys_) show_games(*sys_);
    }

    lv_obj_t* root_ = nullptr;
    Page page_ = Page::Library;
    const System* sys_ = nullptr;
    std::vector<std::string> rows_;
    std::string game_;
    int pending_row_ = -1;
    bool pending_load_ = false;

    Emulator emu_;
    lv_obj_t *stage_ = nullptr, *img_ = nullptr, *note_ = nullptr, *menu_ = nullptr, *toast_ = nullptr;
    lv_timer_t* frame_timer_ = nullptr;
    lv_image_dsc_t dsc_{};
    std::vector<uint16_t> pix_;
    int img_w_ = 0, img_h_ = 0;
    int menu_pick_ = -1;
    uint32_t toast_at_ = 0, active_at_ = 0;

    kit::Job dl_;
    std::string dl_name_;
    std::vector<std::string> sim_downloaded_;
};
}  // namespace

std::unique_ptr<App> make_games_app() { return std::make_unique<GamesApp>(); }

}  // namespace zs
