#include "http.hpp"

#include <windows.h>
#include <winhttp.h>

#include <algorithm>
#include <cctype>
#include <vector>

#include "error.hpp"
#include "util.hpp"

#ifndef WINHTTP_ACCESS_TYPE_AUTOMATIC_PROXY
#define WINHTTP_ACCESS_TYPE_AUTOMATIC_PROXY 4
#endif

namespace ytgrab::http {
namespace {

struct Handle {
    HINTERNET h = nullptr;
    Handle() = default;
    explicit Handle(HINTERNET v) : h(v) {}
    Handle(const Handle&) = delete;
    Handle& operator=(const Handle&) = delete;
    Handle(Handle&& o) noexcept : h(o.h) { o.h = nullptr; }
    Handle& operator=(Handle&& o) noexcept {
        if (this != &o) {
            close();
            h = o.h;
            o.h = nullptr;
        }
        return *this;
    }
    ~Handle() { close(); }
    void close() {
        if (h) {
            WinHttpCloseHandle(h);
            h = nullptr;
        }
    }
    explicit operator bool() const { return h != nullptr; }
};

std::string last_error_text(const char* what) {
    return std::string(what) + " (WinHTTP error " + std::to_string(GetLastError()) + ")";
}

std::string lower(std::string s) {
    for (char& c : s) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return s;
}

/// Split a raw CRLF header block into a lower-cased key/value map.
void parse_raw_headers(const std::wstring& raw, Response& out) {
    std::string text = util::narrow(raw);
    size_t start = text.find("\r\n");
    if (start == std::string::npos) return;
    start += 2;
    while (start < text.size()) {
        size_t end = text.find("\r\n", start);
        if (end == std::string::npos) end = text.size();
        std::string line = text.substr(start, end - start);
        start = end + 2;
        size_t colon = line.find(':');
        if (colon == std::string::npos) continue;
        std::string key = lower(util::trim(line.substr(0, colon)));
        std::string value = util::trim(line.substr(colon + 1));
        if (key == "content-range") out.content_range = value;
        out.headers[key] = value;
    }
}

}  // namespace

struct Client::Impl {
    Handle session;
};

Client::Client(std::string user_agent) : impl_(new Impl()), user_agent_(std::move(user_agent)) {
    std::wstring ua = util::widen(user_agent_);
    impl_->session = Handle(WinHttpOpen(ua.c_str(), WINHTTP_ACCESS_TYPE_AUTOMATIC_PROXY,
                                        WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0));
    if (!impl_->session) {
        impl_->session = Handle(WinHttpOpen(ua.c_str(), WINHTTP_ACCESS_TYPE_DEFAULT_PROXY,
                                            WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0));
    }
    if (!impl_->session) throw Error::http(last_error_text("could not open an HTTP session"));
    WinHttpSetTimeouts(impl_->session.h, 30000, 30000, 30000, 30000);
    DWORD decompress = 0;  // never let WinHTTP gunzip: downloads need raw bytes
    WinHttpSetOption(impl_->session.h, WINHTTP_OPTION_DECOMPRESSION, &decompress,
                     sizeof(decompress));
}

Client::~Client() { delete impl_; }

namespace {

struct Request {
    Handle connect;
    Handle request;
    Response meta;
};

/// Open the request, send it and receive the response headers.
Request open_request(Client::Impl* impl, const std::string& method, const std::string& url,
                     const std::string& cookies, const Headers& headers, const std::string* body) {
    Request out;

    std::wstring wide_url = util::widen(url);
    URL_COMPONENTS uc{};
    uc.dwStructSize = sizeof(uc);
    uc.dwSchemeLength = static_cast<DWORD>(-1);
    uc.dwHostNameLength = static_cast<DWORD>(-1);
    uc.dwUrlPathLength = static_cast<DWORD>(-1);
    uc.dwExtraInfoLength = static_cast<DWORD>(-1);
    if (!WinHttpCrackUrl(wide_url.c_str(), 0, 0, &uc)) {
        throw Error::http("could not parse the url " + url);
    }
    std::wstring host(uc.lpszHostName, uc.dwHostNameLength);
    std::wstring target(uc.lpszUrlPath, uc.dwUrlPathLength);
    if (uc.dwExtraInfoLength > 0) target.append(uc.lpszExtraInfo, uc.dwExtraInfoLength);

    out.connect = Handle(WinHttpConnect(impl->session.h, host.c_str(), uc.nPort, 0));
    if (!out.connect) throw Error::http(last_error_text("could not connect"));

    DWORD flags = uc.nScheme == INTERNET_SCHEME_HTTPS ? WINHTTP_FLAG_SECURE : 0;
    std::wstring verb = util::widen(method);
    out.request = Handle(
        WinHttpOpenRequest(out.connect.h, verb.c_str(), target.c_str(), nullptr,
                           WINHTTP_NO_REFERER, WINHTTP_DEFAULT_ACCEPT_TYPES, flags));
    if (!out.request) throw Error::http(last_error_text("could not create the request"));

    std::string header_block;
    for (const auto& h : headers) header_block += h.first + ": " + h.second + "\r\n";
    if (!cookies.empty()) header_block += "Cookie: " + cookies + "\r\n";
    std::wstring wide_headers = util::widen(header_block);

    BOOL ok = WinHttpSendRequest(
        out.request.h, wide_headers.empty() ? WINHTTP_NO_ADDITIONAL_HEADERS : wide_headers.c_str(),
        static_cast<DWORD>(-1),
        body ? const_cast<char*>(body->data()) : WINHTTP_NO_REQUEST_DATA,
        body ? static_cast<DWORD>(body->size()) : 0, body ? static_cast<DWORD>(body->size()) : 0, 0);
    if (!ok) throw Error::http(last_error_text("could not send the request"));
    if (!WinHttpReceiveResponse(out.request.h, nullptr)) {
        throw Error::http(last_error_text("no response"));
    }

    DWORD status = 0;
    DWORD size = sizeof(status);
    if (!WinHttpQueryHeaders(out.request.h, WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER,
                             WINHTTP_HEADER_NAME_BY_INDEX, &status, &size,
                             WINHTTP_NO_HEADER_INDEX)) {
        throw Error::http(last_error_text("could not read the status code"));
    }
    out.meta.status = static_cast<int>(status);

    DWORD raw_size = 0;
    WinHttpQueryHeaders(out.request.h, WINHTTP_QUERY_RAW_HEADERS_CRLF, WINHTTP_HEADER_NAME_BY_INDEX,
                        WINHTTP_NO_OUTPUT_BUFFER, &raw_size, WINHTTP_NO_HEADER_INDEX);
    if (raw_size > 0) {
        std::wstring raw(raw_size / sizeof(wchar_t), L'\0');
        if (WinHttpQueryHeaders(out.request.h, WINHTTP_QUERY_RAW_HEADERS_CRLF,
                                WINHTTP_HEADER_NAME_BY_INDEX, raw.data(), &raw_size,
                                WINHTTP_NO_HEADER_INDEX)) {
            raw.resize(raw_size / sizeof(wchar_t));
            parse_raw_headers(raw, out.meta);
        }
    }
    return out;
}

/// Pump the body of an already received response into `sink`.
bool read_body(HINTERNET request, const Sink& sink, std::string* body) {
    std::vector<char> buffer(64 * 1024);
    while (true) {
        DWORD read = 0;
        if (!WinHttpReadData(request, buffer.data(), static_cast<DWORD>(buffer.size()), &read)) {
            return false;
        }
        if (read == 0) return true;
        if (body) body->append(buffer.data(), read);
        if (sink && !sink(buffer.data(), read)) return true;
    }
}

}  // namespace

Response Client::get(const std::string& url, const Headers& headers) {
    Request req = open_request(impl_, "GET", url, cookies_, headers, nullptr);
    Response out = req.meta;
    if (!read_body(req.request.h, nullptr, &out.body)) {
        throw Error::http(last_error_text("the transfer was interrupted"));
    }
    last_content_range = out.content_range;
    return out;
}

Response Client::post(const std::string& url, const std::string& body, const Headers& headers) {
    Request req = open_request(impl_, "POST", url, cookies_, headers, &body);
    Response out = req.meta;
    if (!read_body(req.request.h, nullptr, &out.body)) {
        throw Error::http(last_error_text("the transfer was interrupted"));
    }
    last_content_range = out.content_range;
    return out;
}

int Client::get_stream(const std::string& url, const Headers& headers, const Sink& sink,
                       const HeaderSink& on_headers) {
    Request req = open_request(impl_, "GET", url, cookies_, headers, nullptr);
    last_content_range = req.meta.content_range;
    if (on_headers) on_headers(req.meta.status, req.meta.content_range);
    if (!read_body(req.request.h, sink, nullptr)) {
        // A truncated body is reported like a dropped connection in reqwest:
        // the caller resumes from where it stopped.
        return req.meta.status;
    }
    return req.meta.status;
}

}  // namespace ytgrab::http
