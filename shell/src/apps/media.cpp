// Media: music from ~/Music, voice notes from the mic, and music and video
// from a Jellyfin server, in three tabs.
//
//   Left/Right   switch tab          Up/Down   choose
//   Enter        play / open         Space     pause / resume
//   N / P        next / previous     S         stop
//   + / -        volume              R         record a voice note
//   X            delete the selected voice note
//   Esc          back one level in Jellyfin
//
// Playing goes through the shell's queue (ui/player.h), so it carries on to
// the next track after you leave the app or the screen turns off.
//
// A Jellyfin movie or episode plays full width in the app (docs/JELLYFIN.md):
//   Space pause   Left/Right -/+10 s   Up/Down +/-1 min   0 from the start
//   + / - volume   M on the monitor (Dock mode)   Esc or S stop
// Video belongs to the app: it stops when you leave Media.
#include <cstdlib>
#include <functional>
#include <random>

#include "apps/common.h"
#include "ui/player.h"
#include "ui/power.h"
#include "ui/prefs.h"

namespace zs {
namespace {
using namespace theme;

constexpr int kListW = 178;
constexpr int kBars = 9;
// The video picture: the whole content area. 16:9 fills it at 267 x 150.
constexpr int kVidW = kScreenW;
constexpr int kVidH = kContentH;
// What we ask the server for: a picture about the size of the screen (it
// scales down, never up) at a bitrate Wi-Fi on a Pi Zero 2 keeps up with.
constexpr int kStreamW = 427, kStreamH = 240, kStreamKbps = 700;
// And for the monitor in Dock mode: 720p, decoded in hardware there.
constexpr int kDockW = 1280, kDockH = 720, kDockKbps = 4000;
constexpr uint32_t kReportEveryMs = 30000;
constexpr uint32_t kOsdMs = 3000;   // the info bar stays up this long after a key

std::string mmss(int ms) {
    if (ms < 0) return "--:--";
    int s = ms / 1000;
    return fmt("%d:%02d", s / 60, s % 60);
}

class MediaApp : public App {
public:
    ~MediaApp() override {
        // Leaving the app: the screen is being torn down, so only stop the
        // video and tell the server where we got to.
        close_video(false);
    }

    void build(lv_obj_t* root) override {
        root_ = root;

        // Tabs
        static const char* const names[kTabs] = {LV_SYMBOL_AUDIO " Music", LV_SYMBOL_AUDIO " Voice",
                                                 LV_SYMBOL_AUDIO " Jellyfin"};
        int x = 8;
        for (int i = 0; i < kTabs; ++i) {
            tabs_[i] = label(root, names[i], &zs_font_12, kMuted);
            lv_obj_set_pos(tabs_[i], x, 3);
            lv_obj_update_layout(tabs_[i]);
            x += lv_obj_get_width(tabs_[i]) + 12;
        }
        tab_line_ = lv_obj_create(root);
        style_plain(tab_line_);
        lv_obj_set_style_bg_color(tab_line_, c(kAccent), 0);
        lv_obj_set_style_bg_opa(tab_line_, LV_OPA_COVER, 0);
        lv_obj_set_size(tab_line_, 52, 2);

        jf_.server = prefs::get_str("jellyfin.server", "");
        jf_.user = prefs::get_str("jellyfin.user", "");
        jf_.user_id = prefs::get_str("jellyfin.user_id", "");
        jf_.token = prefs::get_str("jellyfin.token", "");
        jf_.device_id = prefs::get_str("jellyfin.device", "");
        if (jf_.device_id.empty()) {
            std::random_device rd;
            jf_.device_id = fmt("zeroos-%08x%08x", unsigned(rd()), unsigned(rd()));
            prefs::set_str("jellyfin.device", jf_.device_id);
        }

        // Now-playing panel (shared by both tabs)
        panel_ = lv_obj_create(root);
        style_panel(panel_);
        lv_obj_set_pos(panel_, kListW + 8, 6);
        lv_obj_set_size(panel_, kScreenW - kListW - 14, kContentH - 12);
        lv_obj_remove_flag(panel_, LV_OBJ_FLAG_SCROLLABLE);

        state_ = label(panel_, "", &zs_font_12, kMuted);
        lv_obj_set_pos(state_, 0, 0);
        title_ = label(panel_, "", &zs_font_14, kText);
        lv_obj_set_width(title_, kScreenW - kListW - 26);
        lv_label_set_long_mode(title_, LV_LABEL_LONG_DOT);
        lv_obj_set_pos(title_, 0, 16);
        artist_ = label(panel_, "", &zs_font_12, kMuted);
        lv_obj_set_width(artist_, kScreenW - kListW - 26);
        lv_label_set_long_mode(artist_, LV_LABEL_LONG_DOT);
        lv_obj_set_pos(artist_, 0, 34);

        // Level meter (visual only: animates while audio is running)
        for (int i = 0; i < kBars; ++i) {
            bars_[i] = lv_obj_create(panel_);
            style_plain(bars_[i]);
            lv_obj_set_style_bg_opa(bars_[i], LV_OPA_COVER, 0);
            lv_obj_set_style_radius(bars_[i], 2, 0);
            lv_obj_set_size(bars_[i], 9, 3);
        }

        progress_ = thin_bar(panel_, kScreenW - kListW - 26, 5, kAccent);
        lv_obj_set_pos(progress_, 0, 88);
        lv_bar_set_range(progress_, 0, 1000);
        time_ = label(panel_, "", &zs_font_12, kMuted);
        lv_obj_set_pos(time_, 0, 95);
        hint_ = label(panel_, "", &zs_font_12, kMuted);
        lv_obj_align(hint_, LV_ALIGN_BOTTOM_LEFT, 0, 0);

        show_tab(0);
    }

    void tick() override {
        if (vid_) {
            video_tick();
            return;
        }
        auto& h = hal();
        PlaybackState pb = h.playback();
        RecordState rs = h.record_state();
        if (rs.recording) {
            lv_label_set_text(state_, LV_SYMBOL_STOP "  RECORDING");
            lv_obj_set_style_text_color(state_, c(kBad), 0);
            lv_label_set_text(title_, rs.path.substr(rs.path.rfind('/') + 1).c_str());
            lv_label_set_text(artist_, "Mic, 16 kHz mono");
            lv_bar_set_value(progress_, 0, LV_ANIM_OFF);
            lv_label_set_text(time_, mmss(rs.elapsed_ms).c_str());
        } else if (!pb.path.empty()) {
            lv_label_set_text(state_, pb.paused ? LV_SYMBOL_PAUSE "  PAUSED" : LV_SYMBOL_PLAY "  NOW PLAYING");
            lv_obj_set_style_text_color(state_, c(pb.paused ? kWarn : kGood), 0);
            const QueueItem* q = player_current();
            std::string name = q ? q->title : pb.path.substr(pb.path.rfind('/') + 1);
            if (name != shown_title_) {
                lv_label_set_text(title_, name.c_str());
                shown_title_ = name;
            }
            lv_label_set_text(artist_, q ? q->artist.c_str() : "");
            // The play mark follows the queue, but not under an open text box:
            // rebuilding the list takes the box out of the focus group.
            if (pb.path != listed_playing_ && !input_) rebuild_list();
            int permille = pb.dur_ms > 0 ? int(1000LL * pb.pos_ms / pb.dur_ms) : 0;
            lv_bar_set_value(progress_, permille, LV_ANIM_OFF);
            lv_label_set_text(time_, (mmss(pb.pos_ms) + " / " + mmss(pb.dur_ms) +
                                      fmt("   " LV_SYMBOL_VOLUME_MID " %d%%", h.volume()))
                                         .c_str());
        } else {
            lv_label_set_text(state_, LV_SYMBOL_STOP "  STOPPED");
            lv_obj_set_style_text_color(state_, c(kMuted), 0);
            static const char* const idle[kTabs] = {"Pick a track", "No note playing", "Jellyfin"};
            lv_label_set_text(title_, idle[tab_]);
            shown_title_.clear();
            std::string sub = tab_ == 0   ? fmt("%zu tracks in ~/Music", music_.size())
                              : tab_ == 1 ? fmt("%zu notes in ~/Recordings", notes_.size())
                              : jf_.signed_in() ? jf_.user + " on " + host_of(jf_.server)
                                                : std::string("Not signed in");
            lv_label_set_text(artist_, sub.c_str());
            if (!listed_playing_.empty() && !input_) rebuild_list();   // clear the play mark
            lv_bar_set_value(progress_, 0, LV_ANIM_OFF);
            lv_label_set_text(time_, fmt(LV_SYMBOL_VOLUME_MID " %d%%", h.volume()).c_str());
        }

        // Meter: lively while playing or recording, flat otherwise.
        bool live = rs.recording || (!pb.path.empty() && !pb.paused);
        uint32_t col = rs.recording ? kBad : kAccent;
        for (int i = 0; i < kBars; ++i) {
            int target = live ? 4 + std::rand() % 26 : 3;
            levels_[i] = (levels_[i] * 2 + target) / 3;
            lv_obj_set_size(bars_[i], 9, levels_[i]);
            lv_obj_set_pos(bars_[i], i * 13, 82 - levels_[i]);
            lv_obj_set_style_bg_color(bars_[i], c(live ? col : kSurface2), 0);
        }

        if (rs.recording != was_recording_) {
            was_recording_ = rs.recording;
            if (!rs.recording && tab_ == 1) rebuild_list();  // new note appears
        }
    }

    bool on_key(uint32_t key) override {
        if (vid_) return video_key(key);
        if (input_) return false;   // typing in the text box
        auto& h = hal();
        switch (key) {
            case LV_KEY_LEFT: show_tab((tab_ + kTabs - 1) % kTabs); return true;
            case LV_KEY_RIGHT:
            case 9:  // Tab
                show_tab((tab_ + 1) % kTabs);
                return true;
            case ' ': {
                PlaybackState pb = h.playback();
                if (!pb.path.empty()) h.set_paused(!pb.paused);
                else activate(focused_index());
                return true;
            }
            case 'n': case 'N': step(+1); return true;
            case 'p': case 'P': step(-1); return true;
            case 's': case 'S':
                player_stop();
                return true;
            case '+': case '=':
                h.set_volume(std::min(100, h.volume() + 5));
                return true;
            case '-': case '_':
                h.set_volume(std::max(0, h.volume() - 5));
                return true;
            case 'r': case 'R':
                if (tab_ != 1) show_tab(1);
                if (h.record_state().recording) {
                    h.record_stop();
                    shell_toast("Voice note saved");
                } else {
                    player_stop();
                    shell_toast(h.record_start() ? "Recording... press R to stop" : "No microphone found");
                }
                return true;
            case 'x': case 'X': case LV_KEY_DEL:
                if (tab_ == 1) delete_focused();
                return true;
            case '/':
                if (tab_ == 2 && jf_.signed_in()) {
                    ask_search();
                    return true;
                }
                break;
        }
        return false;
    }

    bool on_back() override {
        if (vid_) {
            close_video(true);
            return true;
        }
        if (input_) {
            close_input();
            return true;
        }
        if (tab_ == 2 && !stack_.empty()) {
            stack_.pop_back();
            rebuild_list();
            return true;
        }
        return false;
    }

private:
    static constexpr int kTabs = 3;

    // One screen of Jellyfin results; the stack of them is where you are.
    struct Level {
        std::string title;
        std::vector<JfItem> items;
        JfView view = JfView::Latest;
    };

    // A list row: episodes say which, movies their year, and anything
    // half-watched where you stopped.
    static std::string row_text(const JfItem& it, JfView view) {
        std::string t = it.name;
        if (it.kind == JfItem::Episode) {
            if (view == JfView::SeasonEpisodes && it.number >= 0) t = std::to_string(it.number) + ". " + it.name;
            else t = it.detail + " " + it.episode_code() + " " + it.name;
        } else if (it.kind == JfItem::Movie && it.year > 0) {
            t += fmt(" (%d)", it.year);
        }
        if (it.playable_video() && it.resume_s > 0) t += "  @" + mmss(it.resume_s * 1000);
        return t;
    }
    // The Jellyfin tab's first screen.
    enum Root { kSearch, kResume, kMovies, kShows, kLatest, kAlbums, kArtists, kPlaylists, kSignOut, kRootCount };

    static std::string host_of(const std::string& url) {
        auto a = url.find("://");
        std::string h = a == std::string::npos ? url : url.substr(a + 3);
        return h.substr(0, h.find_first_of(":/"));
    }

    void show_tab(int t) {
        tab_ = t;
        for (int i = 0; i < kTabs; ++i) lv_obj_set_style_text_color(tabs_[i], c(i == t ? kText : kMuted), 0);
        lv_obj_set_pos(tab_line_, lv_obj_get_x(tabs_[t]), 18);
        lv_obj_set_width(tab_line_, lv_obj_get_width(tabs_[t]));
        static const char* const hints[kTabs] = {"Space pause   N next", "R record   X delete",
                                                 "/ search   Esc back"};
        lv_label_set_text(hint_, hints[t]);
        rebuild_list();
    }

    lv_obj_t* add_row(const char* symbol, const std::string& text, int index) {
        lv_obj_t* b = list_button(list_, symbol, text.c_str(), on_click, this);
        lv_obj_set_user_data(b, (void*)(intptr_t)index);
        lv_obj_t* lbl = lv_obj_get_child(b, 1);
        if (lbl) lv_label_set_long_mode(lbl, LV_LABEL_LONG_DOT);
        return b;
    }

    void rebuild_list() {
        int keep = focused_index();
        if (list_) {
            lv_group_remove_all_objs(group);
            lv_obj_delete(list_);
        }
        music_ = hal().music_library();
        notes_ = hal().recordings();
        list_ = make_list(root_, 4, 24, kListW, kContentH - 28);
        listed_playing_ = hal().playback().path;
        lv_obj_t* focus = nullptr;

        if (tab_ == 2) {
            focus = build_jellyfin(keep);
        } else {
            const auto& items = tab_ == 0 ? music_ : notes_;
            if (items.empty())
                lv_list_add_text(list_, tab_ == 0 ? "Copy music to ~/Music" : "Press R to record a note");
            for (size_t i = 0; i < items.size(); ++i) {
                const Track& t = items[i];
                std::string text = t.title;
                if (tab_ == 1 && t.duration_s >= 0) text += "  " + mmss(t.duration_s * 1000);
                bool now = t.path == listed_playing_;
                lv_obj_t* b = add_row(now ? LV_SYMBOL_PLAY : (tab_ == 0 ? LV_SYMBOL_AUDIO : LV_SYMBOL_FILE), text, int(i));
                if (now) focus = b;
            }
        }
        finish_list(list_);
        if (focus) lv_group_focus_obj(focus);
        if (input_) lv_obj_move_foreground(input_);
    }

    // Rows for the Jellyfin tab; returns the row to focus, if any.
    lv_obj_t* build_jellyfin(int keep) {
        lv_obj_t* focus = nullptr;
        if (!jf_.signed_in()) {
            lv_list_add_text(list_, "Music from your server");
            add_row(LV_SYMBOL_RIGHT, "Sign in...", 0);
            return nullptr;
        }
        if (stack_.empty()) {
            static const char* const rows[kRootCount] = {"Search...", "Continue watching", "Movies", "TV shows",
                                                         "Latest albums", "Albums", "Artists", "Playlists",
                                                         "Sign out"};
            static const char* const icons[kRootCount] = {
                ZS_ICON_SEARCH,      LV_SYMBOL_PLAY,      LV_SYMBOL_VIDEO, LV_SYMBOL_VIDEO, LV_SYMBOL_BELL,
                LV_SYMBOL_DIRECTORY, LV_SYMBOL_DIRECTORY, LV_SYMBOL_LIST,  LV_SYMBOL_CLOSE};
            for (int i = 0; i < kRootCount; ++i) {
                lv_obj_t* b = add_row(icons[i], rows[i], i);
                if (i == keep) focus = b;
            }
            return focus;
        }
        const Level& lv = stack_.back();
        lv_list_add_text(list_, lv.title.c_str());
        if (lv.items.empty()) lv_list_add_text(list_, "Nothing here");
        for (size_t i = 0; i < lv.items.size(); ++i) {
            const JfItem& it = lv.items[i];
            bool now = it.kind == JfItem::Track && jf::stream_url(jf_, it.id) == listed_playing_;
            const char* icon = now ? LV_SYMBOL_PLAY
                               : it.kind == JfItem::Track ? LV_SYMBOL_AUDIO
                               : it.playable_video() ? LV_SYMBOL_VIDEO
                               : it.kind == JfItem::Playlist ? LV_SYMBOL_LIST : LV_SYMBOL_DIRECTORY;
            lv_obj_t* b = add_row(icon, row_text(it, lv.view), int(i));
            if (now || (!focus && int(i) == keep)) focus = b;
        }
        return focus;
    }

    int focused_index() {
        lv_obj_t* f = group ? lv_group_get_focused(group) : nullptr;
        if (!f || !list_ || lv_obj_get_parent(f) != list_) return -1;
        return int((intptr_t)lv_obj_get_user_data(f));
    }

    // Enter on row i of the current list.
    void activate(int i) {
        if (i < 0) i = 0;
        if (tab_ == 2) {
            activate_jellyfin(i);
            return;
        }
        const auto& items = tab_ == 0 ? music_ : notes_;
        if (i >= int(items.size())) return;
        std::vector<QueueItem> q;
        int at = 0;
        if (tab_ == 0) {   // music continues down the list
            for (auto& t : music_) q.push_back({t.path, t.title, t.artist, t.duration_s, ""});
            at = i;
        } else {
            q.push_back({notes_[i].path, notes_[i].title, notes_[i].artist, notes_[i].duration_s, ""});
        }
        if (!player_play(std::move(q), at)) shell_toast("Can't play " + items[i].title);
        shown_title_.clear();
        rebuild_list();
    }

    void activate_jellyfin(int i) {
        if (!jf_.signed_in()) {
            sign_in();
            return;
        }
        if (stack_.empty()) {
            switch (i) {
                case kSearch: ask_search(); break;
                case kResume: open_level("Continue watching", JfView::Resume, ""); break;
                case kMovies: open_level("Movies", JfView::Movies, ""); break;
                case kShows: open_level("TV shows", JfView::Shows, ""); break;
                case kLatest: open_level("Latest albums", JfView::Latest, ""); break;
                case kAlbums: open_level("Albums", JfView::Albums, ""); break;
                case kArtists: open_level("Artists", JfView::Artists, ""); break;
                case kPlaylists: open_level("Playlists", JfView::Playlists, ""); break;
                case kSignOut:
                    for (const char* k : {"jellyfin.user_id", "jellyfin.token"}) prefs::set_str(k, "");
                    jf_.user_id.clear();
                    jf_.token.clear();
                    shell_toast("Signed out of Jellyfin");
                    rebuild_list();
                    break;
            }
            return;
        }
        // Copies: opening a level reallocates the stack this item lives in.
        const std::vector<JfItem> items = stack_.back().items;
        if (i >= int(items.size())) return;
        const JfItem it = items[i];
        switch (it.kind) {
            case JfItem::Album: open_level(it.name, JfView::AlbumTracks, it.id); break;
            case JfItem::Artist: open_level(it.name, JfView::ArtistAlbums, it.id); break;
            case JfItem::Playlist: open_level(it.name, JfView::PlaylistTracks, it.id); break;
            case JfItem::Series: open_level(it.name, JfView::Seasons, it.id); break;
            case JfItem::Season:
                if (it.series_id.empty()) {
                    shell_toast("The server didn't say which show");
                    break;
                }
                open_level(it.detail.empty() ? it.name : it.detail + ", " + it.name, JfView::SeasonEpisodes,
                           it.series_id + ":" + it.id);
                break;
            case JfItem::Movie:
            case JfItem::Episode:
            case JfItem::Video: open_video(items, i); break;
            case JfItem::Track: {
                // Queue every track on this screen, starting with this one.
                std::vector<QueueItem> q;
                int at = 0;
                std::string auth = jf::auth_header(jf_);
                for (size_t k = 0; k < items.size(); ++k) {
                    if (items[k].kind != JfItem::Track) continue;
                    if (int(k) == i) at = int(q.size());
                    q.push_back({jf::stream_url(jf_, items[k].id), items[k].name, items[k].detail,
                                 items[k].duration_s, auth});
                }
                if (!player_play(std::move(q), at)) shell_toast("Can't play " + it.name);
                shown_title_.clear();
                rebuild_list();
                break;
            }
        }
    }

    // Ask the server for a list and show it as a new level.
    void open_level(const std::string& title, JfView view, const std::string& arg) {
        shell_toast("Loading...");
        lv_refr_now(nullptr);   // the request blocks; show the notice first
        Level lv;
        lv.title = title;
        lv.view = view;
        std::string error;
        if (!hal().jellyfin_list(jf_, view, arg, lv.items, error)) {
            shell_toast(error);
            return;
        }
        shell_toast(lv.items.empty() ? "Nothing found" : fmt("%zu found", lv.items.size()));
        stack_.push_back(std::move(lv));
        rebuild_list();
        if (lv_obj_get_child_count(list_) > 1) lv_group_focus_obj(lv_obj_get_child(list_, 1));
    }

    void ask_search() {
        ask("Search Jellyfin", "", false, [this](const std::string& text) {
            if (!text.empty()) open_level("\"" + text + "\"", JfView::Search, text);
        });
    }

    // Three questions, then sign in. Only the token is kept, not the password.
    void sign_in() {
        ask("Jellyfin server (name or address)", jf_.server, false, [this](const std::string& server) {
            if (server.empty()) return;
            typed_server_ = server;
            ask("User name", jf_.user, false, [this](const std::string& user) {
                if (user.empty()) return;
                typed_user_ = user;
                ask("Password", "", true, [this](const std::string& password) {
                    shell_toast("Signing in...");
                    lv_refr_now(nullptr);
                    JfSession s;
                    std::string error;
                    if (!hal().jellyfin_login(typed_server_, typed_user_, password, jf_.device_id, s, error)) {
                        shell_toast(error);
                        return;
                    }
                    jf_ = s;
                    prefs::set_str("jellyfin.server", s.server);
                    prefs::set_str("jellyfin.user", s.user);
                    prefs::set_str("jellyfin.user_id", s.user_id);
                    prefs::set_str("jellyfin.token", s.token);
                    stack_.clear();
                    shell_toast("Signed in as " + s.user);
                    rebuild_list();
                });
            });
        });
    }

    // One-line text box over the screen. Enter accepts, Esc cancels.
    using Done = std::function<void(const std::string&)>;
    void ask(const char* title, const std::string& initial, bool password, Done done) {
        done_ = std::move(done);
        input_ = lv_obj_create(root_);
        style_panel(input_);
        lv_obj_set_size(input_, 300, 74);
        lv_obj_center(input_);
        lv_obj_set_style_border_color(input_, c(kAccent), 0);
        lv_obj_set_style_border_width(input_, 1, 0);
        label(input_, title, &zs_font_12, kMuted);
        lv_obj_t* ta = lv_textarea_create(input_);
        lv_textarea_set_one_line(ta, true);
        lv_textarea_set_password_mode(ta, password);
        lv_textarea_set_text(ta, initial.c_str());
        lv_obj_set_width(ta, 280);
        lv_obj_align(ta, LV_ALIGN_BOTTOM_MID, 0, 0);
        lv_obj_add_event_cb(ta, [](lv_event_t* e) {
            auto* self = static_cast<MediaApp*>(lv_event_get_user_data(e));
            std::string v = lv_textarea_get_text(lv_event_get_target_obj(e));
            Done d = std::move(self->done_);
            // Enter is still down: without this its release would also
            // "click" whichever list row gets the focus next.
            if (lv_indev_t* in = lv_indev_active()) lv_indev_wait_release(in);
            self->close_input();
            if (d) d(v);   // may open the next box
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
        if (int n = int(lv_obj_get_child_count(list_))) {
            // Focus goes back to the list (its first button).
            for (int i = 0; i < n; ++i) {
                lv_obj_t* ch = lv_obj_get_child(list_, i);
                if (lv_obj_check_type(ch, &lv_list_button_class)) {
                    lv_group_focus_obj(ch);
                    break;
                }
            }
        }
    }

    void step(int dir) {
        if (player_step(dir)) {
            shown_title_.clear();
            rebuild_list();
        } else {
            activate(focused_index());
        }
    }

    void delete_focused() {
        int i = focused_index();
        if (i < 0 || i >= int(notes_.size())) return;
        std::string name = notes_[i].title;
        // Ask once: the second press within 3 s on the same note deletes it.
        if (confirm_path_ != notes_[i].path || lv_tick_elaps(confirm_at_) > 3000) {
            confirm_path_ = notes_[i].path;
            confirm_at_ = lv_tick_get();
            shell_toast("Press again to delete " + name);
            return;
        }
        confirm_path_.clear();
        if (hal().delete_recording(notes_[i].path)) {
            shell_toast("Deleted " + name);
            rebuild_list();
        }
    }

    static void on_click(lv_event_t* e) {
        auto* self = static_cast<MediaApp*>(lv_event_get_user_data(e));
        self->pending_ = int((intptr_t)lv_obj_get_user_data(lv_event_get_target_obj(e)));
        // Deferred: activating rebuilds the list, which deletes the clicked button.
        lv_async_call([](void* p) {
            auto* s = static_cast<MediaApp*>(p);
            s->activate(s->pending_);
        }, self);
    }

    // ---- Video ----------------------------------------------------------

    static std::string new_session_id() {
        std::random_device rd;
        return fmt("%08x%08x%08x%08x", unsigned(rd()), unsigned(rd()), unsigned(rd()), unsigned(rd()));
    }

    // Play items[i] (a movie or episode) on the screen. The rest of the list
    // stays with it, so an episode can go on to the next one.
    void open_video(const std::vector<JfItem>& items, int i) {
        const JfItem& it = items[size_t(i)];
        close_video(true);
        player_stop();   // music and video share the speaker
        std::string session = new_session_id();
        std::string url = jf::video_url(jf_, it.id, session, kStreamW, kStreamH, kStreamKbps);
        if (url.empty()) {
            shell_toast("Can't play " + it.name);
            return;
        }
        vid_pixels_.assign(size_t(kVidW) * kVidH, 0);
        std::string error;
        int start = it.resume_s > 0 ? it.resume_s : 0;
        if (!hal().video_open(url, jf::auth_header(jf_), start, kVidW, kVidH, error)) {
            shell_toast(error.empty() ? "Can't play " + it.name : error);
            vid_pixels_.clear();
            return;
        }
        vid_items_ = items;
        vid_at_ = i;
        vid_item_ = it;
        vid_session_ = session;
        vid_reported_at_ = lv_tick_get();
        vid_started_ = false;
        vid_auto_paused_ = false;
        build_video_view();
        show_osd();
        if (start > 0) shell_toast("Resuming at " + mmss(start * 1000) + "   0: from the start");
    }

    void build_video_view() {
        vid_ = lv_obj_create(root_);
        style_plain(vid_);
        lv_obj_set_pos(vid_, 0, 0);
        lv_obj_set_size(vid_, kVidW, kVidH);
        lv_obj_set_style_bg_color(vid_, c(0x000000), 0);
        lv_obj_set_style_bg_opa(vid_, LV_OPA_COVER, 0);
        lv_obj_remove_flag(vid_, LV_OBJ_FLAG_SCROLLABLE);

        vid_dsc_ = lv_image_dsc_t{};
        vid_dsc_.header.magic = LV_IMAGE_HEADER_MAGIC;
        vid_dsc_.header.cf = LV_COLOR_FORMAT_RGB565;
        vid_dsc_.header.w = kVidW;
        vid_dsc_.header.h = kVidH;
        vid_dsc_.header.stride = kVidW * 2;
        vid_dsc_.data = reinterpret_cast<const uint8_t*>(vid_pixels_.data());
        vid_dsc_.data_size = uint32_t(vid_pixels_.size() * 2);
        vid_img_ = lv_image_create(vid_);
        lv_image_set_src(vid_img_, &vid_dsc_);
        lv_obj_set_pos(vid_img_, 0, 0);

        vid_wait_ = label(vid_, "Loading...", &zs_font_14, kText);
        lv_obj_center(vid_wait_);

        // The info bar along the bottom: title, time, a progress line.
        vid_osd_ = lv_obj_create(vid_);
        style_plain(vid_osd_);
        lv_obj_set_size(vid_osd_, kVidW, 40);
        lv_obj_align(vid_osd_, LV_ALIGN_BOTTOM_MID, 0, 0);
        lv_obj_set_style_bg_color(vid_osd_, c(0x000000), 0);
        lv_obj_set_style_bg_opa(vid_osd_, LV_OPA_70, 0);
        lv_obj_remove_flag(vid_osd_, LV_OBJ_FLAG_SCROLLABLE);
        vid_title_ = label(vid_osd_, "", &zs_font_12, kText);
        lv_obj_set_width(vid_title_, kVidW - 12);
        lv_label_set_long_mode(vid_title_, LV_LABEL_LONG_DOT);
        lv_obj_set_pos(vid_title_, 6, 2);
        vid_bar_ = thin_bar(vid_osd_, kVidW - 12, 3, kAccent);
        lv_bar_set_range(vid_bar_, 0, 1000);
        lv_obj_set_pos(vid_bar_, 6, 19);
        vid_time_ = label(vid_osd_, "", &zs_font_12, kMuted);
        lv_obj_set_pos(vid_time_, 6, 24);
        vid_hint_ = label(vid_osd_, "Space pause  " LV_SYMBOL_LEFT LV_SYMBOL_RIGHT " seek  Esc stop", &zs_font_12,
                          kMuted);
        lv_obj_align(vid_hint_, LV_ALIGN_TOP_RIGHT, -6, 24);
        std::string t = vid_item_.kind == JfItem::Episode
                            ? vid_item_.detail + " " + vid_item_.episode_code() + " " + vid_item_.name
                            : vid_item_.name;
        lv_label_set_text(vid_title_, t.c_str());

        if (group) lv_group_focus_obj(nullptr);
        // Pictures come faster than the shell's 10 Hz tick.
        vid_timer_ = lv_timer_create([](lv_timer_t* t) {
            static_cast<MediaApp*>(lv_timer_get_user_data(t))->video_pump();
        }, 20, this);
    }

    void video_pump() {
        if (!vid_) return;
        if (power().screen_off()) return;   // nobody is looking; the tick pauses it
        if (hal().video_frame(vid_pixels_.data())) {
            lv_obj_invalidate(vid_img_);
            if (!vid_started_) {
                vid_started_ = true;
                hal().jellyfin_report(jf_, JfReport::Start, vid_item_.id, vid_session_, vid_item_.resume_s, false);
            }
        }
    }

    void video_tick() {
        VideoState st = hal().video();
        uint32_t now = lv_tick_get();
        if (!st.open) {
            video_ended(st);
            return;
        }
        if (power().screen_off()) {
            // Face down or timed out: stop watching, keep the place.
            if (!st.paused) {
                hal().video_pause(true);
                vid_auto_paused_ = true;
            }
        } else if (!st.paused) {
            power().stay_awake(now);
        }
        vid_pos_s_ = st.pos_ms / 1000;
        if (now - vid_reported_at_ >= kReportEveryMs) {
            vid_reported_at_ = now;
            hal().jellyfin_report(jf_, JfReport::Progress, vid_item_.id, vid_session_, vid_pos_s_, st.paused);
        }

        const char* wait = st.loading ? "Loading..." : st.paused ? LV_SYMBOL_PAUSE "  Paused" : "";
        lv_label_set_text(vid_wait_, wait);
        if (*wait) lv_obj_remove_flag(vid_wait_, LV_OBJ_FLAG_HIDDEN);
        else lv_obj_add_flag(vid_wait_, LV_OBJ_FLAG_HIDDEN);

        bool osd = st.paused || st.loading || now - vid_osd_at_ < kOsdMs;
        if (osd) lv_obj_remove_flag(vid_osd_, LV_OBJ_FLAG_HIDDEN);
        else lv_obj_add_flag(vid_osd_, LV_OBJ_FLAG_HIDDEN);
        if (osd) {
            int permille = st.dur_ms > 0 ? int(1000LL * st.pos_ms / st.dur_ms) : 0;
            lv_bar_set_value(vid_bar_, permille, LV_ANIM_OFF);
            lv_label_set_text(vid_time_, (mmss(st.pos_ms) + " / " + mmss(st.dur_ms) +
                                          fmt("   " LV_SYMBOL_VOLUME_MID " %d%%", hal().volume()))
                                             .c_str());
        }
    }

    void video_ended(const VideoState& st) {
        bool finished = st.finished;
        std::string error = st.error;
        // Finished: the server should forget the resume point.
        if (finished) vid_pos_s_ = vid_item_.duration_s > 0 ? vid_item_.duration_s : vid_pos_s_;
        close_video(true);
        if (!error.empty()) {
            shell_toast(error);
            return;
        }
        if (!finished) return;
        // An episode: on to the next one in the list.
        int next = vid_at_ + 1;
        // (Only within the season: a list like Continue watching mixes shows.)
        if (vid_item_.kind == JfItem::Episode && next < int(vid_items_.size()) &&
            vid_items_[size_t(next)].kind == JfItem::Episode &&
            vid_items_[size_t(next)].series_id == vid_item_.series_id &&
            vid_items_[size_t(next)].season == vid_item_.season &&
            vid_items_[size_t(next)].number > vid_item_.number) {
            std::vector<JfItem> items = vid_items_;
            items[size_t(next)].resume_s = 0;
            shell_toast("Next: " + items[size_t(next)].name);
            open_video(items, next);
        } else {
            shell_toast("Finished " + vid_item_.name);
        }
    }

    void show_osd() { vid_osd_at_ = lv_tick_get(); }

    bool video_key(uint32_t key) {
        auto& h = hal();
        show_osd();
        switch (key) {
            case ' ':
            case LV_KEY_ENTER: {
                VideoState st = h.video();
                h.video_pause(!st.paused);
                vid_auto_paused_ = false;
                hal().jellyfin_report(jf_, JfReport::Progress, vid_item_.id, vid_session_, st.pos_ms / 1000,
                                      !st.paused);
                return true;
            }
            case LV_KEY_LEFT: h.video_seek(-10, false); return true;
            case LV_KEY_RIGHT: h.video_seek(+10, false); return true;
            case LV_KEY_UP: h.video_seek(+60, false); return true;
            case LV_KEY_DOWN: h.video_seek(-60, false); return true;
            case '0': h.video_seek(0, true); return true;
            case '+': case '=':
                h.set_volume(std::min(100, h.volume() + 5));
                return true;
            case '-': case '_':
                h.set_volume(std::max(0, h.volume() - 5));
                return true;
            case 's': case 'S': close_video(true); return true;
            case 'm': case 'M': video_to_monitor(); return true;
            case LV_KEY_ESC: return false;   // on_back() closes it
        }
        return true;   // the list is underneath: keep keys from it
    }

    // Dock mode: hand the video to the monitor's player, from where we are.
    void video_to_monitor() {
        if (!hal().display().dock_running) {
            shell_toast("Start Dock mode for the monitor first");
            return;
        }
        JfItem it = vid_item_;
        int at = hal().video().pos_ms / 1000;
        std::string url = jf::video_url(jf_, it.id, new_session_id(), kDockW, kDockH, kDockKbps);
        close_video(true);
        if (!jf::write_dock_options(jf_, at) || !hal().dock_open("video", url)) {
            shell_toast("Can't open the video on the monitor");
            return;
        }
        shell_toast("Playing on the monitor");
    }

    // Stop the video, tell the server where it stopped and, with ui, take
    // the picture off the screen.
    void close_video(bool ui) {
        if (vid_timer_) {
            lv_timer_delete(vid_timer_);
            vid_timer_ = nullptr;
        }
        if (!vid_) return;
        VideoState st = hal().video();
        if (st.open) vid_pos_s_ = st.pos_ms / 1000;
        hal().video_close();
        hal().jellyfin_report(jf_, JfReport::Stop, vid_item_.id, vid_session_, vid_pos_s_, false);
        if (ui) {
            lv_obj_delete(vid_);
            // The list may have a stale resume point; show the place we stopped.
            for (auto& lvl : stack_)
                for (auto& x : lvl.items)
                    if (x.id == vid_item_.id) x.resume_s = st.finished ? 0 : vid_pos_s_;
            rebuild_list();
        }
        vid_ = nullptr;
        vid_pixels_.clear();
        vid_pixels_.shrink_to_fit();
    }

    std::string confirm_path_;   // voice note waiting for a second X to delete
    uint32_t confirm_at_ = 0;
    lv_obj_t* root_ = nullptr;
    lv_obj_t* tabs_[kTabs] = {};
    lv_obj_t* tab_line_ = nullptr;
    lv_obj_t* list_ = nullptr;
    lv_obj_t *panel_, *state_, *title_, *artist_, *progress_, *time_, *hint_;
    lv_obj_t* bars_[kBars] = {};
    int levels_[kBars] = {};
    std::vector<Track> music_, notes_;
    std::string shown_title_;
    std::string listed_playing_;   // what the list last marked as playing
    int tab_ = 0;
    int pending_ = -1;

    JfSession jf_;
    std::vector<Level> stack_;
    lv_obj_t* input_ = nullptr;
    Done done_;
    std::string typed_server_, typed_user_;
    bool was_recording_ = false;

    // Video
    lv_obj_t* vid_ = nullptr;   // the player view, over everything; null when no video
    lv_obj_t *vid_img_ = nullptr, *vid_wait_ = nullptr, *vid_osd_ = nullptr, *vid_title_ = nullptr,
             *vid_bar_ = nullptr, *vid_time_ = nullptr, *vid_hint_ = nullptr;
    lv_timer_t* vid_timer_ = nullptr;
    lv_image_dsc_t vid_dsc_{};
    std::vector<uint16_t> vid_pixels_;
    std::vector<JfItem> vid_items_;   // the list it was started from
    int vid_at_ = -1;
    JfItem vid_item_;
    std::string vid_session_;
    uint32_t vid_reported_at_ = 0, vid_osd_at_ = 0;
    int vid_pos_s_ = 0;
    bool vid_started_ = false, vid_auto_paused_ = false;
};
}  // namespace

std::unique_ptr<App> make_media_app() { return std::make_unique<MediaApp>(); }

}  // namespace zs
