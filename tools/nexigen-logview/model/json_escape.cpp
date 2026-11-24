#include "json_escape.hpp"

#include <cstdio>

namespace nexigen::logview {

namespace {
constexpr char kHex[] = "0123456789abcdef";

/// @brief Append one byte, refusing to overrun the terminating NUL slot.
///
/// @param out Destination buffer.
/// @param capacity Destination capacity including the NUL.
/// @param used Write cursor; advanced on success.
/// @param value Byte to append.
/// @return `true` when the byte fit.
[[nodiscard]] bool append_byte(char* out, std::size_t capacity, std::size_t& used,
                               char value) noexcept {
    if (used + 1U >= capacity) {
        return false;
    }
    out[used++] = value;
    return true;
}

/// @brief Append the six-character `\u00XX` escape for one control byte.
///
/// @param byte Control byte below 0x20.
/// @param out Destination buffer.
/// @param capacity Destination capacity including the NUL.
/// @param used Write cursor; advanced on success.
/// @return `true` when the whole escape fit.
[[nodiscard]] bool append_unicode_escape(unsigned char byte, char* out, std::size_t capacity,
                                         std::size_t& used) noexcept {
    return append_byte(out, capacity, used, '\\') && append_byte(out, capacity, used, 'u') &&
           append_byte(out, capacity, used, '0') && append_byte(out, capacity, used, '0') &&
           append_byte(out, capacity, used, kHex[(byte >> 4U) & 0x0FU]) &&
           append_byte(out, capacity, used, kHex[byte & 0x0FU]);
}

/// @brief Append one escaped byte: quote/backslash, control byte or verbatim.
///
/// @param byte Source byte.
/// @param out Destination buffer.
/// @param capacity Destination capacity including the NUL.
/// @param used Write cursor; advanced on success.
/// @return `true` when the byte (and its escape) fit.
[[nodiscard]] bool append_escaped(unsigned char byte, char* out, std::size_t capacity,
                                  std::size_t& used) noexcept {
    if (byte == '"' || byte == '\\') {
        return append_byte(out, capacity, used, '\\') &&
               append_byte(out, capacity, used, static_cast<char>(byte));
    }
    if (byte < 0x20U) {
        return append_unicode_escape(byte, out, capacity, used);
    }
    return append_byte(out, capacity, used, static_cast<char>(byte));
}

} // namespace

std::size_t json_escape(std::string_view text, char* out, std::size_t capacity) noexcept {
    if (capacity == 0U) {
        return 0U;
    }
    std::size_t used = 0;
    for (const char ch : text) {
        if (!append_escaped(static_cast<unsigned char>(ch), out, capacity, used)) {
            out[0] = '\0';
            return 0U;
        }
    }
    out[used] = '\0';
    return used;
}

std::string json_escape(std::string_view text) {
    std::string out;
    out.reserve(text.size() + 8U);
    char buffer[8];
    for (const char ch : text) {
        const auto byte = static_cast<unsigned char>(ch);
        if (ch == '"' || ch == '\\') {
            out.push_back('\\');
            out.push_back(ch);
        } else if (byte < 0x20U) {
            static_cast<void>(std::snprintf(buffer, sizeof(buffer), "\\u%04x", byte));
            out += buffer;
        } else {
            out.push_back(ch);
        }
    }
    return out;
}

} // namespace nexigen::logview
