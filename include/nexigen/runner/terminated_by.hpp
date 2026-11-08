// nexigen::runner — TerminatedBy enum.
#pragma once
#include <cstdint>
#include <string>
#include <string_view>

namespace nexigen::runner {

enum class TerminatedBy : std::uint8_t {
    Self,    ///< The child exited on its own.
    Runner,  ///< The runner/stop command sent the terminating signal.
    Unknown, ///< No attribution is available.
};

/// Canonical lower-case name used by exit.json.
[[nodiscard]] constexpr std::string_view to_string(TerminatedBy value) noexcept {
    switch (value) {
        case TerminatedBy::Self:    return "self";
        case TerminatedBy::Runner:  return "runner";
        case TerminatedBy::Unknown: return "unknown";
        default:                    return "Unknown";
    }
}

/// Parse a canonical name; false leaves `out` unchanged.
[[nodiscard]] constexpr bool from_string(std::string_view name, TerminatedBy& out) noexcept {
    if (name == "self") {
        out = TerminatedBy::Self;
        return true;
    }
    if (name == "runner") {
        out = TerminatedBy::Runner;
        return true;
    }
    if (name == "unknown") {
        out = TerminatedBy::Unknown;
        return true;
    }
    return false;
}

} // namespace nexigen::runner
