// ZClaw: a pocket AI assistant. Chat with Claude, or with ChatGPT, Gemini,
// OpenRouter, Groq, Mistral, an Ollama server on your PC, or any
// OpenAI-compatible server. Bring your own API key. See docs/ZCLAW.md.
//
//   Chat page      type, Enter to send. Up/Down scroll the conversation.
//                  Esc while it's thinking stops the request.
//   Tab            settings: provider, model, API key, server, new chat,
//                  save the chat to Notes. Tab or Esc: back to the chat.
//
// The request runs on a worker thread (curl), so the screen never freezes;
// leaving the app stops it.
#include <sys/stat.h>

#include <csignal>
#include <ctime>
#include <functional>
#include <memory>
#include <mutex>
#include <thread>

#include "apps/common.h"
#include "apps/zclaw/ai.h"
#include "ui/fileio.h"

namespace zs {
namespace {
using namespace theme;

// The conversation outlives the app screen: leave and come back to it.
std::vector<zclaw::Message>& history() {
    static std::vector<zclaw::Message> h;
    return h;
}

std::string conf_path() { return home_dir() + "/.config/zeroos/zclaw.conf"; }

// Shared with the worker thread.
struct Job {
    std::mutex m;
    bool done = false, ok = false;
    std::string reply, error;
    std::atomic<int> pid{0};
};

class ZClawApp : public App {
public:
    ~ZClawApp() override {
        lv_async_call_cancel(rebuild_cb, this);
        lv_async_call_cancel(run_pending_cb, this);
        stop();
    }

    void build(lv_obj_t* root) override {
        root_ = root;
        cfg_ = zclaw::load(conf_path());
        const zclaw::Provider* p = zclaw::find_provider(cfg_.provider);
        // First run: straight to settings to pick a provider and add a key.
        if (p && p->needs_key && cfg_.key.empty() && history().empty()) page_ = Page::Settings;
        rebuild();
    }

    void tick() override {
        if (!job_) return;
        bool done;
        {
            std::lock_guard<std::mutex> l(job_->m);
            done = job_->done;
        }
        if (!done) {
            if (thinking_ && ++dots_ % 5 == 0) {
                static const char* const d[] = {"Thinking", "Thinking.", "Thinking..", "Thinking..."};
                lv_label_set_text(thinking_, d[(dots_ / 5) % 4]);
            }
            return;
        }
        if (worker_.joinable()) worker_.join();
        if (job_->ok) {
            history().push_back({false, job_->reply});
        } else {
            // The question stays, so pressing Enter on an empty box retries it.
            shell_toast(job_->error);
            failed_ = true;
        }
        job_.reset();
        if (page_ == Page::Chat) fill_messages();
    }

    bool on_key(uint32_t key) override {
        if (input_) return false;
        if (key == 9) {   // Tab
            page_ = page_ == Page::Chat ? Page::Settings : Page::Chat;
            defer_rebuild();
            return true;
        }
        if (page_ == Page::Chat && chat_ && (key == LV_KEY_UP || key == LV_KEY_DOWN)) {
            lv_obj_scroll_by_bounded(chat_, 0, key == LV_KEY_UP ? 48 : -48, LV_ANIM_OFF);
            return true;
        }
        return false;
    }

    bool on_back() override {
        if (input_) {
            close_input();
            return true;
        }
        if (page_ == Page::Providers) {
            page_ = Page::Settings;
            defer_rebuild();
            return true;
        }
        if (page_ == Page::Settings) {
            page_ = Page::Chat;
            defer_rebuild();
            return true;
        }
        if (job_) {
            stop();
            shell_toast("Stopped");
            fill_messages();
            return true;
        }
        return false;
    }

private:
    enum class Page { Chat, Settings, Providers };

    void defer_rebuild() { lv_async_call(rebuild_cb, this); }
    static void rebuild_cb(void* p) { static_cast<ZClawApp*>(p)->rebuild(); }

    void rebuild() {
        if (ta_) draft_ = lv_textarea_get_text(ta_);
        if (group) lv_group_set_editing(group, false);
        lv_obj_clean(root_);
        chat_ = ta_ = thinking_ = input_ = list_ = nullptr;
        if (group) lv_group_remove_all_objs(group);
        switch (page_) {
            case Page::Chat: build_chat(); break;
            case Page::Settings: build_settings(); break;
            case Page::Providers: build_providers(); break;
        }
    }

    // ---- chat ----------------------------------------------------------------

    std::string provider_name() const {
        const zclaw::Provider* p = zclaw::find_provider(cfg_.provider);
        return p ? p->name : "?";
    }

    void build_chat() {
        lv_obj_t* head = label(root_, (provider_name() + "  |  " + zclaw::effective_model(cfg_)).c_str(),
                               &zs_font_12, kMuted);
        lv_obj_set_pos(head, 8, 2);
        lv_label_set_long_mode(head, LV_LABEL_LONG_DOT);
        lv_obj_set_width(head, kScreenW - 90);
        lv_obj_t* hint = label(root_, "Tab: settings", &zs_font_12, kMuted);
        lv_obj_align(hint, LV_ALIGN_TOP_RIGHT, -8, 2);

        chat_ = lv_obj_create(root_);
        style_panel(chat_);
        lv_obj_set_pos(chat_, 4, 18);
        lv_obj_set_size(chat_, kScreenW - 8, kContentH - 58);
        lv_obj_set_flex_flow(chat_, LV_FLEX_FLOW_COLUMN);
        lv_obj_set_style_pad_all(chat_, 4, 0);
        lv_obj_set_style_pad_row(chat_, 4, 0);
        fill_messages();

        ta_ = lv_textarea_create(root_);
        lv_textarea_set_one_line(ta_, true);
        lv_textarea_set_placeholder_text(ta_, "Ask anything, Enter to send");
        lv_textarea_set_max_length(ta_, 2000);
        lv_textarea_set_text(ta_, draft_.c_str());
        lv_obj_set_width(ta_, kScreenW - 8);
        lv_obj_set_pos(ta_, 4, kContentH - 37);
        lv_obj_add_event_cb(ta_, on_send, LV_EVENT_READY, this);
        lv_group_add_obj(group, ta_);
        lv_group_focus_obj(ta_);
        lv_group_set_editing(group, true);
    }

    static void on_send(lv_event_t* e) {
        auto* self = static_cast<ZClawApp*>(lv_event_get_user_data(e));
        std::string text = lv_textarea_get_text(self->ta_);
        if (self->job_) {
            shell_toast("Still thinking (Esc stops it)");
            return;
        }
        bool blank = text.find_first_not_of(" \t") == std::string::npos;
        if (blank) {
            // Enter on an empty box after a failure: ask again.
            if (self->failed_ && !history().empty() && history().back().user) self->send();
            return;
        }
        // A question that failed and wasn't retried is replaced, so the
        // conversation keeps taking turns.
        if (!history().empty() && history().back().user) history().pop_back();
        history().push_back({true, text});
        lv_textarea_set_text(self->ta_, "");
        self->draft_.clear();
        self->send();
    }

    void send() {
        failed_ = false;
        job_ = std::make_shared<Job>();
        auto job = job_;
        zclaw::Settings cfg = cfg_;
        std::vector<zclaw::Message> h = history();
        auto run = [job, cfg, h] {
            std::string reply, error;
            bool ok = zclaw::chat(cfg, h, reply, error, &job->pid);
            std::lock_guard<std::mutex> l(job->m);
            job->ok = ok;
            job->reply = std::move(reply);
            job->error = std::move(error);
            job->done = true;
        };
#ifdef __EMSCRIPTEN__
        run();   // the browser demo has no threads; it answers at once
#else
        worker_ = std::thread(run);
#endif
        fill_messages();
    }

    // Kill curl and wait for the worker: it must be gone before the app
    // (and its module) is.
    void stop() {
        if (job_) {
            if (int pid = job_->pid.load()) kill(pid, SIGTERM);
        }
        if (worker_.joinable()) worker_.join();
        if (job_ && !history().empty() && history().back().user) failed_ = true;
        job_.reset();
    }

    void fill_messages() {
        if (!chat_) return;
        lv_obj_clean(chat_);
        thinking_ = nullptr;
        auto& h = history();
        size_t first = h.size() > 30 ? h.size() - 30 : 0;   // keep the screen light
        if (h.empty()) {
            lv_obj_t* l = label(chat_, "Ask me anything: facts, ideas, code, a quick translation...",
                                &zs_font_12, kMuted);
            lv_label_set_long_mode(l, LV_LABEL_LONG_WRAP);
            lv_obj_set_width(l, kScreenW - 24);
        }
        for (size_t i = first; i < h.size(); ++i) {
            lv_obj_t* l = lv_label_create(chat_);
            lv_label_set_text(l, ((h[i].user ? "You: " : "") + h[i].text).c_str());
            lv_obj_set_style_text_font(l, &zs_font_12, 0);
            lv_obj_set_style_text_color(l, c(h[i].user ? kAccent : kText), 0);
            lv_label_set_long_mode(l, LV_LABEL_LONG_WRAP);
            lv_obj_set_width(l, kScreenW - 24);
        }
        if (job_) {
            thinking_ = label(chat_, "Thinking", &zs_font_12, kMuted);
        } else if (failed_ && !h.empty() && h.back().user) {
            label(chat_, "Not answered. Enter on an empty box tries again.", &zs_font_12, kBad);
        }
        lv_obj_update_layout(chat_);
        lv_obj_scroll_to_y(chat_, LV_COORD_MAX, LV_ANIM_OFF);
    }

    // ---- settings ------------------------------------------------------------

    void build_settings() {
        const zclaw::Provider* p = zclaw::find_provider(cfg_.provider);
        lv_obj_t* t = label(root_, LV_SYMBOL_SETTINGS " ZClaw settings", &zs_font_12, kAccent);
        lv_obj_set_pos(t, 8, 2);
        lv_obj_t* hint = label(root_, p ? p->key_hint : "", &zs_font_12, kMuted);
        lv_obj_set_pos(hint, 8, kContentH - 16);
        lv_label_set_long_mode(hint, LV_LABEL_LONG_DOT);
        lv_obj_set_width(hint, kScreenW - 16);

        list_ = make_list(root_, 4, 18, kScreenW - 8, kContentH - 36);
        std::string key = cfg_.key.empty()
                              ? std::string(p && !p->needs_key ? "not needed" : "not set")
                              : "set, ends " + (cfg_.key.size() > 4 ? cfg_.key.substr(cfg_.key.size() - 4) : "");
        std::string url = cfg_.url.empty() ? "default" : cfg_.url;
        if (p && !*p->url && cfg_.url.empty()) url = "not set";
        add_row(LV_SYMBOL_LIST, "Provider: " + provider_name(), [](ZClawApp* s) {
            s->page_ = Page::Providers;
            s->defer_rebuild();
        });
        add_row(LV_SYMBOL_KEYBOARD, "API key: " + key, [](ZClawApp* s) {
            s->ask("API key (paste or type it; empty removes it)", "", true, [s](const std::string& v) {
                s->cfg_.key = v;
                s->store("Key saved");
            });
        });
        add_row(LV_SYMBOL_EDIT, "Model: " + zclaw::effective_model(cfg_), [](ZClawApp* s) {
            s->ask("Model (empty: the default)", zclaw::effective_model(s->cfg_), false,
                   [s](const std::string& v) {
                       s->cfg_.model = v;
                       s->store("Model saved");
                   });
        });
        add_row(LV_SYMBOL_WIFI, "Server: " + url, [](ZClawApp* s) {
            s->ask("Server address (empty: the default)", s->cfg_.url, false, [s](const std::string& v) {
                s->cfg_.url = zclaw::normalize_url(s->cfg_.provider, v);
                s->store("Server saved");
            });
        });
        add_row(LV_SYMBOL_REFRESH, "New chat", [](ZClawApp* s) {
            s->stop();
            history().clear();
            s->failed_ = false;
            s->page_ = Page::Chat;
            s->defer_rebuild();
            shell_toast("New chat");
        });
        add_row(LV_SYMBOL_SAVE, "Save chat to Notes", [](ZClawApp* s) { s->save_to_notes(); });
        add_row(LV_SYMBOL_EDIT, "Personality (system prompt)", [](ZClawApp* s) {
            s->ask("How ZClaw should answer (empty: the default)", s->cfg_.system, false,
                   [s](const std::string& v) {
                       s->cfg_.system = v;
                       s->store("Saved");
                   });
        });
        finish_list(list_);
    }

    void build_providers() {
        lv_obj_t* t = label(root_, "Pick an AI provider", &zs_font_12, kAccent);
        lv_obj_set_pos(t, 8, 2);
        list_ = make_list(root_, 4, 18, kScreenW - 8, kContentH - 22);
        const auto& all = zclaw::providers();
        lv_obj_t* current = nullptr;
        for (size_t i = 0; i < all.size(); ++i) {
            bool on = cfg_.provider == all[i].id;
            lv_obj_t* b = list_button(list_, on ? LV_SYMBOL_OK : LV_SYMBOL_RIGHT, all[i].name, [](lv_event_t* e) {
                auto* s = static_cast<ZClawApp*>(lv_event_get_user_data(e));
                size_t i = size_t((intptr_t)lv_obj_get_user_data(lv_event_get_target_obj(e)));
                s->stop();
                zclaw::save(conf_path(), s->cfg_);
                zclaw::switch_provider(conf_path(), s->cfg_, zclaw::providers()[i].id);
                s->store(std::string("Using ") + zclaw::providers()[i].name);
                s->page_ = Page::Settings;
                s->defer_rebuild();
            }, this);
            lv_obj_set_user_data(b, (void*)(intptr_t)i);
            if (on) current = b;
        }
        finish_list(list_);
        if (current) lv_group_focus_obj(current);
    }

    using RowFn = void (*)(ZClawApp*);
    static void run_pending_cb(void* p) {
        auto* s = static_cast<ZClawApp*>(p);
        if (s->pending_) s->pending_(s);
    }
    void add_row(const char* sym, const std::string& text, RowFn fn) {
        lv_obj_t* b = list_button(list_, sym, text.c_str(), [](lv_event_t* e) {
            auto* s = static_cast<ZClawApp*>(lv_event_get_user_data(e));
            auto f = reinterpret_cast<RowFn>(lv_obj_get_user_data(lv_event_get_target_obj(e)));
            // Deferred: a row may rebuild the list it's in.
            s->pending_ = f;
            lv_async_call(run_pending_cb, s);
        }, this);
        lv_obj_set_user_data(b, reinterpret_cast<void*>(fn));
        lv_obj_t* l = lv_obj_get_child(b, 1);
        if (l) lv_label_set_long_mode(l, LV_LABEL_LONG_DOT);
    }

    void store(const std::string& msg) {
        if (zclaw::save(conf_path(), cfg_)) shell_toast(msg);
        else shell_toast("Couldn't save the settings");
        if (page_ == Page::Settings) defer_rebuild();
    }

    void save_to_notes() {
        if (history().empty()) {
            shell_toast("Nothing to save yet");
            return;
        }
        std::string dir = home_dir() + "/Notes";
        mkdir(dir.c_str(), 0755);
        char stamp[32];
        std::time_t now = std::time(nullptr);
        std::strftime(stamp, sizeof stamp, "%Y-%m-%d %H%M", std::localtime(&now));
        std::string path = dir + "/ZClaw " + stamp + ".txt";
        std::string out = "ZClaw chat, " + provider_name() + " (" + zclaw::effective_model(cfg_) + ")\n\n";
        for (auto& m : history()) out += (m.user ? "You: " : "ZClaw: ") + m.text + "\n\n";
        shell_toast(write_file_atomic(path, out) ? "Saved to Notes/ZClaw " + std::string(stamp) + ".txt"
                                                 : "Couldn't save (card full?)");
    }

    // ---- one-line text box over the screen (Enter accepts, Esc cancels) -------

    using Done = std::function<void(const std::string&)>;
    void ask(const char* title, const std::string& initial, bool password, Done done) {
        done_ = std::move(done);
        input_ = lv_obj_create(root_);
        style_panel(input_);
        lv_obj_set_size(input_, 300, 74);
        lv_obj_center(input_);
        lv_obj_set_style_border_color(input_, c(kAccent), 0);
        lv_obj_set_style_border_width(input_, 1, 0);
        lv_obj_remove_flag(input_, LV_OBJ_FLAG_SCROLLABLE);
        label(input_, title, &zs_font_12, kMuted);
        lv_obj_t* ta = lv_textarea_create(input_);
        lv_textarea_set_one_line(ta, true);
        lv_textarea_set_password_mode(ta, password);
        lv_textarea_set_max_length(ta, 1000);
        lv_textarea_set_text(ta, initial.c_str());
        lv_obj_set_width(ta, 280);
        lv_obj_align(ta, LV_ALIGN_BOTTOM_MID, 0, 0);
        lv_obj_add_event_cb(ta, [](lv_event_t* e) {
            auto* self = static_cast<ZClawApp*>(lv_event_get_user_data(e));
            std::string v = lv_textarea_get_text(lv_event_get_target_obj(e));
            Done d = std::move(self->done_);
            if (lv_indev_t* in = lv_indev_active()) lv_indev_wait_release(in);
            self->close_input();
            if (d) d(v);
        }, LV_EVENT_READY, this);
        lv_group_add_obj(group, ta);
        lv_group_focus_obj(ta);
        lv_group_set_editing(group, true);
    }

    void close_input() {
        if (!input_) return;
        lv_group_set_editing(group, false);
        lv_obj_t* in = input_;
        input_ = nullptr;
        lv_obj_delete_async(in);
        if (list_ && lv_obj_get_child_count(list_)) lv_group_focus_obj(lv_obj_get_child(list_, 0));
    }

    lv_obj_t* root_ = nullptr;
    lv_obj_t* chat_ = nullptr;
    lv_obj_t* ta_ = nullptr;
    lv_obj_t* thinking_ = nullptr;
    lv_obj_t* input_ = nullptr;
    lv_obj_t* list_ = nullptr;
    Page page_ = Page::Chat;
    zclaw::Settings cfg_;
    std::string draft_;
    std::shared_ptr<Job> job_;
    std::thread worker_;
    bool failed_ = false;
    int dots_ = 0;
    Done done_;
    RowFn pending_ = nullptr;
};
}  // namespace

std::unique_ptr<App> make_zclaw_app() { return std::make_unique<ZClawApp>(); }

}  // namespace zs
