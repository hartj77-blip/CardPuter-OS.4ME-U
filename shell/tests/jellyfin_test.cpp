// Jellyfin client: addresses, list paths and reading the server's replies.
// No network: only the pure functions in hal/jellyfin.cpp.
#include <sys/stat.h>
#include <unistd.h>

#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

#include "hal/jellyfin.h"

using namespace zs;

static int failures = 0;
#define CHECK(cond)                                                    \
    do {                                                               \
        if (!(cond)) {                                                 \
            std::printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); \
            ++failures;                                                \
        }                                                              \
    } while (0)

static bool has(const std::string& s, const std::string& part) { return s.find(part) != std::string::npos; }

static std::string slurp(const std::string& path) {
    std::ifstream f(path);
    std::stringstream b;
    b << f.rdbuf();
    return b.str();
}

static JfSession session() {
    JfSession s;
    s.server = "http://nas:8096";
    s.user = "john";
    s.user_id = "0123456789abcdef0123456789abcdef";
    s.token = "tok123";
    s.device_id = "zeroos-1";
    return s;
}

// Apps built against older headers spell these by number (Media does), so
// they must never move: new values only ever go at the end.
static_assert(int(JfView::Libraries) == 13 && int(JfView::Library) == 14, "JfView values are fixed");
static_assert(int(JfItem::Folder) == 9, "JfItem::Kind values are fixed");
static_assert(kJfPage == 200, "Media assumes 200 rows a page");

int main() {
    CHECK(jf::normalize_server("nas") == "http://nas:8096");
    CHECK(jf::normalize_server("https://media.example.com/") == "https://media.example.com");

    JfSession s = session();

    // The video address: HLS, H.264/AAC, sized as asked, no token in it.
    std::string v = jf::video_url(s, "aabbccdd", "sess1", 427, 240, 700);
    CHECK(v.rfind("http://nas:8096/Videos/aabbccdd/master.m3u8?", 0) == 0);
    CHECK(has(v, "MediaSourceId=aabbccdd"));
    CHECK(has(v, "VideoCodec=h264"));
    CHECK(has(v, "AudioCodec=aac"));
    CHECK(has(v, "MaxWidth=427"));
    CHECK(has(v, "MaxHeight=240"));
    CHECK(has(v, "VideoBitrate=700000"));
    CHECK(has(v, "PlaySessionId=sess1"));
    CHECK(has(v, "DeviceId=zeroos-1"));
    CHECK(!has(v, "tok123"));
    CHECK(!has(v, "api_key"));
    CHECK(jf::video_url(s, "../etc", "x", 1, 1, 1).empty());   // only hex ids

    // Video lists.
    CHECK(has(jf::list_path(s, JfView::Movies, ""), "includeItemTypes=Movie"));
    CHECK(has(jf::list_path(s, JfView::Shows, ""), "includeItemTypes=Series"));
    CHECK(jf::list_path(s, JfView::Seasons, "ab12").rfind("/Shows/ab12/Seasons?", 0) == 0);
    CHECK(jf::list_path(s, JfView::SeasonEpisodes, "ab12:cd34").rfind("/Shows/ab12/Episodes?", 0) == 0);
    CHECK(has(jf::list_path(s, JfView::SeasonEpisodes, "ab12:cd34"), "seasonId=cd34"));
    CHECK(jf::list_path(s, JfView::SeasonEpisodes, "ab12").empty());
    CHECK(jf::list_path(s, JfView::SeasonEpisodes, "ab12:x/y").empty());
    CHECK(has(jf::list_path(s, JfView::Resume, ""), "mediaTypes=Video"));
    CHECK(has(jf::list_path(s, JfView::Search, "dune"), "Movie,Series,Episode"));

    // A list reply with every kind we show, and one we skip.
    const char* reply = R"({"Items":[
        {"Name":"Dune","Id":"a1","Type":"Movie","ProductionYear":2021,"RunTimeTicks":93560000000,
         "UserData":{"PlaybackPositionTicks":12345000000,"Played":false}},
        {"Name":"Pilot","Id":"b2","Type":"Episode","SeriesName":"Lost","SeriesId":"c3",
         "IndexNumber":1,"ParentIndexNumber":1,"RunTimeTicks":25200000000,"UserData":{"PlaybackPositionTicks":0}},
        {"Name":"Season 2","Id":"d4","Type":"Season","SeriesName":"Lost","SeriesId":"c3","IndexNumber":2},
        {"Name":"Lost","Id":"c3","Type":"Series"},
        {"Name":"Home movie","Id":"e5","Type":"Video"},
        {"Name":"A book","Id":"f6","Type":"Book"}
    ],"TotalRecordCount":6})";
    std::vector<JfItem> items;
    CHECK(jf::parse_items(reply, items));
    CHECK(items.size() == 5);
    if (items.size() == 5) {
        CHECK(items[0].kind == JfItem::Movie && items[0].year == 2021);
        CHECK(items[0].duration_s == 9356);
        CHECK(items[0].resume_s == 1235);   // 1234.5 s, rounded
        CHECK(items[0].playable_video());
        CHECK(items[1].kind == JfItem::Episode && items[1].detail == "Lost" && items[1].series_id == "c3");
        CHECK(items[1].episode_code() == "S1E1");
        CHECK(items[1].resume_s == 0);
        CHECK(items[2].kind == JfItem::Season && items[2].number == 2 && items[2].series_id == "c3");
        CHECK(!items[2].playable_video());
        CHECK(items[3].kind == JfItem::Series);
        CHECK(items[4].kind == JfItem::Video && items[4].playable_video());
    }

    // Every library the user has, and what is in one.
    CHECK(jf::list_path(s, JfView::Libraries, "").rfind("/UserViews?userId=", 0) == 0);
    std::string lib = jf::list_path(s, JfView::Library, "aa11:music");
    CHECK(has(lib, "parentId=aa11") && has(lib, "includeItemTypes=MusicAlbum") && has(lib, "recursive=true"));
    CHECK(has(jf::list_path(s, JfView::Library, "aa11:movies"), "includeItemTypes=Movie"));
    CHECK(has(jf::list_path(s, JfView::Library, "aa11:tvshows"), "includeItemTypes=Series"));
    std::string folder = jf::list_path(s, JfView::Library, "aa11:homevideos");
    CHECK(has(folder, "parentId=aa11") && !has(folder, "recursive") && !has(folder, "includeItemTypes"));
    CHECK(has(jf::list_path(s, JfView::Library, "aa11:"), "parentId=aa11"));
    CHECK(has(jf::list_path(s, JfView::Library, "aa11"), "parentId=aa11"));
    CHECK(jf::list_path(s, JfView::Library, "a/b:music").empty());
    // Pages after the first.
    CHECK(has(jf::list_path(s, JfView::Albums, "@200"), "&startIndex=200"));
    CHECK(!has(jf::list_path(s, JfView::Albums, ""), "startIndex"));
    CHECK(has(jf::list_path(s, JfView::Library, "aa11:music@400"), "&startIndex=400"));
    CHECK(has(jf::list_path(s, JfView::Library, "aa11:music@400"), "parentId=aa11&"));
    CHECK(!has(jf::list_path(s, JfView::Albums, "@2x"), "startIndex"));
    // Search text keeps its '@'.
    CHECK(has(jf::list_path(s, JfView::Search, "a@200"), "searchTerm=a%40200"));
    CHECK(!has(jf::list_path(s, JfView::Search, "a@200"), "startIndex"));

    const std::string views = R"({"Items":[
        {"Name":"Music","Id":"f1","Type":"CollectionFolder","CollectionType":"music"},
        {"Name":"Vinyl","Id":"f2","Type":"CollectionFolder","CollectionType":"music"},
        {"Name":"Home Videos","Id":"f3","Type":"CollectionFolder","CollectionType":"homevideos"},
        {"Name":"Live TV","Id":"f4","Type":"UserView","CollectionType":"livetv"},
        {"Name":"Collections","Id":"f5","Type":"BoxSet"},
        {"Name":"2024","Id":"f6","Type":"Folder"},
        {"Name":"Dune","Id":"f7","Type":"AudioBook","AlbumArtist":"Frank Herbert","RunTimeTicks":360000000000}
    ]})";
    items.clear();
    CHECK(jf::parse_items(views, items));
    CHECK(items.size() == 6);   // not Live TV
    if (items.size() == 6) {
        CHECK(items[0].kind == JfItem::Folder && items[0].detail == "music");
        CHECK(items[1].kind == JfItem::Folder && items[1].name == "Vinyl");
        CHECK(items[2].detail == "homevideos");
        CHECK(items[3].kind == JfItem::Folder && items[3].detail == "boxsets");
        CHECK(items[4].kind == JfItem::Folder && items[4].detail.empty());
        CHECK(items[5].kind == JfItem::Track && items[5].detail == "Frank Herbert");
    }

    // Playback reports.
    std::string body = jf::report_body("a1", "sess1", 90, true);
    CHECK(has(body, "\"ItemId\":\"a1\""));
    CHECK(has(body, "\"PositionTicks\":900000000"));
    CHECK(has(body, "\"IsPaused\":true"));
    CHECK(has(body, "\"PlaySessionId\":\"sess1\""));

    // Addresses tried for what was typed: https first for a bare host.
    {
        auto c = jf::server_candidates("nas");
        CHECK(c.size() == 3);
        if (c.size() == 3) {
            CHECK(c[0] == "https://nas");
            CHECK(c[1] == "https://nas:8920");
            CHECK(c[2] == "http://nas:8096");
        }
        c = jf::server_candidates("nas:8096/jf/");
        CHECK(c.size() == 2 && c[0] == "https://nas:8096/jf" && c[1] == "http://nas:8096/jf");
        c = jf::server_candidates("http://nas:8096");   // typed http: kept as it is
        CHECK(c.size() == 1 && c[0] == "http://nas:8096");
        c = jf::server_candidates("https://media.example.com/");
        CHECK(c.size() == 1 && c[0] == "https://media.example.com");
        CHECK(jf::server_candidates("  ").empty());
        CHECK(jf::is_encrypted("https://nas") && !jf::is_encrypted("http://nas:8096"));
    }

    // Tokens: plain characters only, so none can end a header line.
    {
        CHECK(jf::safe_token("0123456789abcdef0123456789abcdef"));
        CHECK(jf::safe_token("a-b_c.d"));
        CHECK(!jf::safe_token(""));
        CHECK(!jf::safe_token("tok\nstart=0"));
        CHECK(!jf::safe_token("tok\""));
        CHECK(!jf::safe_token("tok, Other=1"));
        JfSession t;
        CHECK(!jf::parse_login(R"({"AccessToken":"ab\ncd","User":{"Id":"0123abcd"}})", t));
        CHECK(!jf::parse_login(R"({"AccessToken":"abcd","User":{"Id":"../x"}})", t));
        CHECK(jf::parse_login(R"({"AccessToken":"abcd","User":{"Id":"0123abcd","Name":"j"}})", t));
        CHECK(t.token == "abcd" && t.user == "j");
        JfSession bad = session();
        bad.token = "x\"\r\nEvil: 1";
        CHECK(!has(jf::auth_header(bad), "Evil"));
    }

    // Signing in and listing through a stand-in curl: the token and the
    // password travel in files, never on the command line; no redirects are
    // followed with them; a bare host finds the server over https first.
    {
        char dir[] = "/tmp/jf-test-XXXXXX";
        std::string d = mkdtemp(dir);
        std::string bin = d + "/bin";
        mkdir(bin.c_str(), 0755);
        std::ofstream(bin + "/curl")
            << "#!/bin/sh\n"
               "printf '%s ' \"$@\" >> " << d << "/args; echo >> " << d << "/args\n"
               "for a; do case $a in @*) cat \"${a#@}\" >> " << d << "/files;; esac; url=$a; done\n"
               "case $FAKE_MOVE/$url in\n"
               "  same/http://*/System/Info/Public) printf '{\"Id\":\"abc\"}\\n%s\\n200' \"https://${url#http://}\"; exit 0;;\n"
               "  other/http://*/System/Info/Public) printf '{\"Id\":\"abc\"}\\nhttps://evil.example/System/Info/Public\\n200'; exit 0;;\n"
               "esac\n"
               "case $url in\n"
               "  https://*) if [ -z \"$FAKE_HTTPS\" ]; then printf '\\n%s\\n000' \"$url\"; exit 7; fi;;\n"
               "esac\n"
               "case $url in\n"
               "  */System/Info/Public) printf '{\"Id\":\"abc\"}\\n%s\\n200' \"$url\";;\n"
               "  */Users/AuthenticateByName) printf '{\"AccessToken\":\"tok999\",\"User\":"
               "{\"Id\":\"0123abcd\",\"Name\":\"john\"}}\\n%s\\n200' \"$url\";;\n"
               "  *) printf '{\"Items\":[]}\\n%s\\n200' \"$url\";;\n"
               "esac\n";
        chmod((bin + "/curl").c_str(), 0755);
        std::string old = std::getenv("PATH") ? std::getenv("PATH") : "";
        setenv("PATH", (bin + ":" + old).c_str(), 1);

        unsetenv("FAKE_HTTPS");
        JfSession got;
        std::string error;
        CHECK(jf::login("nas", "john", "pa55word", "zeroos-1", got, error));
        CHECK(got.server == "http://nas:8096");   // no https answered: plain http
        CHECK(got.token == "tok999");
        std::string args = slurp(d + "/args");
        CHECK(has(args, "https://nas/System/Info/Public"));   // https was tried first
        CHECK(args.find("https://nas/System") < args.find("http://nas:8096/System"));
        CHECK(!has(args, "pa55word"));
        CHECK(has(slurp(d + "/files"), "pa55word"));
        std::vector<JfItem> items2;
        CHECK(jf::list(got, JfView::Albums, "", items2, error));
        args = slurp(d + "/args");
        CHECK(!has(args, "tok999"));
        CHECK(has(slurp(d + "/files"), "Token=\"tok999\""));
        // Requests that carry the token or password follow no redirects.
        std::istringstream lines(args);
        for (std::string l; std::getline(lines, l);)
            if (!has(l, "/System/Info/Public") && (has(l, " -L") || has(l, "--post30"))) {
                std::printf("follows redirects: %s\n", l.c_str());
                CHECK(false);
            }

        setenv("FAKE_HTTPS", "1", 1);
        CHECK(jf::login("nas", "john", "pa55word", "zeroos-1", got, error));
        CHECK(got.server == "https://nas");
        // A typed http address the server itself moves to https (same host).
        setenv("FAKE_MOVE", "same", 1);
        CHECK(jf::login("http://nas:8096", "john", "pa55word", "zeroos-1", got, error));
        CHECK(got.server == "https://nas:8096");
        // ...but never to another host.
        setenv("FAKE_MOVE", "other", 1);
        CHECK(jf::login("http://nas:8096", "john", "pa55word", "zeroos-1", got, error));
        CHECK(got.server == "http://nas:8096");
        CHECK(!has(slurp(d + "/args"), "evil.example"));
        unsetenv("FAKE_MOVE");
        unsetenv("FAKE_HTTPS");
        setenv("PATH", old.c_str(), 1);
        if (std::system(("rm -rf '" + d + "'").c_str())) {}
        // No temporary files left behind with secrets in them.
        CHECK(std::system("ls /tmp/zeroos-jf-* >/dev/null 2>&1") != 0);
    }

    if (failures) {
        std::printf("%d failure(s)\n", failures);
        return 1;
    }
    std::printf("jellyfin_test: all passed\n");
    return 0;
}
