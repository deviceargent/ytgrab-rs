//! Unit tests for the C++ port. Plain assertions, no framework needed.

#include <cstdio>
#include <string>
#include <vector>

#include "browser_cookies.hpp"
#include "cookies.hpp"
#include "error.hpp"
#include "formats.hpp"
#include "output.hpp"
#include "subtitles.hpp"
#include "url.hpp"
#include "util.hpp"

namespace {

int failures = 0;

void check(bool condition, const std::string& what) {
    if (!condition) {
        std::fprintf(stderr, "FAIL: %s\n", what.c_str());
        ++failures;
    }
}

void test_url() {
    using ytgrab::url::Target;

    Target t = ytgrab::url::parse("https://www.youtube.com/watch?v=dQw4w9WgXcQ");
    check(t.kind == Target::Kind::Video && t.id == "dQw4w9WgXcQ", "parses watch urls");

    t = ytgrab::url::parse("https://youtu.be/dQw4w9WgXcQ?t=42");
    check(t.kind == Target::Kind::Video && t.id == "dQw4w9WgXcQ", "parses short urls");

    t = ytgrab::url::parse("https://youtube.com/shorts/abc12345678");
    check(t.kind == Target::Kind::Video && t.id == "abc12345678", "parses shorts");

    t = ytgrab::url::parse("https://youtube.com/playlist?list=PL1234567890");
    check(t.kind == Target::Kind::Playlist && t.id == "PL1234567890", "parses playlists");

    bool threw = false;
    try {
        ytgrab::url::parse("https://vimeo.com/12345");
    } catch (const ytgrab::Error&) {
        threw = true;
    }
    check(threw, "rejects non-youtube urls");

    check(ytgrab::url::looks_like_video_id("dQw4w9WgXcQ"), "accepts 11 char ids");
    check(!ytgrab::url::looks_like_video_id("dQw4w9WgXcQx"), "rejects longer ids");
}

void test_output() {
    ytgrab::output::TemplateVars vars;
    vars.title = "Never Gonna Give You Up";
    vars.id = "dQw4w9WgXcQ";
    vars.ext = "mp4";
    vars.uploader = "Rick Astley";
    vars.duration_secs = 213;
    vars.resolution = "1920x1080";
    vars.format_id = "137+140";

    check(ytgrab::output::render("%(title)s.%(ext)s", vars) ==
              "Never Gonna Give You Up.mp4",
          "renders a template");

    vars.playlist_index = 3;
    check(ytgrab::output::render("%(playlist_index)s - %(title)s", vars) ==
              "3 - Never Gonna Give You Up",
          "renders the playlist index");

    check(ytgrab::output::sanitize("a/b:c") == "a_b_c", "strips path separators");
    check(ytgrab::output::sanitize_path("/tmp/out.mp4") == "/tmp/out.mp4",
          "keeps absolute paths");
    check(ytgrab::output::sanitize_path("../etc/passwd") == "video/etc/passwd",
          "neutralises ..");
    check(ytgrab::output::sanitize_path("C:\\Users\\me\\Downloads\\%(title)s.%(ext)s") ==
              "C:/Users/me/Downloads/%(title)s.%(ext)s",
          "keeps windows absolute paths");
    check(ytgrab::output::sanitize_path("sub\\out.mp4") == "sub/out.mp4",
          "normalises windows separators");
    check(ytgrab::output::format_duration(3661) == "1:01:01", "formats hours");
}

ytgrab::models::Format make_format(long long itag, const std::string& mime, long long height,
                                   const std::string& codecs, long long tbr) {
    ytgrab::models::Format f;
    f.itag = itag;
    f.url = "http://example";
    f.mime_type = mime;
    f.height = height;
    f.codecs = codecs;
    f.bitrate = tbr * 1000;
    return f;
}

void test_formats() {
    using ytgrab::formats::Kind;
    using ytgrab::formats::Selector;

    check(Selector::parse("best").parts[0].kind == Kind::Combined, "parses best");
    check(Selector::parse("bv").parts[0].kind == Kind::Video, "parses bv");
    check(Selector::parse("ba").parts[0].kind == Kind::Audio, "parses ba");
    check(Selector::parse("1080").parts[0].itag == 1080, "parses a bare height");

    bool threw = false;
    try {
        Selector::parse("nonsense");
    } catch (const ytgrab::Error&) {
        threw = true;
    }
    check(threw, "rejects unknown selectors");

    std::vector<ytgrab::models::Format> sample = {
        make_format(18, "video/mp4", 360, "avc1.42001E, mp4a.40.2", 700),
        make_format(137, "video/mp4", 1080, "avc1.640028", 4200),
        make_format(248, "video/webm", 1080, "vp9", 2800),
        make_format(313, "video/webm", 2160, "vp09.00.40.08", 12000),
        make_format(140, "audio/mp4", 0, "mp4a.40.2", 128),
        make_format(251, "audio/webm", 0, "opus", 160),
    };

    auto picked = ytgrab::formats::select(Selector::parse("bestvideo"), sample);
    check(picked.videos[0].itag == 313, "highest resolution wins");

    picked = ytgrab::formats::select(Selector::parse("bestvideo[height=1080]"), sample);
    check(picked.videos[0].itag == 137, "avc1 beats vp9 at 1080p");

    picked = ytgrab::formats::select(Selector::parse("137"), sample);
    check(picked.videos[0].itag == 137, "bare number is a format id when it exists");

    picked = ytgrab::formats::select(Selector::parse("720"), sample);
    check(picked.videos[0].height_val() <= 720, "bare number falls back to a height cap");

    picked = ytgrab::formats::select(Selector::parse("bestvideo[height<=720]"), sample);
    check(picked.videos[0].height_val() == 360, "height filter caps resolution");

    picked = ytgrab::formats::select(Selector::parse("best"), sample);
    check(picked.audios.size() == 1 && picked.videos[0].itag == 313,
          "best pairs the highest video with audio");

    picked = ytgrab::formats::select(Selector::parse("bestvideo"), sample);
    check(picked.audios.empty(), "explicit video selector adds no audio");

    std::vector<ytgrab::models::Format> only_combined = {
        make_format(18, "video/mp4", 360, "avc1.42001E, mp4a.40.2", 700)};
    picked = ytgrab::formats::select(Selector::parse("best"), only_combined);
    check(picked.videos[0].itag == 18 && !picked.needs_mux(),
          "combined only when no separate audio exists");

    picked = ytgrab::formats::select(Selector::parse("best[ext=mp4][height<=360]"), sample);
    check(picked.videos[0].height_val() == 360 && picked.videos[0].ext() == "mp4",
          "height and ext filters apply");
}

void test_subtitles() {
    const std::string body =
        R"({"events":[{"tStartMs":0,"dDurationMs":1500,"segs":[{"utf8":"Hello"},{"utf8":" world"}]},{"tStartMs":2000,"dDurationMs":1000,"segs":[{"utf8":"Second line"}]}]})";
    auto cues = ytgrab::subtitles::parse_json3(body);
    check(cues.size() == 2, "parses two cues");
    check(cues[0].text == "Hello world", "joins the cue segments");

    const std::string srt = ytgrab::subtitles::to_srt(cues);
    check(srt.rfind("1\n00:00:00,000 --> 00:00:01,500\n", 0) == 0, "renders srt timestamps");
    check(srt.find("Second line") != std::string::npos, "renders the second cue");

    const std::string vtt = ytgrab::subtitles::to_vtt(cues);
    check(vtt.rfind("WEBVTT", 0) == 0, "renders a vtt header");
    check(vtt.find("00:00:02.000 --> 00:00:03.000") != std::string::npos,
          "renders vtt timestamps");
}

void test_cookies() {
    ytgrab::CookieJar jar;
    ytgrab::Cookie cookie;
    cookie.domain = ".youtube.com";
    cookie.path = "/";
    cookie.name = "SID";
    cookie.value = "abc";
    cookie.secure = true;
    jar.add(cookie);

    check(jar.header_for("https://www.youtube.com/watch?v=x") == "SID=abc",
          "sends cookies to subdomains");
    check(jar.header_for("https://example.com/").empty(), "ignores other hosts");
    check(jar.header_for("http://www.youtube.com/").empty(), "honours the secure flag");

    std::vector<ytgrab::BrowserCookie> cookies = {
        {".youtube.com", "/", "SID", "abc", true, 1700000000}};
    const std::string netscape = ytgrab::to_netscape(cookies);
    check(netscape.find(".youtube.com\tTRUE\t/\tTRUE\t1700000000\tSID\tabc") !=
              std::string::npos,
          "writes the netscape layout");
}

}  // namespace

int main() {
    test_url();
    test_output();
    test_formats();
    test_subtitles();
    test_cookies();

    if (failures == 0) {
        std::printf("all tests passed\n");
        return 0;
    }
    std::fprintf(stderr, "%d test(s) failed\n", failures);
    return 1;
}
