//! Error type shared by the whole port.
//!
//! The Rust version uses a single `Error` enum with `thiserror`; this mirrors
//! it so the messages the CLI prints stay identical.

#pragma once

#include <stdexcept>
#include <string>

namespace ytgrab {

enum class ErrKind {
    Http,
    Json,
    Io,
    Unavailable,
    UnsupportedUrl,
    NoFormat,
    Ffmpeg,
    Db,
    /// googlevideo refused the range: a fresh player request may help.
    SignedUrl,
    Other,
};

class Error : public std::runtime_error {
public:
    ErrKind kind = ErrKind::Other;
    int status = 0;
    long long itag = 0;

    explicit Error(std::string message, ErrKind k = ErrKind::Other)
        : std::runtime_error(std::move(message)), kind(k) {}

    static Error other(const std::string& message) { return Error(message, ErrKind::Other); }
    static Error io(const std::string& message) { return Error("io error: " + message, ErrKind::Io); }
    static Error http(const std::string& message) {
        return Error("network error: " + message, ErrKind::Http);
    }
    static Error json(const std::string& message) {
        return Error("malformed response: " + message, ErrKind::Json);
    }
    static Error unavailable(const std::string& message) {
        return Error("this video is unavailable: " + message, ErrKind::Unavailable);
    }
    static Error unsupported_url(const std::string& message) {
        return Error("unsupported url: " + message, ErrKind::UnsupportedUrl);
    }
    static Error no_format(const std::string& message) {
        return Error("no matching format for selector `" + message + "`", ErrKind::NoFormat);
    }
    static Error ffmpeg(const std::string& message) {
        return Error("ffmpeg failed: " + message, ErrKind::Ffmpeg);
    }
    static Error db(const std::string& message) {
        return Error("database error: " + message, ErrKind::Db);
    }
    static Error signed_url(int status, long long itag) {
        Error e("googlevideo rejected the request (HTTP " + std::to_string(status) +
                    ") for format " + std::to_string(itag),
                ErrKind::SignedUrl);
        e.status = status;
        e.itag = itag;
        return e;
    }
};

}  // namespace ytgrab
