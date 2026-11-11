//! Minimal HTTP client on top of WinHTTP.
//!
//! Rust uses reqwest; on Windows the equivalent without extra dependencies is
//! the WinHTTP API. One `Client` maps to one `WinHttpOpen` session and can be
//! used from a single thread (the segmented downloader creates one per worker).

#pragma once

#include <functional>
#include <map>
#include <string>
#include <utility>
#include <vector>

namespace ytgrab::http {

using Header = std::pair<std::string, std::string>;
using Headers = std::vector<Header>;
using Sink = std::function<bool(const char* data, size_t len)>;

struct Response {
    int status = 0;
    std::string body;
    std::string content_range;
    std::map<std::string, std::string> headers;  // keys are lower case
};

class Client {
public:
    explicit Client(std::string user_agent);
    ~Client();
    Client(const Client&) = delete;
    Client& operator=(const Client&) = delete;

    /// Cookies are sent as a single `Cookie:` header on every request.
    void set_cookies(std::string header) { cookies_ = std::move(header); }
    const std::string& user_agent() const { return user_agent_; }

    Response get(const std::string& url, const Headers& headers = {});
    Response post(const std::string& url, const std::string& body,
                  const Headers& headers = {});

    /// Called once the response headers are in, before the body is read.
    /// Throwing from it aborts the transfer (used for 403/range handling).
    using HeaderSink = std::function<void(int status, const std::string& content_range)>;

    /// Streams the body into `sink` as it arrives. Returns the HTTP status.
    int get_stream(const std::string& url, const Headers& headers, const Sink& sink,
                   const HeaderSink& on_headers = {});

    /// Extra header captured from the last response body-less request.
    std::string last_content_range;

    struct Impl;

private:
    Impl* impl_;
    std::string user_agent_;
    std::string cookies_;
};

}  // namespace ytgrab::http
