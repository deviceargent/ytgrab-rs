//! InnerTube HTTP client.
//!
//! InnerTube is YouTube's internal JSON API. Each "client context" (android,
//! ios, web, ...) unlocks a different set of features and rate limits; the
//! ANDROID client hands back direct `googlevideo.com` URLs with no signature
//! cipher, which is why it is the default here too.

#pragma once

#include <optional>
#include <string>

#include <nlohmann/json.hpp>

#include "cookies.hpp"
#include "http.hpp"

namespace ytgrab::innertube {

using json = nlohmann::json;

struct ClientContext {
    std::string name;
    std::string version;
    std::string user_agent;
    std::string api_key;
    unsigned numeric_id = 0;
    /// Extra fields merged into the `client` object of the request context.
    json extra = json::object();

    static ClientContext android();
    static ClientContext ios();
    static ClientContext mweb();
    static ClientContext web();
    static ClientContext tv();
    static std::optional<ClientContext> by_name(const std::string& name);

    json client_object() const;
};

class InnerTube {
public:
    InnerTube(ClientContext ctx, const CookieJar* jar);
    InnerTube(const InnerTube&) = delete;
    InnerTube& operator=(const InnerTube&) = delete;

    const ClientContext& context() const { return ctx_; }
    const std::string& user_agent() const { return ctx_.user_agent; }

    /// POST to an InnerTube endpoint and return the parsed JSON.
    json post(const std::string& endpoint, const json& body);

    /// The `context` object every request body carries.
    json context_object() const;

private:
    ClientContext ctx_;
    const CookieJar* jar_;
    http::Client http_;
};

}  // namespace ytgrab::innertube
