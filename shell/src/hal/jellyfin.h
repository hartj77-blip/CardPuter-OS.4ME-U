// A small Jellyfin client: sign in, browse your libraries, music (albums,
// artists, playlists) and video (movies, shows, continue watching), search, build the
// URLs tracks and videos stream from, and tell the server what is playing. HTTP goes
// through the curl command, as the rest of the hardware layer uses system
// tools; the JSON reader is hand-written so ZeroShell needs no library.
// Endpoints from Jellyfin's OpenAPI description (api.jellyfin.org).
#pragma once

#include <string>
#include <vector>

namespace zs {

// A signed-in session. The password is never kept; the token is.
struct JfSession {
    std::string server;      // http://host:8096, no trailing slash
    std::string user;
    std::string user_id;
    std::string token;
    std::string device_id;   // any stable string; Jellyfin lists it under Devices
    bool signed_in() const { return !server.empty() && !token.empty(); }
};

struct JfItem {
    // Folder (added last, so modules built before it keep their numbers): a
    // library, or a folder or collection inside one. Opened with JfView::Library.
    enum Kind { Album, Artist, Playlist, Track, Movie, Series, Season, Episode, Video, Folder } kind = Track;
    std::string id;
    std::string name;
    std::string detail;      // artist for albums and tracks; series name for seasons and episodes;
                             // a folder's library type ("music", "movies", "tvshows", "" ...)
    int duration_s = -1;     // tracks and videos
    int resume_s = 0;        // videos: where you stopped watching, 0 = from the start
    int number = -1;         // episode number (seasons: the season's)
    int season = -1;         // episodes: season number
    int year = -1;           // movies
    std::string series_id;   // seasons and episodes

    bool playable_video() const { return kind == Movie || kind == Episode || kind == Video; }
    // "S1E3" for an episode, empty otherwise.
    std::string episode_code() const {
        if (kind != Episode || number < 0) return "";
        return (season >= 0 ? "S" + std::to_string(season) : std::string()) + "E" + std::to_string(number);
    }
};

// What to list. arg: an item id for the *Tracks, ArtistAlbums and Seasons
// views, "seriesId:seasonId" for SeasonEpisodes, the text for Search.
// Libraries lists the user's libraries (every one, as Jellyfin's home
// screen does). Library opens one, or a folder in one: arg is
// "id:type", type being the folder's detail (its library type, may be
// empty). A music library lists its albums, movies its movies, TV its
// shows; any other kind lists what is in the folder, sub-folders included.
// Lists stop at a page (kJfPage rows). The long lists (Albums, Artists,
// Playlists, Movies, Shows, and Library) take the row to start at after a
// '@': "@200" for Albums, "id:type@200" for Library. A list that comes
// back a whole page long may have more.
// (Libraries and Library were added last, so older modules keep their numbers.)
enum class JfView {
    Latest, Albums, Artists, Playlists, Search, AlbumTracks, ArtistAlbums, PlaylistTracks,
    Resume, Movies, Shows, Seasons, SeasonEpisodes, Libraries, Library
};
constexpr int kJfPage = 200;

// Telling the server about playback: shows as "now playing" in its
// dashboard, keeps the resume point, and lets it stop transcoding.
enum class JfReport { Start, Progress, Stop };

namespace jf {

// "nas", "nas:8096" or "https://music.example.com/" as typed by a person,
// made into a base URL. Bare hosts get http:// and Jellyfin's port, 8096.
std::string normalize_server(const std::string& typed);

// The Authorization header value Jellyfin expects from a client.
std::string auth_header(const JfSession& s);

// Where a track streams from: the original file when it is mp3, flac, ogg,
// opus, m4a or wav, otherwise transcoded to mp3 by the server.
std::string stream_url(const JfSession& s, const std::string& item_id);

// Where a movie or episode streams from: HLS (so seeking works), H.264 and
// AAC, transcoded by the server down to max_w x max_h at about kbps.
// play_session ties the stream to the reports below. The address holds no
// token (Jellyfin 12 refuses api_key by default): send auth_header() with
// the playlist and every segment, as mpv does with http-header-fields.
std::string video_url(const JfSession& s, const std::string& item_id, const std::string& play_session,
                      int max_w, int max_h, int kbps);

// Request path and query for a view (after the server address).
std::string list_path(const JfSession& s, JfView view, const std::string& arg);

// Reads AccessToken and the user's id out of the sign-in reply.
bool parse_login(const std::string& json, JfSession& s);
// Reads the "Items" array of a list reply (or a bare array).
bool parse_items(const std::string& json, std::vector<JfItem>& out);

// Network calls (curl). On failure they return false and set error to a
// short sentence a person can act on.
bool login(const std::string& server, const std::string& user, const std::string& password,
           const std::string& device_id, JfSession& out, std::string& error);
bool list(const JfSession& s, JfView view, const std::string& arg, std::vector<JfItem>& out,
          std::string& error);

// The JSON body for a playback report.
std::string report_body(const std::string& item_id, const std::string& play_session, int pos_s, bool paused);
// Sends a playback report in the background: returns at once, and a
// failure is only a missing line in the server's dashboard.
void report(const JfSession& s, JfReport what, const std::string& item_id, const std::string& play_session,
            int pos_s, bool paused);

// For the Dock's video player (zeroos-video), which can't be handed a
// header on its command line: writes ~/.config/zeroos/jellyfin-mpv.conf
// (readable only by you) with the Authorization header and a start time.
// zeroos-video includes it for http(s) addresses.
bool write_dock_options(const JfSession& s, int start_s);

}  // namespace jf
}  // namespace zs
