// nexigen::hub — pure HTTP parsing helpers (cold path, fuzzable).
//
// These helpers contain no sockets, no state, and no allocations beyond the
// returned strings. The HTTP server uses them directly and the libFuzzer target
// in tests/fuzz/http_parse_fuzz.cpp exercises them without a live server.
#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>

namespace nexigen::hub::detail {

/// @brief Percent-decode one URL component.
///
/// `+` becomes a space and every valid `%XX` escape becomes its byte. An
/// invalid or truncated escape is copied literally, keeping the helper total
/// for hostile request bytes. The result is a new string: no view into the
/// input is retained.
///
/// @param text Encoded component, typically a query value.
/// @return Decoded string; empty for an empty input.
/// @note Spec `hub/http-api`, requirement "Malformed requests are rejected
///       consistently" (an invalid percent escape is treated literally).
/// @note Cold path only; allocates the returned string.
[[nodiscard]] inline std::string url_decode(std::string_view text) {
    std::string out;
    out.reserve(text.size());
    for (std::size_t index = 0; index < text.size(); ++index) {
        if (text[index] == '%' && index + 2 < text.size()) {
            const auto hex = [](char c) -> int {
                if (c >= '0' && c <= '9') return c - '0';
                if (c >= 'a' && c <= 'f') return c - 'a' + 10;
                if (c >= 'A' && c <= 'F') return c - 'A' + 10;
                return -1;
            };
            const int hi = hex(text[index + 1]);
            const int lo = hex(text[index + 2]);
            if (hi >= 0 && lo >= 0) {
                out.push_back(static_cast<char>((hi << 4) | lo));
                index += 2;
                continue;
            }
        }
        out.push_back(text[index] == '+' ? ' ' : text[index]);
    }
    return out;
}

/// @brief Lowercase one ASCII letter, leaving every other byte unchanged.
///
/// Only `A`-`Z` are mapped; digits, punctuation, and non-ASCII bytes pass
/// through untouched, which is what HTTP token comparison needs.
///
/// @param value Byte to fold.
/// @return Lowercase ASCII letter, or `value` itself.
/// @note Cold path only; no locale dependency.
[[nodiscard]] inline char ascii_lower(char value) noexcept {
    return value >= 'A' && value <= 'Z' ? static_cast<char>(value - 'A' + 'a') : value;
}

/// @brief Compare two ASCII strings without regard to letter case.
///
/// Lengths must match exactly before any byte comparison. Non-ASCII bytes are
/// compared through `ascii_lower`, so they only match themselves.
///
/// @param left First view (for example a parsed header name).
/// @param right Second view (for example the canonical name).
/// @return `true` when both views have equal length and equal folded bytes.
/// @note Spec `hub/http-api`, requirement "Request headers are matched
///       case-insensitively".
[[nodiscard]] inline bool equals_ignore_case(std::string_view left,
                                             std::string_view right) noexcept {
    if (left.size() != right.size()) {
        return false;
    }
    for (std::size_t index = 0; index < left.size(); ++index) {
        if (ascii_lower(left[index]) != ascii_lower(right[index])) {
            return false;
        }
    }
    return true;
}

/// @brief Strip optional whitespace from both ends of a header field.
///
/// Only SP and HTAB are removed, per the HTTP optional-whitespace rule; other
/// bytes (including CR/LF) are left in place.
///
/// @param text View to trim.
/// @return Sub-view of `text` without leading or trailing SP/HTAB.
/// @note Spec `hub/http-api`, requirement "Request headers are matched
///       case-insensitively" (whitespace around a value is ignored).
[[nodiscard]] inline std::string_view trim_header(std::string_view text) noexcept {
    while (!text.empty() && (text.front() == ' ' || text.front() == '\t')) {
        text.remove_prefix(1);
    }
    while (!text.empty() && (text.back() == ' ' || text.back() == '\t')) {
        text.remove_suffix(1);
    }
    return text;
}

/// @brief Case-insensitive lookup of one header field in a raw request.
///
/// The request line is skipped and only the header block up to the first empty
/// line is inspected. The name must match a whole field name exactly (after
/// case folding), so a similarly named field such as `X-Last-Event-ID` is not
/// a match for `Last-Event-ID`. The returned view points into `request`.
///
/// @param request Raw request bytes, including the request line.
/// @param name Canonical header name to find.
/// @return Value view with surrounding SP/HTAB trimmed, or an empty view when
///         the header is absent, the block is incomplete, or no request line
///         exists.
/// @note Spec `hub/http-api`, requirement "Request headers are matched
///       case-insensitively".
/// @warning The view is only valid while `request` stays alive and unmodified.
[[nodiscard]] inline std::string_view header_value(std::string_view request,
                                                   std::string_view name) noexcept {
    std::size_t position = request.find("\r\n");
    if (position == std::string_view::npos) {
        return {};
    }
    position += 2;
    while (position < request.size()) {
        std::size_t line_end = request.find("\r\n", position);
        if (line_end == std::string_view::npos) {
            line_end = request.size();
        }
        const std::string_view line = request.substr(position, line_end - position);
        if (line.empty()) {
            break;
        }
        const std::size_t colon = line.find(':');
        if (colon != std::string_view::npos &&
            equals_ignore_case(trim_header(line.substr(0, colon)), name)) {
            return trim_header(line.substr(colon + 1));
        }
        position = line_end + 2;
    }
    return {};
}

/// @brief Extract and decode one query-string parameter.
///
/// Pairs are separated by `&` and split at the first `=`; the name is compared
/// byte-exactly. A pair without `=` yields an empty value, and the first match
/// wins. The returned value is percent-decoded with `url_decode`.
///
/// @param query Query string without the leading `?`.
/// @param key Parameter name to look up.
/// @return Decoded value of the first matching pair, or an empty string when
///         the key is absent or carries no `=`.
/// @note Spec `hub/http-api`, requirement "Malformed requests are rejected
///       consistently" (invalid `limit`/`since` values fall back to defaults).
/// @note Cold path only; allocates the returned string.
[[nodiscard]] inline std::string query_value(std::string_view query, std::string_view key) {
    std::size_t position = 0;
    while (position < query.size()) {
        const std::size_t ampersand = query.find('&', position);
        const std::size_t end = ampersand == std::string_view::npos ? query.size() : ampersand;
        const std::string_view pair = query.substr(position, end - position);
        const std::size_t equals = pair.find('=');
        const std::string_view name = pair.substr(0, equals);
        if (name == key) {
            return equals == std::string_view::npos ? std::string{}
                                                    : url_decode(pair.substr(equals + 1));
        }
        if (ampersand == std::string_view::npos) {
            break;
        }
        position = ampersand + 1;
    }
    return {};
}

/// @brief Decomposed HTTP request line.
///
/// All three members are non-owning views into the request buffer, so the
/// structure is valid only while that buffer lives and is not modified.
struct RequestLine {
    std::string_view method{};  ///< Request method token, e.g. `GET`.
    std::string_view path{};    ///< Request target including its query string.
    std::string_view version{}; ///< Protocol token, `HTTP/1.0` or `HTTP/1.1`.
};

/// @brief Outcome of parsing one request line.
///
/// The enumerators map directly onto the server's bounded error responses;
/// a parse never throws and leaves the output partially filled on failure.
///
/// @note Spec `hub/http-api`, requirement "Malformed requests are rejected
///       consistently".
enum class RequestLineError : std::uint8_t {
    None,             ///< The line parsed and the method is `GET`.
    BadRequest,       ///< The line has no method/target/version separation.
    BadVersion,       ///< The version is neither `HTTP/1.0` nor `HTTP/1.1`.
    MethodNotAllowed, ///< The version is supported but the method is not `GET`.
};

/// @brief Parse method, target, and version from the first request line.
///
/// The request line ends at the first `\r\n`; the method ends at the first
/// space and the target at the second. Only `GET` and `HTTP/1.0`/`HTTP/1.1`
/// are accepted, so the server can answer `405` and `400` respectively.
///
/// @param request Raw request bytes; only the first line is inspected.
/// @param out Destination receiving the three views. It may be partially
///        assigned even when an error is returned.
/// @return `RequestLineError::None` on success, otherwise the first failed
///         validation.
/// @note Spec `hub/http-api`, requirement "HTTP parsing and errors are
///       bounded" (only `GET`, only HTTP/1.0 and HTTP/1.1).
[[nodiscard]] inline RequestLineError parse_request_line(std::string_view request,
                                                         RequestLine& out) noexcept {
    const std::size_t first_line_end = request.find("\r\n");
    const std::string_view first_line = request.substr(0, first_line_end);
    const std::size_t first_space = first_line.find(' ');
    const std::size_t second_space = first_space == std::string_view::npos
                                         ? std::string_view::npos
                                         : first_line.find(' ', first_space + 1);
    if (first_space == std::string_view::npos || second_space == std::string_view::npos) {
        return RequestLineError::BadRequest;
    }
    out.method = first_line.substr(0, first_space);
    out.path = first_line.substr(first_space + 1, second_space - first_space - 1);
    out.version = first_line.substr(second_space + 1);
    if (out.version != "HTTP/1.1" && out.version != "HTTP/1.0") {
        return RequestLineError::BadVersion;
    }
    if (out.method != "GET") {
        return RequestLineError::MethodNotAllowed;
    }
    return RequestLineError::None;
}

} // namespace nexigen::hub::detail
