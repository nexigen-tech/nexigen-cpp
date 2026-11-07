// nexigen tools — shared command-line helpers
// (change refactor-shared-tool-cli-parser).
#pragma once

#include <cstdint>
#include <string_view>

namespace nexigen::tools {

/// Parse an unsigned 64-bit decimal. Returns false on empty input, a
/// non-digit, or overflow, leaving `out` unchanged on failure.
[[nodiscard]] inline bool parse_u64(std::string_view text, std::uint64_t& out) noexcept {
    if (text.empty()) {
        return false;
    }
    std::uint64_t value = 0;
    for (const char c : text) {
        if (c < '0' || c > '9') {
            return false;
        }
        const std::uint64_t digit = static_cast<std::uint64_t>(c - '0');
        if (value > (UINT64_MAX - digit) / 10ULL) {
            return false;
        }
        value = value * 10ULL + digit;
    }
    out = value;
    return true;
}

} // namespace nexigen::tools
