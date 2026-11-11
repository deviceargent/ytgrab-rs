#include "client.hpp"

#include <cstdlib>
#include <iostream>

#include "error.hpp"
#include "util.hpp"

namespace ytgrab::innertube {
namespace {

constexpr unsigned CLIENT_ID_ANDROID = 3;
constexpr unsigned CLIENT_ID_IOS = 5;
constexpr unsigned CLIENT_ID_MWEB = 2;
constexpr unsigned CLIENT_ID_WEB = 1;
constexpr unsigned CLIENT_ID_TV = 7;

}  // namespace

ClientContext ClientContext::android() {
    ClientContext c;
    c.name = "ANDROID";
    c.version = "20.10.38";
    c.user_agent = "com.google.android.youtube/20.10.38 (Linux; U; Android 14) gzip";
    c.api_key = "AIzaSyA8eiZmM1FaDVjRy-df2KTyQ_vz_yYM39w";
    c.numeric_id = CLIENT_ID_ANDROID;
    c.extra = {{"androidSdkVersion", 34}, {"osName", "Android"}, {"osVersion", "14"}};
    return c;
}

ClientContext ClientContext::ios() {
    ClientContext c;
    c.name = "IOS";
    c.version = "20.10.4";
    c.user_agent =
        "com.google.ios.youtube/20.10.4 (iPhone16,2; U; CPU iOS 18_3_2 like Mac OS X;)";
    c.api_key = "AIzaSyB-63vPrdThhKuerbB2N_l7Kwwcxj6yUAc";
    c.numeric_id = CLIENT_ID_IOS;
    c.extra = {{"deviceMake", "Apple"},
               {"deviceModel", "iPhone16,2"},
               {"osName", "iPhone"},
               {"osVersion", "18.3.2.22D82"}};
    return c;
}

ClientContext ClientContext::mweb() {
    ClientContext c;
    c.name = "MWEB";
    c.version = "2.20250101.00.00";
    c.user_agent =
        "Mozilla/5.0 (iPhone; CPU iPhone OS 17_5 like Mac OS X) AppleWebKit/605.1.15 "
        "(KHTML, like Gecko) Version/17.5 Mobile/15E148 Safari/604.1";
    c.api_key = "AIzaSyAO_FJ2SlqU8Q4STEHLGCilw_Y9_11qcW8";
    c.numeric_id = CLIENT_ID_MWEB;
    return c;
}

ClientContext ClientContext::web() {
    ClientContext c;
    c.name = "WEB";
    c.version = "2.20250101.00.00";
    c.user_agent =
        "Mozilla/5.0 (Windows NT 10.0; Win64; x64) AppleWebKit/537.36 (KHTML, like Gecko) "
        "Chrome/132.0.0.0 Safari/537.36";
    c.api_key = "AIzaSyAO_FJ2SlqU8Q4STEHLGCilw_Y9_11qcW8";
    c.numeric_id = CLIENT_ID_WEB;
    return c;
}

ClientContext ClientContext::tv() {
    ClientContext c;
    c.name = "TVHTML5_SIMPLY_EMBEDDED_PLAYER";
    c.version = "2.0";
    c.user_agent =
        "Mozilla/5.0 (PlayStation; PlayStation 4/12.00) AppleWebKit/605.1.15 (KHTML, like "
        "Gecko) Version/16.0 Safari/605.1.15";
    c.api_key = "AIzaSyDCU8hByM-4DrUqRUYnGn-3llEO78bcxq8";
    c.numeric_id = CLIENT_ID_TV;
    return c;
}

std::optional<ClientContext> ClientContext::by_name(const std::string& name) {
    std::string n = util::to_lower(name);
    if (n == "android") return android();
    if (n == "ios") return ios();
    if (n == "mweb") return mweb();
    if (n == "web") return web();
    if (n == "tv" || n == "tvhtml5") return tv();
    return std::nullopt;
}

json ClientContext::client_object() const {
    json obj = {{"clientName", name}, {"clientVersion", version}, {"hl", "en"}, {"gl", "US"}};
    if (extra.is_object()) {
        for (auto it = extra.begin(); it != extra.end(); ++it) obj[it.key()] = it.value();
    }
    return obj;
}

InnerTube::InnerTube(ClientContext ctx, const CookieJar* jar)
    : ctx_(std::move(ctx)), jar_(jar), http_(ctx_.user_agent) {}

json InnerTube::post(const std::string& endpoint, const json& body) {
    const std::string url = "https://www.youtube.com/youtubei/v1/" + endpoint + "?key=" +
                            ctx_.api_key + "&prettyPrint=false";
    if (std::getenv("YTDBG")) {
        std::cerr << "DEBUG url=" << url << "\n";
        std::cerr << "DEBUG body=" << body.dump() << "\n";
    }
    if (jar_) http_.set_cookies(jar_->header_for(url));

    http::Headers headers = {
        {"Content-Type", "application/json"},
        {"Origin", "https://www.youtube.com"},
        {"X-Youtube-Client-Name", std::to_string(ctx_.numeric_id)},
        {"X-Youtube-Client-Version", ctx_.version},
        {"Accept-Language", "en-US,en;q=0.9"},
    };

    http::Response response = http_.post(url, body.dump(), headers);
    const bool success = response.status >= 200 && response.status < 300;
    if (!success) {
        std::string snippet = response.body.substr(0, 300);
        throw Error::other("innertube " + endpoint + " returned HTTP " +
                           std::to_string(response.status) + ": " + snippet);
    }
    try {
        return json::parse(response.body);
    } catch (const json::exception& e) {
        throw Error::json(e.what());
    }
}

json InnerTube::context_object() const {
    return json{{"context", {{"client", ctx_.client_object()}}}};
}

}  // namespace ytgrab::innertube
