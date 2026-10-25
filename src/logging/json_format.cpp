// nexigen::logging — JSON line implementation (design.md D12). See
// detail/json_writer.hpp for the schema and escaping contract.
#include <cstddef>
#include <cstdint>
#include <string_view>

#include <nexigen/core/result.hpp>
#include <nexigen/chrono/time_format.hpp>
#include <nexigen/logging/detail/json_writer.hpp>
#include <nexigen/logging/log_level.hpp>

namespace nexigen::logging::detail {

using namespace std::literals::string_view_literals; // for the schema string literals

namespace {

constexpr std::string_view hex_digits = "0123456789abcdef";

[[nodiscard]] char low_nibble(std::uint8_t b) noexcept {
    return hex_digits[static_cast<std::size_t>(b & 0x0FU)];
}

/// Escape one byte: returns true if it was a special that consumed the
/// caller's switch (kept inline in append_escaped for clarity of the table).
void append_escape_byte(Fixed_writer& w, std::uint8_t b) noexcept {
    switch (b) {
        case '"':  w.append_char('\\'); w.append_char('"');  return;
        case '\\': w.append_char('\\'); w.append_char('\\'); return;
        case '\b': w.append_char('\\'); w.append_char('b');  return;
        case '\f': w.append_char('\\'); w.append_char('f');  return;
        case '\n': w.append_char('\\'); w.append_char('n');  return;
        case '\r': w.append_char('\\'); w.append_char('r');  return;
        case '\t': w.append_char('\\'); w.append_char('t');  return;
        default: break;
    }
    if (b < 0x20U) {
        w.append_char('\\');
        w.append_char('u');
        w.append_char('0');
        w.append_char('0');
        w.append_char(static_cast<char>(hex_digits[(b >> 4U) & 0x0FU]));
        w.append_char(low_nibble(b));
        return;
    }
    w.append_char(static_cast<char>(b)); // includes UTF-8 multibyte pass-through
}

} // namespace

bool Fixed_writer::append_char(char c) noexcept {
    if (!ok_ || len_ >= cap_) {
        ok_ = false;
        return false;
    }
    buf_[len_++] = c;
    return true;
}

bool Fixed_writer::append_sv(std::string_view sv) noexcept {
    if (!ok_ || sv.size() > cap_ - len_) {
        ok_ = false;
        return false;
    }
    for (const char c : sv) {
        buf_[len_++] = c;
    }
    return true;
}

bool Fixed_writer::append_u64(std::uint64_t value) noexcept {
    char digits[20];
    std::size_t n = 0;
    do {
        digits[n++] = static_cast<char>('0' + (value % 10U));
        value /= 10U;
    } while (value != 0U);
    for (std::size_t i = n; i-- > 0;) {
        if (!append_char(digits[i])) {
            return false;
        }
    }
    return true;
}

bool Fixed_writer::append_escaped(std::string_view sv) noexcept {
    for (const char c : sv) {
        append_escape_byte(*this, static_cast<std::uint8_t>(c));
        if (!ok_) {
            return false;
        }
    }
    return ok_;
}

std::size_t format_json_line(std::int64_t utc_ns, LogLevel level, std::string_view app,
                             std::uint32_t thread_id, std::string_view file_name,
                             std::uint32_t line, std::string_view function_name,
                             std::string_view message,
                             char (&scratch)[json_scratch_capacity]) noexcept {
    char ts[31];
    chrono::format_utc_iso8601_ns(utc_ns, ts);

    Fixed_writer w{scratch, json_scratch_capacity};
    static_cast<void>(w.append_sv("{\"timestamp\":\""sv));
    static_cast<void>(w.append_sv(std::string_view(ts, 30)));
    static_cast<void>(w.append_sv("\",\"level\":\""sv));
    static_cast<void>(w.append_sv(to_string(level)));
    static_cast<void>(w.append_sv("\",\"app\":\""sv));
    static_cast<void>(w.append_escaped(app));
    static_cast<void>(w.append_sv("\",\"thread_id\":"sv));
    static_cast<void>(w.append_u64(thread_id));
    static_cast<void>(w.append_sv(",\"message\":\"["sv));
    // Message body: "[<basename>:<line>] (<function>) <user message>" with
    // each piece JSON-escaped in place (escaping composes over concatenation).
    const std::size_t slash = file_name.find_last_of('/');
    const std::string_view base =
        (slash == std::string_view::npos) ? file_name : file_name.substr(slash + 1);
    static_cast<void>(w.append_escaped(base));
    static_cast<void>(w.append_char(':'));
    static_cast<void>(w.append_u64(line));
    static_cast<void>(w.append_sv("] ("sv));
    static_cast<void>(w.append_escaped(function_name));
    static_cast<void>(w.append_sv(") "sv));
    static_cast<void>(w.append_escaped(message));
    static_cast<void>(w.append_sv("\"}\n"sv));

    if (!w.ok()) {
        return 0; // overflowing line is skipped by the caller, never half-emitted
    }
    return w.size();
}

} // namespace nexigen::logging::detail
