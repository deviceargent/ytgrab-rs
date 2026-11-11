#include "cookies.hpp"

#include <fstream>

#include "error.hpp"
#include "util.hpp"

namespace ytgrab {
namespace {

/// Split a url into (scheme, host, path).
void crack_url(const std::string& url, std::string* scheme, std::string* host,
               std::string* path) {
    std::string rest = url;
    *scheme = "https";
    size_t scheme_end = rest.find("://");
    if (scheme_end != std::string::npos) {
        *scheme = util::to_lower(rest.substr(0, scheme_end));
        rest = rest.substr(scheme_end + 3);
    }
    size_t slash = rest.find('/');
    if (slash == std::string::npos) {
        *host = util::to_lower(rest);
        *path = "/";
    } else {
        *host = util::to_lower(rest.substr(0, slash));
        *path = rest.substr(slash);
        size_t query = path->find('?');
        if (query != std::string::npos) *path = path->substr(0, query);
    }
    size_t colon = host->find(':');
    if (colon != std::string::npos) *host = host->substr(0, colon);
}

bool domain_matches(const std::string& cookie_domain, const std::string& host) {
    if (cookie_domain.empty()) return false;
    std::string domain = util::to_lower(cookie_domain);
    if (!domain.empty() && domain.front() == '.') {
        std::string bare = domain.substr(1);
        if (host == bare) return true;
        std::string suffix = "." + bare;
        return host.size() > suffix.size() &&
               host.compare(host.size() - suffix.size(), suffix.size(), suffix) == 0;
    }
    return host == domain;
}

}  // namespace

std::string CookieJar::header_for(const std::string& url) const {
    std::string scheme, host, path;
    crack_url(url, &scheme, &host, &path);

    std::string header;
    for (const auto& c : cookies_) {
        if (!domain_matches(c.domain, host)) continue;
        std::string cookie_path = c.path.empty() ? "/" : c.path;
        if (path.compare(0, cookie_path.size(), cookie_path) != 0) continue;
        if (c.secure && scheme != "https") continue;
        if (!header.empty()) header += "; ";
        header += c.name + "=" + c.value;
    }
    return header;
}

CookieJar CookieJar::from_netscape_file(const std::string& path) {
    std::ifstream in(util::fspath(path), std::ios::binary);
    if (!in) throw Error::io("cannot open " + path);

    CookieJar jar;
    std::string line;
    while (std::getline(in, line)) {
        line = util::trim(line);
        if (line.empty()) continue;
        if (util::starts_with(line, "#HttpOnly_")) line = line.substr(10);
        if (line.empty() || line[0] == '#') continue;

        auto fields = util::split(line, '\t');
        if (fields.size() < 7) continue;
        Cookie c;
        c.domain = fields[0];
        c.path = fields[2];
        c.secure = util::eq_ignore_ascii_case(fields[3], "true");
        c.expires = std::atoll(fields[4].c_str());
        c.name = fields[5];
        c.value = fields[6];
        jar.add(std::move(c));
    }
    return jar;
}

}  // namespace ytgrab
