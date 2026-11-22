// Terminal: a real Linux console on the built-in screen, running tmux, so
// sessions (and SSH connections) keep running when you go back. Ctrl+Q (tmux
// detach) or `exit` returns to ZeroShell. In the simulator, a stand-in shows
// what would run.
//
// Other apps open it with shell_open_app("terminal", "<command>") to run a
// command in a new tmux window (Remote: "ssh desktop"); "shell" opens a shell.
#include "apps/common.h"

namespace zs {

namespace {
using namespace theme;

class TerminalApp : public App {
public:
    void build(lv_obj_t* root) override {
        root_ = root;
        // On the device, opening the app opens the terminal; nothing is drawn
        // first (it would flash before the console takes the screen), and
        // closing it (Ctrl+Q or exit) goes back to the home screen.
        std::string cmd = shell_app_arg();
        if (cmd == "shell") cmd.clear();
        if (shell_open_terminal(cmd)) return;
        // Simulator: a page with help and a button, and a stand-in console.
        lv_obj_t* panel = lv_obj_create(root);
        style_panel(panel);
        lv_obj_set_pos(panel, 6, 4);
        lv_obj_set_size(panel, 308, kContentH - 10);
        lv_obj_remove_flag(panel, LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_set_flex_flow(panel, LV_FLEX_FLOW_COLUMN);
        lv_obj_set_style_pad_row(panel, 4, 0);
        label(panel, LV_SYMBOL_KEYBOARD "  Terminal", &zs_font_16, kAccent);
        lv_obj_t* help = label(panel,
                               "Enter opens a terminal on this screen (53 x 14).\n"
                               "Ctrl+Q  or  exit  comes back here. Sessions keep\n"
                               "running in tmux; open it again to pick up.\n"
                               "Remote has your devices, for one-key SSH.",
                               &zs_font_12, kMuted);
        lv_obj_set_width(help, 290);
        lv_obj_t* b = lv_button_create(panel);
        lv_obj_set_size(b, 160, 26);
        lv_obj_set_style_bg_color(b, c(kSurface2), 0);
        lv_obj_set_style_bg_color(b, c(kAccent), LV_STATE_FOCUSED);
        lv_obj_set_style_text_color(b, c(kOnAccent), LV_STATE_FOCUSED);
        lv_obj_t* l = lv_label_create(b);
        lv_label_set_text(l, LV_SYMBOL_PLAY "  Open terminal");
        lv_obj_center(l);
        lv_obj_add_event_cb(b, [](lv_event_t* e) {
            static_cast<TerminalApp*>(lv_event_get_user_data(e))->open("");
        }, LV_EVENT_CLICKED, this);
        shell_add_focus(b);

        show_fake(cmd);
    }

    bool on_back() override {
        if (fake_) {
            lv_obj_delete(fake_);
            fake_ = nullptr;
            return true;
        }
        return false;
    }

private:
    void open(const std::string& cmd) {
        if (shell_open_terminal(cmd)) return;
        show_fake(cmd);
    }

    // Simulator: what the console would show.
    void show_fake(const std::string& cmd) {
        if (fake_) lv_obj_delete(fake_);
        fake_ = lv_obj_create(root_);
        style_plain(fake_);
        lv_obj_set_size(fake_, kScreenW, kContentH);
        lv_obj_set_style_bg_color(fake_, c(kTerminalBg), 0);
        lv_obj_set_style_bg_opa(fake_, LV_OPA_COVER, 0);
        lv_obj_set_style_pad_all(fake_, 6, 0);
        // Ubuntu's prompt colours: green user@host, blue path.
        lv_obj_t* prompt = lv_obj_create(fake_);
        style_plain(prompt);
        lv_obj_set_size(prompt, 300, LV_SIZE_CONTENT);
        lv_obj_set_flex_flow(prompt, LV_FLEX_FLOW_ROW);
        lv_obj_set_style_pad_column(prompt, 0, 0);
        label(prompt, "pi@cardputer", &zs_font_12, 0x8ae234);
        label(prompt, ":", &zs_font_12, 0xffffff);
        label(prompt, "~", &zs_font_12, 0x729fcf);
        label(prompt, ("$ " + cmd + (cmd.empty() ? "_" : "")).c_str(), &zs_font_12, 0xffffff);
        lv_obj_t* l = label(fake_,
                            "[simulator] On the Cardputer this is a real\n"
                            "Linux terminal running tmux. Ctrl+Q or exit\n"
                            "comes back. Esc closes this preview.",
                            &zs_font_12, 0xffffff);
        lv_obj_set_pos(l, 0, 30);
        lv_obj_set_width(l, 300);
    }

    lv_obj_t* root_ = nullptr;
    lv_obj_t* fake_ = nullptr;
};
}  // namespace

std::unique_ptr<App> make_terminal_app() { return std::make_unique<TerminalApp>(); }

}  // namespace zs
