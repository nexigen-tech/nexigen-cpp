// nexigen::tracing — JSON writer implementation (design D2/D11). See
// detail/json_writer.hpp for the escaping contract.
#include <charconv>
#include <cstddef>
#include <cstdint>
#include <string_view>
#include <system_error>

#include <nexigen/tracing/detail/json_writer.hpp>

namespace nexigen::tracing::detail {
namespace {

constexpr std::string_view hex_digits = "0123456789abcdef";

[[nodiscard]] char low_nibble(std::uint8_t b) noexcept {
    return hex_digits[static_cast<std::size_t>(b & 0x0FU)];
}

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
    char digits[24];
    const auto result = std::to_chars(digits, digits + sizeof(digits), value);
    if (result.ec != std::errc{}) {
        ok_ = false;
        return false;
    }
    return append_sv(std::string_view(digits, static_cast<std::size_t>(result.ptr - digits)));
}

bool Fixed_writer::append_i64(std::int64_t value) noexcept {
    char digits[24];
    const auto result = std::to_chars(digits, digits + sizeof(digits), value);
    if (result.ec != std::errc{}) {
        ok_ = false;
        return false;
    }
    return append_sv(std::string_view(digits, static_cast<std::size_t>(result.ptr - digits)));
}

bool Fixed_writer::append_double(double value) noexcept {
    char digits[40];
    const auto result = std::to_chars(digits, digits + sizeof(digits), value);
    if (result.ec != std::errc{}) {
        ok_ = false;
        return false;
    }
    return append_sv(std::string_view(digits, static_cast<std::size_t>(result.ptr - digits)));
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

bool Fixed_writer::append_hex16(std::uint64_t value) noexcept {
    if (!ok_ || (cap_ - len_) < 16U) {
        ok_ = false;
        return false;
    }
    for (std::size_t i = 0; i < 16U; ++i) {
        // Most significant nibble first: nibble i occupies bits 60-4i.
        const unsigned shift = 60U - static_cast<unsigned>(i) * 4U;
        buf_[len_++] = hex_digits[(value >> shift) & 0xFULL];
    }
    return true;
}

} // namespace nexigen::tracing::detail
