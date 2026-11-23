#include "json_escape.hpp"

#include <cstdio>

namespace nexigen::logview {

namespace {
constexpr char kHex[] = "0123456789abcdef";
} // namespace

std::size_t json_escape(std::string_view text, char* out, std::size_t capacity) noexcept {
    if (capacity == 0U) {
        return 0U;
    }
    std::size_t used = 0;
    const auto append = [&](char value) {
        if (used + 1U >= capacity) {
            return false;
        }
        out[used++] = value;
        return true;
    };
    for (const char ch : text) {
        const auto byte = static_cast<unsigned char>(ch);
        if (ch == '"' || ch == '\\') {
            if (!append('\\') || !append(ch)) {
                out[0] = '\0';
                return 0U;
            }
        } else if (byte < 0x20U) {
            if (!append('\\') || !append('u') || !append('0') || !append('0') ||
                !append(kHex[(byte >> 4U) & 0x0FU]) || !append(kHex[byte & 0x0FU])) {
                out[0] = '\0';
                return 0U;
            }
        } else if (!append(ch)) {
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
