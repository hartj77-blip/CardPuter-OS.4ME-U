// REPL: an interactive prompt for a language, in the Terminal app. Python,
// Lua, Node.js, a calculator (bc), SQLite and a shell. Languages that aren't
// installed say which package brings them.
#include <string>
#include <vector>

#include "apps/common.h"
#include "apps/kit/run.h"
#include "apps/kit/ui.h"

namespace zs {
namespace {
using namespace theme;

struct Lang {
    const char* name;
    const char* command;   // what runs in the terminal
    const char* tool;      // the program, to check it is installed
    const char* package;   // Debian package with it
    const char* about;
};

const Lang kLangs[] = {
    {"Python", "python3", "python3", "python3", "python3"},
    {"Lua", "lua5.4", "lua5.4", "lua5.4", "Lua 5.4"},
    {"Node.js", "node", "node", "nodejs", "JavaScript"},
    {"Calculator", "bc -lq", "bc", "bc", "bc, with maths functions"},
    {"SQLite", "sqlite3 ~/Code/scratch.db", "sqlite3", "sqlite3", "~/Code/scratch.db"},
    {"Shell", "bash", "bash", "bash", "bash in ~/Code"},
};

class ReplApp : public App {
public:
    void build(lv_obj_t* root) override {
        kit::title_row(root, "REPL", "Enter start");
        lv_obj_t* list = make_list(root, 0, 18, kScreenW, kContentH - 18);
        for (const Lang& l : kLangs) {
            bool ok = kit::have(l.tool);
            std::string text = std::string(l.name) + "   " +
                               (ok ? l.about : (std::string("not installed: ") + l.package).c_str());
            lv_obj_t* b = list_button(list, ok ? LV_SYMBOL_PLAY : LV_SYMBOL_DOWNLOAD, text.c_str(), on_click, this);
            if (!ok) lv_obj_set_style_text_color(b, c(kMuted), 0);
        }
        finish_list(list);
    }

    void tick() override {
        if (pending_ < 0) return;
        const Lang& l = kLangs[pending_];
        pending_ = -1;
        if (!kit::have(l.tool)) {
            shell_toast(std::string("Install it with: sudo apt install ") + l.package);
            return;
        }
        std::string dir = home_dir() + "/Code";
        kit::make_dirs(dir);
        kit::terminal_run(l.command, dir);
    }

private:
    static void on_click(lv_event_t* e) {
        auto* self = static_cast<ReplApp*>(lv_event_get_user_data(e));
        int i = int(lv_obj_get_index(lv_event_get_target_obj(e)));
        if (i >= 0 && i < int(std::size(kLangs))) self->pending_ = i;
    }

    int pending_ = -1;
};
}  // namespace

std::unique_ptr<App> make_repl_app() { return std::make_unique<ReplApp>(); }

}  // namespace zs
