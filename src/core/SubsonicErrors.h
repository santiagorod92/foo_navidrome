#pragma once

#include <string>

namespace navidrome {

enum class ErrorKind {
    None,
    NotConfigured,
    Network,
    Timeout,
    Tls,
    Auth,
    NotFound,
    RateLimited,
    ServerError,
    Parse,
    Cancelled,
    Unknown,
};

inline const char* errorKindName(ErrorKind kind) {
    switch (kind) {
        case ErrorKind::None:          return "None";
        case ErrorKind::NotConfigured: return "NotConfigured";
        case ErrorKind::Network:      return "Network";
        case ErrorKind::Timeout:      return "Timeout";
        case ErrorKind::Tls:          return "Tls";
        case ErrorKind::Auth:         return "Auth";
        case ErrorKind::NotFound:     return "NotFound";
        case ErrorKind::RateLimited:  return "RateLimited";
        case ErrorKind::ServerError:  return "ServerError";
        case ErrorKind::Parse:        return "Parse";
        case ErrorKind::Cancelled:    return "Cancelled";
        default:                      return "Unknown";
    }
}

inline bool isRetryable(ErrorKind kind) {
    switch (kind) {
        case ErrorKind::Network:
        case ErrorKind::Timeout:
        case ErrorKind::RateLimited:
        case ErrorKind::ServerError:
            return true;
        default:
            return false;
    }
}

inline ErrorKind httpStatusToErrorKind(int status) {
    if (status >= 200 && status < 300) return ErrorKind::None;
    if (status == 0)                   return ErrorKind::Network;
    if (status == 401 || status == 403) return ErrorKind::Auth;
    if (status == 404 || status == 410) return ErrorKind::NotFound;
    if (status == 429)                 return ErrorKind::RateLimited;
    if (status >= 500 && status < 600) return ErrorKind::ServerError;
    if (status >= 300 && status < 400) return ErrorKind::Network;
    return ErrorKind::ServerError;
}

inline ErrorKind subsonicCodeToErrorKind(int code) {
    switch (code) {
        case 40: case 41: case 50: return ErrorKind::Auth;
        case 70:                   return ErrorKind::NotFound;
        default:                   return ErrorKind::ServerError;
    }
}

struct Error {
    ErrorKind   kind = ErrorKind::None;
    int         http = 0;
    int         code = 0;
    std::string message;

    bool ok()        const { return kind == ErrorKind::None; }
    bool retryable() const { return isRetryable(kind); }
    const char* kindName() const { return errorKindName(kind); }
};

namespace retry {

constexpr int kMaxAttempts = 3;

inline bool again(const Error& e, int attempt) {
    return e.retryable() && attempt < kMaxAttempts;
}

inline int backoffMs(int attempt, int jitterMs) {
    return 300 * attempt + jitterMs;
}
}
}
