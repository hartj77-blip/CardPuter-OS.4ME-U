// Notes: a plain-text editor. Saves to ~/Notes/notes.txt (or the file opened from
// Files) a few seconds after you stop typing, and when you leave.
#include <sys/stat.h>
#include <unistd.h>

#include <cstdio>
#include <fstream>
#include <sstream>

#include "apps/common.h"
#include "ui/fileio.h"

namespace zs {

namespace {
using namespace theme;

class NotesApp : public App {
public:
    ~NotesApp() override { save(); }

    // ~/Notes/notes.txt (synced with the Notes folder). Older images used
    // ~/notes.txt; move it the first time.
    static std::string default_path() {
        std::string dir = home_dir() + "/Notes", p = dir + "/notes.txt", old = home_dir() + "/notes.txt";
        mkdir(dir.c_str(), 0755);
        if (access(p.c_str(), F_OK) != 0 && access(old.c_str(), F_OK) == 0) rename(old.c_str(), p.c_str());
        return p;
    }

    void build(lv_obj_t* root) override {
        // Files opens a text file here by passing its path.
        std::string pending = shell_app_arg();
        path_ = pending.empty() ? default_path() : pending;

        std::string name = path_.substr(path_.find_last_of('/') + 1);
        lv_obj_t* t = label(root, name.c_str(), &zs_font_12, kAccent);
        lv_obj_set_pos(t, 8, 3);
        status_ = label(root, "", &zs_font_12, kMuted);
        lv_obj_align(status_, LV_ALIGN_TOP_RIGHT, -8, 3);

        ta_ = lv_textarea_create(root);
        lv_obj_set_pos(ta_, 4, 20);
        lv_obj_set_size(ta_, kScreenW - 8, kContentH - 24);
        lv_obj_set_style_bg_color(ta_, c(kSurface), 0);
        lv_obj_set_style_border_width(ta_, 0, 0);
        lv_obj_set_style_text_font(ta_, &zs_font_12, 0);
        lv_obj_set_style_pad_all(ta_, 6, 0);
        lv_obj_set_style_bg_color(ta_, c(kAccent), LV_PART_CURSOR | LV_STATE_FOCUSED);

        std::ifstream f(path_);
        std::stringstream ss;
        ss << f.rdbuf();
        text_ = ss.str();
        if (text_.empty() && !f) {
            text_ = "Welcome to ZeroOS Notes.\n\nType anything. It saves as you go.\n";
        }
        lv_textarea_set_text(ta_, text_.c_str());
        lv_obj_add_event_cb(ta_, on_change, LV_EVENT_VALUE_CHANGED, this);
        shell_add_focus(ta_);
        lv_group_focus_obj(ta_);
        update_status();
    }

    // Autosave 3 s after the last edit, so a crash or flat battery loses
    // at most a few seconds of typing.
    void tick() override {
        if (dirty_ && lv_tick_elaps(edited_at_) > 3000) save();
    }

private:
    static void on_change(lv_event_t* e) {
        auto* self = static_cast<NotesApp*>(lv_event_get_user_data(e));
        self->dirty_ = true;
        self->edited_at_ = lv_tick_get();
        self->update_status();
    }

    void update_status() {
        std::string t = lv_textarea_get_text(ta_);
        size_t words = 0;
        bool in = false;
        for (char ch : t) {
            bool ws = ch == ' ' || ch == '\n' || ch == '\t';
            if (!ws && !in) ++words;
            in = !ws;
        }
        lv_label_set_text(status_, fmt("%zu words%s", words, dirty_ ? "  *" : "").c_str());
    }

    // Written to a temp file and renamed over the note, so a power cut or a
    // full card never leaves it half-written. On failure the edits stay
    // marked unsaved and the next autosave tries again.
    void save() {
        if (!dirty_ || !ta_) return;
        mode_t mode = 0644;
        struct stat st;
        if (stat(path_.c_str(), &st) == 0) mode = st.st_mode & 07777;   // keep the file's permissions
        if (write_file_atomic(path_, lv_textarea_get_text(ta_), mode)) {
            dirty_ = false;
            failed_ = false;
        } else if (!failed_) {
            failed_ = true;   // say it once, not on every retry
            shell_toast("Couldn't save the note (card full or read-only?)");
        }
        if (status_) update_status();
    }

    std::string path_, text_;
    lv_obj_t* ta_ = nullptr;
    lv_obj_t* status_ = nullptr;
    bool dirty_ = false, failed_ = false;
    uint32_t edited_at_ = 0;
};
}  // namespace

std::unique_ptr<App> make_notes_app() { return std::make_unique<NotesApp>(); }

}  // namespace zs
