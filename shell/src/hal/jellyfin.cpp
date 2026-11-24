#include "hal/jellyfin.h"

#include <spawn.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <unistd.h>

#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <utility>

#include "platform/watchdog.h"

namespace zs::jf {
namespace {

constexpr int kPage = kJfPage;   // most rows fetched for one list
constexpr int kTimeoutS = 8;   // these calls run on the UI thread

// ---- a small JSON reader: just enough to walk objects and arrays ----------

struct Json {
    enum Type { Null, Bool, Number, String, Array, Object } type = Null;
    bool b = false;
    double num = 0;
    std::string str;
    std::vector<Json> items;                             // Array
    std::vector<std::pair<std::string, Json>> fields;    // Object

    const Json& operator[](const char* key) const {
        static const Json none;
        for (auto& f : fields)
            if (f.first == key) return f.second;
        return none;
    }
    const std::string& text() const { return str; }
};

class Parser {
public:
    explicit Parser(const std::string& s) : s_(s) {}
    bool parse(Json& out) {
        bool ok = value(out, 0);
        ws();
        return ok && i_ == s_.size();
    }

private:
    void ws() {
        while (i_ < s_.size() && std::isspace((unsigned char)s_[i_])) ++i_;
    }
    bool lit(const char* word) {
        size_t n = std::strlen(word);
        if (s_.compare(i_, n, word) != 0) return false;
        i_ += n;
        return true;
    }
    static void utf8(std::string& o, unsigned cp) {
        if (cp < 0x80) o += char(cp);
        else if (cp < 0x800) { o += char(0xC0 | (cp >> 6)); o += char(0x80 | (cp & 0x3F)); }
        else if (cp < 0x10000) {
            o += char(0xE0 | (cp >> 12)); o += char(0x80 | ((cp >> 6) & 0x3F)); o += char(0x80 | (cp & 0x3F));
        } else {
            o += char(0xF0 | (cp >> 18)); o += char(0x80 | ((cp >> 12) & 0x3F));
            o += char(0x80 | ((cp >> 6) & 0x3F)); o += char(0x80 | (cp & 0x3F));
        }
    }
    bool hex4(unsigned& v) {
        if (i_ + 4 > s_.size()) return false;
        v = 0;
        for (int k = 0; k < 4; ++k) {
            char ch = s_[i_++];
            if (!std::isxdigit((unsigned char)ch)) return false;
            v = v * 16 + unsigned(std::isdigit((unsigned char)ch) ? ch - '0' : std::tolower(ch) - 'a' + 10);
        }
        return true;
    }
    bool string(std::string& o) {
        if (i_ >= s_.size() || s_[i_] != '"') return false;
        ++i_;
        while (i_ < s_.size()) {
            char ch = s_[i_++];
            if (ch == '"') return true;
            if (ch != '\\') { o += ch; continue; }
            if (i_ >= s_.size()) return false;
            char e = s_[i_++];
            switch (e) {
                case 'n': o += '\n'; break;
                case 't': o += '\t'; break;
                case 'r': o += '\r'; break;
                case 'b': o += '\b'; break;
                case 'f': o += '\f'; break;
                case 'u': {
                    unsigned cp;
                    if (!hex4(cp)) return false;
                    if (cp >= 0xD800 && cp < 0xDC00 && s_.compare(i_, 2, "\\u") == 0) {
                        size_t back = i_;
                        unsigned lo;
                        i_ += 2;
                        if (!hex4(lo)) return false;
                        if (lo >= 0xDC00 && lo < 0xE000) {
                            cp = 0x10000 + ((cp - 0xD800) << 10) + (lo - 0xDC00);
                        } else {
                            i_ = back;   // not a pair: the next escape stands alone
                            cp = 0xFFFD;
                        }
                    } else if (cp >= 0xD800 && cp < 0xE000) {
                        cp = 0xFFFD;     // a lone surrogate is not a character
                    }
                    utf8(o, cp);
                    break;
                }
                default: o += e;   // \" \\ \/
            }
        }
        return false;
    }
    bool value(Json& out, int depth) {
        if (depth > 32) return false;
        ws();
        if (i_ >= s_.size()) return false;
        char ch = s_[i_];
        if (ch == '{') {
            out.type = Json::Object;
            ++i_;
            ws();
            if (i_ < s_.size() && s_[i_] == '}') { ++i_; return true; }
            while (true) {
                ws();
                std::string key;
                if (!string(key)) return false;
                ws();
                if (i_ >= s_.size() || s_[i_++] != ':') return false;
                Json v;
                if (!value(v, depth + 1)) return false;
                out.fields.emplace_back(std::move(key), std::move(v));
                ws();
                if (i_ >= s_.size()) return false;
                if (s_[i_] == ',') { ++i_; continue; }
                if (s_[i_] == '}') { ++i_; return true; }
                return false;
            }
        }
        if (ch == '[') {
            out.type = Json::Array;
            ++i_;
            ws();
            if (i_ < s_.size() && s_[i_] == ']') { ++i_; return true; }
            while (true) {
                Json v;
                if (!value(v, depth + 1)) return false;
                out.items.push_back(std::move(v));
                ws();
                if (i_ >= s_.size()) return false;
                if (s_[i_] == ',') { ++i_; continue; }
                if (s_[i_] == ']') { ++i_; return true; }
                return false;
            }
        }
        if (ch == '"') {
            out.type = Json::String;
            return string(out.str);
        }
        if (lit("true")) { out.type = Json::Bool; out.b = true; return true; }
        if (lit("false")) { out.type = Json::Bool; return true; }
        if (lit("null")) return true;
        char* end = nullptr;
        out.num = std::strtod(s_.c_str() + i_, &end);
        if (end == s_.c_str() + i_) return false;
        out.type = Json::Number;
        i_ = size_t(end - s_.c_str());
        return true;
    }

    const std::string& s_;
    size_t i_ = 0;
};

std::string json_escape(const std::string& s) {
    std::string o;
    for (unsigned char ch : s) {
        if (ch == '"' || ch == '\\') { o += '\\'; o += char(ch); }
        else if (ch < 0x20) { char b[8]; std::snprintf(b, sizeof b, "\\u%04x", ch); o += b; }
        else o += char(ch);
    }
    return o;
}

std::string url_encode(const std::string& s) {
    std::string o;
    for (unsigned char ch : s) {
        if (std::isalnum(ch) || ch == '-' || ch == '_' || ch == '.' || ch == '~') o += char(ch);
        else { char b[4]; std::snprintf(b, sizeof b, "%%%02X", ch); o += b; }
    }
    return o;
}

std::string shell_quote(const std::string& s) {
    std::string r = "'";
    for (char ch : s) {
        if (ch == '\'') r += "'\\''";
        else r += ch;
    }
    return r + "'";
}

// Jellyfin ids are hex, with or without dashes. Anything else never goes
// into a URL.
bool safe_id(const std::string& id) {
    if (id.empty() || id.size() > 64) return false;
    for (unsigned char ch : id)
        if (!std::isxdigit(ch) && ch != '-') return false;
    return true;
}

struct Reply {
    int status = 0;        // HTTP status, 0 = no answer
    int curl_exit = -1;    // curl's exit code, -1 = it didn't run
    std::string body;
    std::string final_url; // where the answer came from (after redirects, if followed)
};

// A private (0600) temporary file holding data, for curl to read. "" on failure.
std::string private_file(const std::string& data) {
    char tmp[] = "/tmp/zeroos-jf-XXXXXX";
    int fd = mkstemp(tmp);   // created 0600
    if (fd < 0) return "";
    bool ok = write(fd, data.data(), data.size()) == ssize_t(data.size());
    close(fd);
    if (!ok) {
        unlink(tmp);
        return "";
    }
    return tmp;
}

// The curl command for a request (without the address), and the temporary
// files it reads: the headers (the token never goes on a command line, where
// any process could read it) and the body, if any. False if a file can't be
// made. Redirects are not followed: curl would send the token (a custom
// header) and the password (a re-POSTed body) to wherever the server points.
bool curl_command(const std::string& auth, const std::string* body, std::string& cmd,
                  std::vector<std::string>& tmps) {
    tmps.clear();
    std::string headers = "Accept: application/json\n";
    if (!auth.empty()) headers += "Authorization: " + auth + "\n";
    if (body) headers += "Content-Type: application/json\n";
    std::string hfile = private_file(headers);
    if (hfile.empty()) return false;
    tmps.push_back(hfile);
    cmd = "curl -sS --proto =http,https --max-time " + std::to_string(kTimeoutS) + " --connect-timeout 4 -H @" +
          hfile;
    if (!body) return true;
    std::string bfile = private_file(*body);
    if (bfile.empty()) {
        unlink(hfile.c_str());
        tmps.clear();
        return false;
    }
    tmps.push_back(bfile);
    cmd += " --data-binary @" + bfile;
    return true;
}

void remove_all(const std::vector<std::string>& files) {
    for (auto& f : files) unlink(f.c_str());
}

// Runs a curl command line and reads the body, final address and status.
Reply run_curl(const std::string& cmd_line) {
    Reply r;
    // Up to kTimeoutS on the UI thread, and a screen may make a few calls in
    // a row: a wait, not a hang (platform/watchdog.h).
    WatchdogGrace grace;
    std::string cmd = cmd_line + " 2>/dev/null";
    if (FILE* p = popen(cmd.c_str(), "r")) {
        char buf[4096];
        size_t n;
        while ((n = fread(buf, 1, sizeof buf, p)) > 0) r.body.append(buf, n);
        int st = pclose(p);
        if (st != -1 && WIFEXITED(st)) r.curl_exit = WEXITSTATUS(st);
    }
    auto nl = r.body.rfind('\n');
    if (nl != std::string::npos) {
        r.status = std::atoi(r.body.c_str() + nl + 1);
        r.body.erase(nl);
        auto nl2 = r.body.rfind('\n');
        if (nl2 != std::string::npos) {
            r.final_url = r.body.substr(nl2 + 1);
            r.body.erase(nl2);
        }
    }
    return r;
}

// GET, or POST when body is set. The body travels in a private temporary
// file so a password never shows in the process list; so does the token.
Reply http(const std::string& url, const std::string& auth, const std::string* body) {
    std::string cmd;
    std::vector<std::string> tmps;
    if (!curl_command(auth, body, cmd, tmps)) return Reply{};
    cmd += " -w '\\n%{url_effective}\\n%{http_code}' -- " + shell_quote(url);
    Reply r = run_curl(cmd);
    remove_all(tmps);
    return r;
}

std::string lower(std::string s) {
    for (char& ch : s) ch = char(std::tolower((unsigned char)ch));
    return s;
}

// "https://Host:8920/jf/x" -> "host:8920" (scheme, path and user info gone).
std::string host_port(const std::string& url) {
    auto p = url.find("://");
    std::string rest = p == std::string::npos ? url : url.substr(p + 3);
    rest = rest.substr(0, rest.find_first_of("/?#"));
    auto at = rest.rfind('@');
    if (at != std::string::npos) rest = rest.substr(at + 1);
    return lower(rest);
}

std::string host_only(const std::string& url) {
    std::string hp = host_port(url);
    if (!hp.empty() && hp[0] == '[') return hp.substr(0, hp.find(']') + 1);
    return hp.substr(0, hp.find(':'));
}

const char kPublicInfo[] = "/System/Info/Public";

// Is a Jellyfin server at this base address? Asks for its public info (no
// sign-in, no secrets sent), following redirects only on the same host and
// never from https down to http. Returns the base address that answered
// ("" if none): a server that sends http to https gives the https one.
std::string probe(const std::string& base) {
    bool tls = base.rfind("https://", 0) == 0;
    std::vector<std::string> tmps;
    std::string cmd;
    if (!curl_command("", nullptr, cmd, tmps)) return "";
    // Shorter waits than a normal request: several addresses may be tried.
    cmd += std::string(" -L --max-redirs 3 --proto-redir ") + (tls ? "=https" : "=http,https") +
           " --connect-timeout 3 -w '\\n%{url_effective}\\n%{http_code}' -- " + shell_quote(base + kPublicInfo);
    Reply r = run_curl(cmd);
    remove_all(tmps);
    if (r.curl_exit != 0 || r.status != 200) return "";
    Json j;
    if (!Parser(r.body).parse(j) || j["Id"].text().empty()) return "";
    const std::string& got = r.final_url;
    size_t n = sizeof kPublicInfo - 1;
    if (got.size() <= n || got.compare(got.size() - n, n, kPublicInfo) != 0) return base;
    std::string moved = got.substr(0, got.size() - n);
    bool moved_tls = moved.rfind("https://", 0) == 0;
    if (host_only(moved) != host_only(base) || (tls && !moved_tls)) return base;
    return moved;
}

std::string status_error(const Reply& r) {
    if (r.status == 0) return "Can't reach the server";
    if (r.status == 401) return "Signed out by the server; sign in again";
    if (r.status == 403) return "This user may not do that";
    if (r.status == 404) return "The server doesn't have that";
    return "Server error " + std::to_string(r.status);
}

std::string artist_of(const Json& it) {
    if (!it["AlbumArtist"].text().empty()) return it["AlbumArtist"].text();
    const Json& a = it["Artists"];
    if (a.type == Json::Array && !a.items.empty()) return a.items[0].text();
    return "";
}

int ticks_to_s(const Json& v) {
    // RunTimeTicks and positions: 10 000 000 to the second.
    return v.type == Json::Number ? int(v.num / 1e7 + 0.5) : -1;
}

int int_of(const Json& v) { return v.type == Json::Number ? int(v.num) : -1; }

}  // namespace

std::string normalize_server(const std::string& typed) {
    std::string s;
    for (char ch : typed)
        if (!std::isspace((unsigned char)ch)) s += ch;
    while (!s.empty() && s.back() == '/') s.pop_back();
    if (s.empty()) return s;
    if (s.find("://") == std::string::npos) {
        // A bare host: plain HTTP on Jellyfin's own port unless one is given.
        auto slash = s.find('/');
        std::string host = s.substr(0, slash);
        bool has_port = host.rfind(':') != std::string::npos && host.back() != ']';
        if (!has_port) s.insert(host.size(), ":8096");
        s = "http://" + s;
    }
    return s;
}

std::vector<std::string> server_candidates(const std::string& typed) {
    std::string n = normalize_server(typed);
    if (n.empty()) return {};
    std::string s;
    for (char ch : typed)
        if (!std::isspace((unsigned char)ch)) s += ch;
    while (!s.empty() && s.back() == '/') s.pop_back();
    if (s.find("://") != std::string::npos) return {n};   // the scheme was chosen: keep it
    // A bare host: https first (443, then Jellyfin's own https port 8920,
    // or the port given), plain http on 8096 only when none answers.
    auto slash = s.find('/');
    std::string host = s.substr(0, slash), path = slash == std::string::npos ? "" : s.substr(slash);
    bool has_port = host.rfind(':') != std::string::npos && host.back() != ']';
    std::vector<std::string> v;
    if (has_port) {
        v.push_back("https://" + host + path);
    } else {
        v.push_back("https://" + host + path);
        v.push_back("https://" + host + ":8920" + path);
    }
    v.push_back(n);
    return v;
}

bool is_encrypted(const std::string& server) { return server.rfind("https://", 0) == 0; }

bool safe_token(const std::string& t) {
    if (t.empty() || t.size() > 256) return false;
    for (unsigned char ch : t)
        if (!std::isalnum(ch) && ch != '-' && ch != '_' && ch != '.') return false;
    return true;
}

std::string auth_header(const JfSession& s) {
    std::string h = "MediaBrowser Client=\"ZeroOS\", Device=\"Cardputer Zero\", DeviceId=\"" + s.device_id +
                    "\", Version=\"0.1.0\"";
    // Only a token of plain characters: anything else could end the header
    // or the line it is written on (mpv's config). The server then asks to
    // sign in again.
    if (safe_token(s.token)) h += ", Token=\"" + s.token + "\"";
    return h;
}

std::string stream_url(const JfSession& s, const std::string& item_id) {
    if (!safe_id(item_id)) return "";
    return s.server + "/Audio/" + item_id + "/universal?userId=" + url_encode(s.user_id) +
           "&deviceId=" + url_encode(s.device_id) +
           "&container=mp3,flac,ogg,opus,m4a,wav&transcodingContainer=mp3&transcodingProtocol=http"
           "&audioCodec=mp3&maxStreamingBitrate=320000";
}

std::string video_url(const JfSession& s, const std::string& item_id, const std::string& play_session,
                      int max_w, int max_h, int kbps) {
    if (!safe_id(item_id)) return "";
    // H.264 and AAC in MPEG-TS segments: what the Cardputer decodes cheaply
    // (and the Dock's mpv in hardware). The server copies streams that
    // already fit and transcodes the rest.
    return s.server + "/Videos/" + item_id + "/master.m3u8?MediaSourceId=" + item_id +
           "&DeviceId=" + url_encode(s.device_id) + "&PlaySessionId=" + url_encode(play_session) +
           "&VideoCodec=h264&AudioCodec=aac&SegmentContainer=ts&BreakOnNonKeyFrames=true"
           "&MaxWidth=" + std::to_string(max_w) + "&MaxHeight=" + std::to_string(max_h) +
           "&VideoBitrate=" + std::to_string(kbps * 1000) + "&AudioBitrate=128000"
           "&MaxAudioChannels=2&TranscodingMaxAudioChannels=2&RequireAvc=false";
}

// "...@200" -> ("...", "&startIndex=200"); no '@' or not a number: no start.
static std::string split_start(const std::string& arg, std::string& rest) {
    auto at = arg.rfind('@');
    rest = arg.substr(0, at);
    if (at == std::string::npos || at + 1 >= arg.size() || arg.size() - at > 7) return "";
    for (size_t i = at + 1; i < arg.size(); ++i)
        if (!std::isdigit((unsigned char)arg[i])) return "";
    return "&startIndex=" + arg.substr(at + 1);
}

std::string list_path(const JfSession& s, JfView view, const std::string& full_arg) {
    std::string arg;
    const std::string start = split_start(full_arg, arg);
    const std::string user = "userId=" + url_encode(s.user_id);
    const std::string limit = "&limit=" + std::to_string(kPage) + start;
    const std::string albums = "/Items?" + user + "&includeItemTypes=MusicAlbum&recursive=true";
    switch (view) {
        case JfView::Latest: return albums + "&sortBy=DateCreated&sortOrder=Descending&limit=40";
        case JfView::Albums: return albums + "&sortBy=SortName" + limit;
        case JfView::Artists:
            return "/Items?" + user + "&includeItemTypes=MusicArtist&recursive=true&sortBy=SortName" + limit;
        case JfView::Playlists:
            return "/Items?" + user + "&includeItemTypes=Playlist&recursive=true&sortBy=SortName" + limit;
        case JfView::Search:
            return "/Items?" + user +
                   "&includeItemTypes=Audio,MusicAlbum,MusicArtist,Playlist,Movie,Series,Episode,MusicVideo,Video,"
                   "AudioBook&recursive=true&limit=60&searchTerm=" + url_encode(full_arg);
        case JfView::AlbumTracks:
            if (!safe_id(arg)) return "";
            return "/Items?" + user + "&parentId=" + arg +
                   "&includeItemTypes=Audio&recursive=true&sortBy=ParentIndexNumber,IndexNumber,SortName" + limit;
        case JfView::ArtistAlbums:
            if (!safe_id(arg)) return "";
            return albums + "&albumArtistIds=" + arg + "&sortBy=ProductionYear,SortName" + limit;
        case JfView::PlaylistTracks:
            if (!safe_id(arg)) return "";
            return "/Playlists/" + arg + "/Items?" + user + limit;
        case JfView::Resume:
            return "/UserItems/Resume?" + user + "&mediaTypes=Video&enableUserData=true&limit=40";
        case JfView::Movies:
            return "/Items?" + user + "&includeItemTypes=Movie&recursive=true&sortBy=SortName" + limit;
        case JfView::Shows:
            return "/Items?" + user + "&includeItemTypes=Series&recursive=true&sortBy=SortName" + limit;
        case JfView::Seasons:
            if (!safe_id(arg)) return "";
            return "/Shows/" + arg + "/Seasons?" + user;
        case JfView::SeasonEpisodes: {
            auto colon = arg.find(':');
            if (colon == std::string::npos) return "";
            std::string series = arg.substr(0, colon), season = arg.substr(colon + 1);
            if (!safe_id(series) || !safe_id(season)) return "";
            return "/Shows/" + series + "/Episodes?" + user + "&seasonId=" + season + limit;
        }
        case JfView::Libraries: return "/UserViews?" + user;
        case JfView::Library: {
            auto colon = arg.find(':');
            std::string id = arg.substr(0, colon);
            std::string type = colon == std::string::npos ? "" : arg.substr(colon + 1);
            if (!safe_id(id)) return "";
            const std::string in = "&parentId=" + id;
            if (type == "music") return albums + in + "&sortBy=SortName" + limit;
            if (type == "movies")
                return "/Items?" + user + in + "&includeItemTypes=Movie&recursive=true&sortBy=SortName" + limit;
            if (type == "tvshows")
                return "/Items?" + user + in + "&includeItemTypes=Series&recursive=true&sortBy=SortName" + limit;
            // Home videos, music videos, books, mixed, collections, a plain
            // folder: what is in it, folders first.
            return "/Items?" + user + in + "&sortBy=IsFolder,SortName&sortOrder=Descending,Ascending" +
                   "&enableUserData=true" + limit;
        }
    }
    return "";
}

bool parse_login(const std::string& json, JfSession& s) {
    Json j;
    if (!Parser(json).parse(j)) return false;
    const std::string& token = j["AccessToken"].text();
    const std::string& user_id = j["User"]["Id"].text();
    // Both go into headers and addresses: plain characters only.
    if (!safe_token(token) || !safe_token(user_id)) return false;
    s.token = token;
    s.user_id = user_id;
    if (!j["User"]["Name"].text().empty()) s.user = j["User"]["Name"].text();
    return true;
}

bool parse_items(const std::string& json, std::vector<JfItem>& out) {
    Json j;
    if (!Parser(json).parse(j)) return false;
    const Json& arr = j.type == Json::Array ? j : j["Items"];
    if (arr.type != Json::Array) return false;
    for (const Json& it : arr.items) {
        JfItem x;
        const std::string& type = it["Type"].text();
        if (type == "MusicAlbum") x.kind = JfItem::Album;
        else if (type == "MusicArtist") x.kind = JfItem::Artist;
        else if (type == "Playlist") x.kind = JfItem::Playlist;
        else if (type == "Audio" || type == "AudioBook") x.kind = JfItem::Track;
        else if (type == "Movie") x.kind = JfItem::Movie;
        else if (type == "Series") x.kind = JfItem::Series;
        else if (type == "Season") x.kind = JfItem::Season;
        else if (type == "Episode") x.kind = JfItem::Episode;
        else if (type == "Video" || type == "MusicVideo") x.kind = JfItem::Video;
        else if (type == "CollectionFolder" || type == "UserView" || type == "Folder" || type == "BoxSet" ||
                 type == "PhotoAlbum" || type == "PlaylistsFolder" || type == "ManualPlaylistsFolder")
            x.kind = JfItem::Folder;
        else continue;
        x.id = it["Id"].text();
        x.name = it["Name"].text();
        if (!safe_id(x.id) || x.name.empty()) continue;
        if (x.kind == JfItem::Album || x.kind == JfItem::Track) x.detail = artist_of(it);
        if (x.kind == JfItem::Folder) {
            // The library type: "music", "movies", "tvshows", "homevideos"...
            // Live TV can't be played here, so it isn't listed.
            for (char ch : it["CollectionType"].text()) x.detail += char(std::tolower((unsigned char)ch));
            if (x.detail == "livetv") continue;
            if (type == "BoxSet") x.detail = "boxsets";
            if (type == "PlaylistsFolder" || type == "ManualPlaylistsFolder") x.detail = "playlists";
            if (x.detail.find_first_not_of("abcdefghijklmnopqrstuvwxyz") != std::string::npos) x.detail.clear();
        }
        if (x.kind == JfItem::Season || x.kind == JfItem::Episode) {
            x.detail = it["SeriesName"].text();
            x.series_id = it["SeriesId"].text();
            if (!safe_id(x.series_id)) x.series_id.clear();
        }
        x.duration_s = ticks_to_s(it["RunTimeTicks"]);
        x.number = int_of(it["IndexNumber"]);
        x.season = int_of(it["ParentIndexNumber"]);
        x.year = int_of(it["ProductionYear"]);
        int resume = ticks_to_s(it["UserData"]["PlaybackPositionTicks"]);
        if (resume > 0) x.resume_s = resume;
        out.push_back(std::move(x));
    }
    return true;
}

bool login(const std::string& server, const std::string& user, const std::string& password,
           const std::string& device_id, JfSession& out, std::string& error) {
    JfSession s;
    s.server = normalize_server(server);
    s.user = user;
    s.device_id = device_id;
    if (s.server.empty() || user.empty()) {
        error = "Server and user name are needed";
        return false;
    }
    // Find the server before the password goes anywhere: https first for a
    // bare name, and an http address that the server itself moves to https
    // (same host) is signed in to over https.
    for (const std::string& base : server_candidates(server)) {
        std::string found = probe(base);
        if (!found.empty()) {
            s.server = found;
            break;
        }
    }
    std::string body = "{\"Username\":\"" + json_escape(user) + "\",\"Pw\":\"" + json_escape(password) + "\"}";
    Reply r = http(s.server + "/Users/AuthenticateByName", auth_header(s), &body);
    if (r.status == 401 || r.status == 403) {
        error = "Wrong user name or password";
        return false;
    }
    if (r.status >= 300 && r.status < 400) {
        // Not followed: the password would go along. Type where it points.
        error = "The server moved; type its full address (https://...)";
        return false;
    }
    if (r.status != 200) {
        if (r.curl_exit == 60)
            error = "The server's certificate isn't trusted";
        else
            error = r.status == 0 ? "Can't reach " + s.server : status_error(r);
        return false;
    }
    if (!parse_login(r.body, s)) {
        error = "That address isn't a Jellyfin server";
        return false;
    }
    out = s;
    return true;
}

bool list(const JfSession& s, JfView view, const std::string& arg, std::vector<JfItem>& out,
          std::string& error) {
    std::string path = list_path(s, view, arg);
    if (path.empty()) {
        error = "Nothing to open";
        return false;
    }
    Reply r = http(s.server + path, auth_header(s), nullptr);
    std::vector<JfItem> got;
    bool ok = r.status == 200 && parse_items(r.body, got);
    if (view == JfView::Resume && !ok && safe_id(s.user_id)) {
        // Servers before 10.9 only have the per-user path.
        got.clear();
        r = http(s.server + "/Users/" + s.user_id + "/Items/Resume?mediaTypes=Video&limit=40", auth_header(s), nullptr);
        ok = r.status == 200 && parse_items(r.body, got);
    }
    if (view == JfView::Libraries && !ok && safe_id(s.user_id)) {
        // Servers before 10.9 only have the per-user path.
        got.clear();
        r = http(s.server + "/Users/" + s.user_id + "/Views", auth_header(s), nullptr);
        ok = r.status == 200 && parse_items(r.body, got);
    }
    if (view == JfView::Artists && (!ok || got.empty())) {
        // Servers differ on whether artists come from /Items; this is the
        // older, dedicated list.
        got.clear();
        std::string rest;
        r = http(s.server + "/Artists/AlbumArtists?userId=" + url_encode(s.user_id) + "&limit=" +
                     std::to_string(kPage) + split_start(arg, rest),
                 auth_header(s), nullptr);
        ok = r.status == 200 && parse_items(r.body, got);
    }
    if (!ok) {
        error = r.status == 200 ? "Unexpected answer from the server" : status_error(r);
        return false;
    }
    out = std::move(got);
    return true;
}

std::string report_body(const std::string& item_id, const std::string& play_session, int pos_s, bool paused) {
    return "{\"ItemId\":\"" + json_escape(item_id) + "\",\"MediaSourceId\":\"" + json_escape(item_id) +
           "\",\"PlaySessionId\":\"" + json_escape(play_session) +
           "\",\"PositionTicks\":" + std::to_string((long long)(pos_s < 0 ? 0 : pos_s) * 10000000LL) +
           ",\"IsPaused\":" + (paused ? "true" : "false") +
           ",\"CanSeek\":true,\"PlayMethod\":\"Transcode\"}";
}

void report(const JfSession& s, JfReport what, const std::string& item_id, const std::string& play_session,
            int pos_s, bool paused) {
#ifdef __EMSCRIPTEN__
    return;   // the browser simulator has no processes (and no server)
#else
    if (!s.signed_in() || !safe_id(item_id)) return;
    const char* path = what == JfReport::Start ? "/Sessions/Playing"
                       : what == JfReport::Progress ? "/Sessions/Playing/Progress"
                                                    : "/Sessions/Playing/Stopped";
    std::string body = report_body(item_id, play_session, pos_s, paused), cmd;
    std::vector<std::string> tmps;
    if (!curl_command(auth_header(s), &body, cmd, tmps)) return;
    // The shell backgrounds curl and exits at once, so nothing waits on the
    // network and no child is left to reap. The header and body files go after.
    std::string rm = "rm -f";
    for (auto& t : tmps) rm += " " + t;
    std::string script = "(" + cmd + " -o /dev/null -- " + shell_quote(s.server + path) + "; " + rm +
                         ") </dev/null >/dev/null 2>&1 &";
    const char* argv[] = {"sh", "-c", script.c_str(), nullptr};
    pid_t pid;
    if (posix_spawnp(&pid, "sh", nullptr, nullptr, const_cast<char* const*>(argv), environ) != 0) {
        remove_all(tmps);
        return;
    }
    int st;
    waitpid(pid, &st, 0);
#endif
}

bool write_dock_options(const JfSession& s, int start_s) {
    const char* home = std::getenv("HOME");
    if (!home || !*home) return false;
    std::string dir = std::string(home) + "/.config/zeroos";
    mkdir((std::string(home) + "/.config").c_str(), 0755);
    mkdir(dir.c_str(), 0700);
    std::string path = dir + "/jellyfin-mpv.conf";
    std::string header = "Authorization: " + auth_header(s);
    // One line per option: a line break in a value would start a new one.
    if (header.find_first_of("\r\n") != std::string::npos) return false;
    // %N% quotes a value of N bytes as is: the header is full of commas and
    // quotes that mpv would otherwise read as list separators.
    std::string text = "http-header-fields-append=%" + std::to_string(header.size()) + "%" + header + "\n";
    if (start_s > 0) text += "start=" + std::to_string(start_s) + "\n";
    std::string tmp = path + ".tmp";
    int fd = open(tmp.c_str(), O_WRONLY | O_CREAT | O_TRUNC, 0600);
    if (fd < 0) return false;
    bool ok = write(fd, text.data(), text.size()) == ssize_t(text.size());
    close(fd);
    return ok && rename(tmp.c_str(), path.c_str()) == 0;
}

}  // namespace zs::jf
