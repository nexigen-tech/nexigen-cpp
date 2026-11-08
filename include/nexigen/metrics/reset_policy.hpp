// nexigen::metrics — ResetPolicy enum.
#pragma once

#include <cstdint>
#include <string>
#include <string_view>

#include <nlohmann/json.hpp>

namespace nexigen::metrics {

/// Whether observations span the instrument's lifetime or the last window.
enum class ResetPolicy : std::uint8_t {
    Cumulative,
    ResetOnRead,
};

[[nodiscard]] constexpr std::string_view to_string(ResetPolicy policy) noexcept {
    switch (policy) {
        case ResetPolicy::Cumulative:  return "Cumulative";
        case ResetPolicy::ResetOnRead: return "ResetOnRead";
        default:                       return "Unknown";
    }
}

[[nodiscard]] constexpr bool from_string(std::string_view name, ResetPolicy& out) noexcept {
    if (name == "Cumulative") {
        out = ResetPolicy::Cumulative;
        return true;
    }
    if (name == "ResetOnRead") {
        out = ResetPolicy::ResetOnRead;
        return true;
    }
    return false;
}

inline void from_json(const nlohmann::json& j, ResetPolicy& out) noexcept {
    const auto* const name = j.get_ptr<const nlohmann::json::string_t*>();
    if (name != nullptr) {
        static_cast<void>(from_string(std::string_view(*name), out));
    }
}


inline void to_json(nlohmann::json& j, const ResetPolicy resetPolicy) {
    j = std::string(to_string(resetPolicy));
}

} // namespace nexigen::metrics
