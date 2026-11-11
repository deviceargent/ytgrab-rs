//! Cookie handling: Netscape cookie files and the in-memory jar.

#pragma once

#include <string>
#include <vector>

namespace ytgrab {

struct Cookie {
    std::string domain;
    std::string path;
    std::string name;
    std::string value;
    bool secure = false;
    long long expires = 0;
};

class CookieJar {
public:
    void add(Cookie cookie) { cookies_.push_back(std::move(cookie)); }
    const std::vector<Cookie>& all() const { return cookies_; }
    bool empty() const { return cookies_.empty(); }

    /// The `Cookie:` header value for a url, or an empty string.
    std::string header_for(const std::string& url) const;

    /// Load a Netscape-format cookie file.
    static CookieJar from_netscape_file(const std::string& path);

private:
    std::vector<Cookie> cookies_;
};

}  // namespace ytgrab
