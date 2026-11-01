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

[[nodiscard]] inline char ascii_lower(char value) noexcept {
    return value >= 'A' && value <= 'Z'
               ? static_cast<char>(value - 'A' + 'a')
               : value;
}

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

[[nodiscard]] inline std::string_view trim_header(
    std::string_view text) noexcept {
    while (!text.empty() && (text.front() == ' ' || text.front() == '\t')) {
        text.remove_prefix(1);
    }
    while (!text.empty() && (text.back() == ' ' || text.back() == '\t')) {
        text.remove_suffix(1);
    }
    return text;
}

/// Case-insensitive header lookup. Returns a view into `request` or an empty
/// view when the header is absent. Only the header block is inspected.
[[nodiscard]] inline std::string_view header_value(
    std::string_view request, std::string_view name) noexcept {
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
        const std::string_view line =
            request.substr(position, line_end - position);
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

[[nodiscard]] inline std::string query_value(std::string_view query,
                                             std::string_view key) {
    std::size_t position = 0;
    while (position < query.size()) {
        const std::size_t ampersand = query.find('&', position);
        const std::size_t end =
            ampersand == std::string_view::npos ? query.size() : ampersand;
        const std::string_view pair = query.substr(position, end - position);
        const std::size_t equals = pair.find('=');
        const std::string_view name = pair.substr(0, equals);
        if (name == key) {
            return equals == std::string_view::npos
                       ? std::string{}
                       : url_decode(pair.substr(equals + 1));
        }
        if (ampersand == std::string_view::npos) {
            break;
        }
        position = ampersand + 1;
    }
    return {};
}

struct RequestLine {
    std::string_view method{};
    std::string_view path{};
    std::string_view version{};
};

enum class RequestLineError : std::uint8_t {
    None,
    BadRequest,
    BadVersion,
    MethodNotAllowed,
};

[[nodiscard]] inline RequestLineError parse_request_line(
    std::string_view request, RequestLine& out) noexcept {
    const std::size_t first_line_end = request.find("\r\n");
    const std::string_view first_line = request.substr(0, first_line_end);
    const std::size_t first_space = first_line.find(' ');
    const std::size_t second_space =
        first_space == std::string_view::npos
            ? std::string_view::npos
            : first_line.find(' ', first_space + 1);
    if (first_space == std::string_view::npos ||
        second_space == std::string_view::npos) {
        return RequestLineError::BadRequest;
    }
    out.method = first_line.substr(0, first_space);
    out.path =
        first_line.substr(first_space + 1, second_space - first_space - 1);
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
