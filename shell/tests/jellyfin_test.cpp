// Jellyfin client: addresses, list paths and reading the server's replies.
// No network: only the pure functions in hal/jellyfin.cpp.
#include <cstdio>
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

static JfSession session() {
    JfSession s;
    s.server = "http://nas:8096";
    s.user = "john";
    s.user_id = "0123456789abcdef0123456789abcdef";
    s.token = "tok123";
    s.device_id = "zeroos-1";
    return s;
}

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

    if (failures) {
        std::printf("%d failure(s)\n", failures);
        return 1;
    }
    std::printf("jellyfin_test: all passed\n");
    return 0;
}
