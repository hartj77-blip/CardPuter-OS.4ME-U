// Apps: install and remove apps (docs/APPS.md). ZeroOS itself keeps only a
// few apps; everything else comes from here, downloaded from GitHub.
//
//   list     Enter: details. Tab: U update all, R look for new apps, P sources
//   details  Enter: the highlighted button. Tab: I install, U update, O open, D remove
//   sources  Enter on "Add a GitHub repo". Tab: N add one, D remove the highlighted one
// Typing finds an app or a source (ui/list_view.h).
//
// zeroos-apps does the work as root; this screen starts it and shows how far
// it has got. After an install or removal the home screen is rebuilt at
// once; an upgraded app needs ZeroShell to restart, which this does.
#include <csignal>

#include "apps/common.h"
#include "ui/list_view.h"
#include "ui/store.h"

namespace zs {
namespace {
using namespace theme;

std::string size_text(long kb) {
    if (kb <= 0) return "";
    if (kb < 1024) return fmt("%ld KB", kb);
    return fmt("%.1f MB", kb / 1024.0);
}

std::string age_text(long s) {
    if (s < 0) return "never";
    if (s < 90) return "just now";
    if (s < 5400) return fmt("%ld min ago", (s + 30) / 60);
    if (s < 2 * 86400) return fmt("%ld h ago", (s + 1800) / 3600);
    return fmt("%ld days ago", s / 86400);
}

class StoreApp : public App {
public:
    void build(lv_obj_t* root) override {
        root_ = root;
        st_ = store::status();
        show_list();
        // Look for new apps when the list is old, unless a job is running.
        long age = store::catalog_age();
        if (!st_.busy() && (age < 0 || age > 6 * 3600) && online()) start([] { store::refresh(); });
    }

    ~StoreApp() override {
        lv_async_call_cancel(rebuild_cb, this);
    }

    void tick() override {
        if (restart_in_ > 0 && --restart_in_ == 0) {
            // systemd starts ZeroShell again at once (Restart=always), now
            // with the new version of the app.
            std::raise(SIGTERM);
            return;
        }
        if (++n_ % 5 != 0) return;   // twice a second
        bool waited = wait_ > 0 && --wait_ == 0;
        store::Status st = store::status();
        bool changed = st.state != st_.state || st.percent != st_.percent || st.app != st_.app;
        st_ = st;
        if (watching_ && wait_ == 0 && !st_.busy() && (changed || waited)) finished();
        if (changed || waited) show_progress();
    }

    bool on_back() override {
        if (page_ == Page::Add) {
            show_sources();
            return true;
        }
        if (page_ != Page::List) {
            show_list();
            return true;
        }
        return false;
    }

    bool on_key(uint32_t key) override {
        if (page_ == Page::List || page_ == Page::Sources) return list_.key(key);   // typing finds
        return false;   // details: buttons; add: the text box types
    }

private:
    enum class Page { List, Details, Sources, Add };

    bool online() {
        NetStatus n = hal().net();
        return n.wifi_connected || n.ethernet || !n.tether.empty();
    }

    // Run a job, unless one is already running.
    template <class F>
    bool start(F job) {
        if (busy()) {
            shell_toast("Busy: wait for the current job");
            return false;
        }
        if (!online()) {
            shell_toast("Not online: join Wi-Fi first");
            return false;
        }
        job();
        watching_ = true;
        wait_ = 4;   // the service takes a moment to write its first state
        show_progress();
        return true;
    }
    bool busy() const { return wait_ > 0 || st_.busy(); }

    void clear() {
        lv_obj_clean(root_);
        lv_group_remove_all_objs(group);
        bar_ = status_ = nullptr;
        ta_ = nullptr;
    }

    // The bottom line, on every page: what's going on, with a bar while busy.
    void bottom_line() {
        status_ = label(root_, "", &zs_font_12, kMuted);
        lv_obj_set_width(status_, kScreenW - 16);
        lv_label_set_long_mode(status_, LV_LABEL_LONG_DOT);
        lv_obj_align(status_, LV_ALIGN_BOTTOM_LEFT, 8, -2);
        bar_ = thin_bar(root_, kScreenW - 16, 3, kAccent);
        lv_obj_align(bar_, LV_ALIGN_BOTTOM_LEFT, 8, -17);
        lv_bar_set_range(bar_, 0, 100);
        show_progress();
    }

    std::string name_of(const std::string& id) {
        for (auto& e : cat_)
            if (e.id == id) return e.name;
        return id;
    }

    void show_progress() {
        if (!status_) return;
        std::string text, who = st_.app.empty() ? "" : name_of(st_.app);
        uint32_t color = kMuted;
        int pct = -1;
        store::Status::State s = st_.state;
        if (wait_ > 0) s = store::Status::Refreshing;
        switch (s) {
            case store::Status::Refreshing:
                text = wait_ > 0 ? "Starting..." : "Looking for apps...";
                pct = 0;
                break;
            case store::Status::Downloading:
                text = fmt("Downloading %s  %d%%", who.c_str(), st_.percent);
                pct = st_.percent;
                break;
            case store::Status::Installing:
                text = who.empty() ? "Installing..." : "Installing " + who + "...";
                pct = 100;
                break;
            case store::Status::Removing:
                text = "Removing " + who + "...";
                pct = 100;
                break;
            case store::Status::Failed:
                text = st_.message.empty() ? "That didn't work" : st_.message;
                color = kBad;
                break;
            case store::Status::Offline:
                text = "Can't reach GitHub. Check the connection";
                color = kWarn;
                break;
            default:
                text = page_ == Page::List ? "Updated " + age_text(store::catalog_age()) : "";
                break;
        }
        color = s == store::Status::Refreshing || s == store::Status::Downloading || s == store::Status::Installing ||
                        s == store::Status::Removing
                    ? kAccent
                    : color;
        lv_label_set_text(status_, text.c_str());
        lv_obj_set_style_text_color(status_, c(color), 0);
        if (pct < 0) {
            lv_obj_add_flag(bar_, LV_OBJ_FLAG_HIDDEN);
        } else {
            lv_obj_remove_flag(bar_, LV_OBJ_FLAG_HIDDEN);
            lv_bar_set_value(bar_, pct, LV_ANIM_OFF);
        }
    }

    // A job ended: say how, and pick up what changed.
    void finished() {
        watching_ = false;
        if (st_.state == store::Status::Done) {
            std::string who = name_of(st_.app);
            if (st_.action == "install") shell_toast(who + " is installed");
            else if (st_.action == "remove") shell_toast(who + " is removed");
            else if (st_.action == "upgrade") shell_toast("Apps updated");
            if (st_.action == "install" || st_.action == "remove" || st_.action == "upgrade") {
                app_registry_reload();
                if (app_modules_stale()) {
                    shell_toast("Restarting to finish the update");
                    restart_in_ = 15;   // 1.5 s, so the message can be read
                }
            }
        }
        defer(page_ == Page::Add ? Page::Sources : page_);
    }

    // Rebuild a page after the current event handler returns.
    void defer(Page p) {
        next_ = p;
        lv_async_call(rebuild_cb, this);
    }
    static void rebuild_cb(void* p) {
        auto* s = static_cast<StoreApp*>(p);
        switch (s->next_) {
            case Page::List: s->show_list(); break;
            case Page::Details: s->show_details(); break;
            case Page::Sources: s->show_sources(); break;
            case Page::Add: s->show_add(); break;
        }
    }

    const char* icon_of(const std::string& id) {
        const AppInfo* a = app_find(id);
        return a ? a->symbol : ZS_ICON_GRID;
    }

    // ---- The list ----

    void show_list() {
        clear();
        page_ = Page::List;
        cat_ = store::catalog();
        int updates = 0;
        for (auto& e : cat_) updates += e.update();
        shell_set_actions({
            {"Details", 0, "Enter", nullptr},
            {fmt("Update all (%d)", updates), 'u', nullptr, [this] { update_all(); }, updates > 0},
            {"Look for new apps", 'r', nullptr, [this] { start([] { store::refresh(); }); }},
            {"Sources", 'p', nullptr, [this] { show_sources(); }},
        });

        list_.build(root_, 2, 2, kScreenW - 4, kBodyH - 24);
        list_.set_empty_text(online() ? "No apps yet. Tab > R looks for them." : "No apps yet. Join Wi-Fi, then Tab > R.");
        int focus = -1;
        if (updates > 0) list_.add(LV_SYMBOL_REFRESH, "Update all", fmt("%d", updates), [this] { update_all(); });
        for (size_t i = 0; i < cat_.size(); ++i) {
            auto& e = cat_[i];
            std::string right;
            uint32_t color = kMuted;
            if (e.update()) {
                right = "Update";
                color = kAccent;
            } else if (!e.installed.empty()) {
                right = LV_SYMBOL_OK " Installed";
                color = kGood;
            } else {
                right = size_text(e.size_kb);
            }
            std::string id = e.id;
            list_.add(icon_of(e.id), e.name, right, [this, id] {
                sel_id_ = id;
                show_details();
            });
            list_.set_value_color(list_.size() - 1, color);
            if (!sel_id_.empty() && e.id == sel_id_) focus = list_.size() - 1;
        }
        list_.finish();
        // Back on the list after a details page: keep the same app selected.
        if (focus >= 0) list_.focus(focus);
        bottom_line();
    }

    void update_all() {
        start([] { store::upgrade_all(); });
    }

    // ---- One app ----

    const store::Entry* selected() {
        for (auto& e : cat_)
            if (e.id == sel_id_) return &e;
        return nullptr;
    }

    void show_details() {
        clear();
        page_ = Page::Details;
        cat_ = store::catalog();
        const store::Entry* e = selected();
        if (!e) {
            show_list();
            return;
        }
        lv_obj_t* icon = lv_obj_create(root_);
        style_plain(icon);
        lv_obj_set_size(icon, 34, 34);
        lv_obj_set_pos(icon, 8, 6);
        lv_obj_set_style_radius(icon, 10, 0);
        const AppInfo* a = app_find(e->id);
        lv_obj_set_style_bg_color(icon, c(a ? a->color : 0x5e6c7b), 0);
        lv_obj_set_style_bg_opa(icon, LV_OPA_COVER, 0);
        lv_obj_center(label(icon, icon_of(e->id), &zs_font_16, 0xffffff));

        lv_obj_t* name = label(root_, e->name.c_str(), &zs_font_16, kText);
        lv_obj_set_pos(name, 50, 4);
        std::string meta;
        if (!e->installed.empty()) meta = "Installed " + e->installed;
        if (e->update()) meta += ", " + e->version + " ready";
        if (e->installed.empty() && !e->version.empty()) meta = "Version " + e->version;
        if (e->size_kb > 0) meta += "   " + size_text(e->size_kb);
        lv_obj_t* m = label(root_, meta.c_str(), &zs_font_12, e->update() ? kAccent : kMuted);
        lv_obj_set_pos(m, 50, 24);

        lv_obj_t* sum = label(root_, e->summary.c_str(), &zs_font_12, kText);
        lv_obj_set_width(sum, kScreenW - 16);
        lv_label_set_long_mode(sum, LV_LABEL_LONG_WRAP);
        lv_obj_set_pos(sum, 8, 44);
        std::string from = e->source.empty() ? "" : "From " + e->source;
        if (!e->needs.empty()) from += (from.empty() ? "" : ".  ") + std::string("Brings ") + e->needs;
        lv_obj_t* f = label(root_, from.c_str(), &zs_font_12, kMuted);
        lv_obj_set_size(f, kScreenW - 16, 15);   // one line, cut with "..."
        lv_label_set_long_mode(f, LV_LABEL_LONG_DOT);
        lv_obj_set_pos(f, 8, 74);

        int x = 8;
        auto button = [&](const char* text, bool accent, lv_event_cb_t cb) {
            lv_obj_t* b = lv_button_create(root_);
            lv_obj_set_height(b, 24);
            lv_obj_set_width(b, LV_SIZE_CONTENT);
            lv_obj_set_style_pad_hor(b, 10, 0);
            lv_obj_set_style_bg_color(b, c(accent ? kAccent : kSurface2), 0);
            lv_obj_set_style_border_color(b, c(kText), LV_STATE_FOCUSED);
            lv_obj_set_style_border_width(b, 2, LV_STATE_FOCUSED);
            lv_obj_set_style_shadow_width(b, 0, 0);
            lv_obj_t* l = label(b, text, &zs_font_12, kText);
            lv_obj_center(l);
            lv_obj_set_pos(b, x, 90);   // above the progress line (kBodyH)
            lv_obj_add_event_cb(b, cb, LV_EVENT_CLICKED, this);
            shell_add_focus(b);
            lv_obj_update_layout(b);
            x += lv_obj_get_width(b) + 6;
        };
        bool installed = !e->installed.empty();
        std::string id = e->id, app_name = e->name;
        std::vector<Action> acts = {{"Select", 0, "Enter", nullptr}};
        if (!installed && !e->version.empty())
            acts.push_back({"Install", 'i', nullptr, [this, id] { start([id] { store::install(id); }); }});
        if (e->update()) acts.push_back({"Update", 'u', nullptr, [this, id] { start([id] { store::install(id); }); }});
        if (installed && app_find(id)) acts.push_back({"Open", 'o', nullptr, [id] { shell_open_app(id); }});
        if (installed) {
            Action rm{"Remove", 'd', nullptr, [this, id] { start([id] { store::remove(id); }); }};
            rm.danger = true;
            rm.confirm = "Remove " + app_name + "?";
            acts.push_back(rm);
        }
        shell_set_actions(std::move(acts));
        if (!installed && !e->version.empty())
            button(LV_SYMBOL_DOWNLOAD "  Install", true, [](lv_event_t* ev) {
                auto* s = static_cast<StoreApp*>(lv_event_get_user_data(ev));
                std::string id = s->sel_id_;
                s->start([id] { store::install(id); });
            });
        if (e->update())
            button(LV_SYMBOL_REFRESH "  Update", true, [](lv_event_t* ev) {
                auto* s = static_cast<StoreApp*>(lv_event_get_user_data(ev));
                std::string id = s->sel_id_;
                s->start([id] { store::install(id); });
            });
        if (installed && app_find(e->id))
            button(LV_SYMBOL_PLAY "  Open", !e->update(), [](lv_event_t* ev) {
                shell_open_app(static_cast<StoreApp*>(lv_event_get_user_data(ev))->sel_id_);
            });
        if (installed)
            button(LV_SYMBOL_TRASH "  Remove", false, [](lv_event_t* ev) {
                auto* s = static_cast<StoreApp*>(lv_event_get_user_data(ev));
                std::string id = s->sel_id_;
                shell_confirm("Remove " + s->name_of(id) + "?", "", "Remove", [s, id] {
                    s->start([id] { store::remove(id); });
                });
            });
        bottom_line();
    }

    // ---- Sources ----

    void show_sources() {
        clear();
        page_ = Page::Sources;
        sources_ = store::sources();
        Action rm{"Remove", 'd', nullptr, [this] { remove_source(); }};
        rm.danger = true;
        rm.confirm = "Remove this source?";
        shell_set_actions({{"Select", 0, "Enter", nullptr}, {"Add a GitHub repo", 'n', nullptr, [this] { show_add(); }}, rm});
        lv_obj_t* h = label(root_, "Apps come from these GitHub repos.", &zs_font_12, kMuted);
        lv_obj_set_pos(h, 8, 3);
        list_.build(root_, 2, 20, kScreenW - 4, kBodyH - 42);
        for (size_t i = 0; i < sources_.size(); ++i) list_.add(ZS_ICON_GLOBE, sources_[i], "", nullptr);
        list_.add(LV_SYMBOL_PLUS, "Add a GitHub repo", "", [this] { show_add(); });
        list_.finish();
        bottom_line();
    }

    void remove_source() {
        int i = list_.focused();
        if (i < 0 || i >= int(sources_.size())) {
            shell_toast("Pick a source first");
            return;
        }
        std::string s = sources_[size_t(i)];
        if (start([s] { store::remove_source(s); })) {
            shell_toast("Removed " + s);
            defer(Page::Sources);
        }
    }

    void show_add() {
        clear();
        page_ = Page::Add;
        shell_set_actions({{"Add", 0, "Enter", nullptr}, {"Back", 0, "Esc", nullptr}});
        lv_obj_t* t = label(root_, "GitHub repo (owner/name)", &zs_font_14);
        lv_obj_set_pos(t, 10, 6);
        ta_ = lv_textarea_create(root_);
        lv_textarea_set_one_line(ta_, true);
        lv_textarea_set_placeholder_text(ta_, "someone/cardputer-apps");
        lv_obj_set_size(ta_, kScreenW - 20, 34);
        lv_obj_set_pos(ta_, 10, 28);
        lv_obj_set_style_bg_color(ta_, c(kSurface), 0);
        lv_obj_set_style_border_color(ta_, c(kAccent), 0);
        lv_obj_add_event_cb(ta_, on_add, LV_EVENT_READY, this);
        shell_add_focus(ta_);
        lv_obj_t* h = label(root_,
                            "Its newest release must have a zeroos-apps.index file. Apps from it run with full "
                            "access to this device: only add repos you trust.",
                            &zs_font_12, kWarn);
        lv_obj_set_width(h, kScreenW - 20);
        lv_label_set_long_mode(h, LV_LABEL_LONG_WRAP);
        lv_obj_set_pos(h, 10, 68);
    }

    static void on_add(lv_event_t* e) {
        auto* self = static_cast<StoreApp*>(lv_event_get_user_data(e));
        std::string src = store::normalise_source(lv_textarea_get_text(self->ta_));
        if (src.empty()) {
            shell_toast("Type it as owner/name");
            return;
        }
        for (auto& s : store::sources())
            if (s == src) {
                shell_toast("Already there");
                return;
            }
        if (self->start([src] { store::add_source(src); })) self->defer(Page::Sources);
    }

    lv_obj_t* root_ = nullptr;
    lv_obj_t* status_ = nullptr;
    lv_obj_t* bar_ = nullptr;
    lv_obj_t* ta_ = nullptr;
    Page page_ = Page::List;
    Page next_ = Page::List;
    std::vector<store::Entry> cat_;
    std::vector<std::string> sources_;
    std::string sel_id_;
    ListView list_;
    store::Status st_;
    bool watching_ = false;
    int wait_ = 0;        // half seconds
    int restart_in_ = 0;  // ticks
    int n_ = 0;
};
}  // namespace

std::unique_ptr<App> make_store_app() { return std::make_unique<StoreApp>(); }

}  // namespace zs
