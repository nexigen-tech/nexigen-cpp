// nexigen::tracing — SpanStatus enum.
#pragma once

#include <cstdint>
#include <string>
#include <string_view>

#include <nlohmann/json.hpp>

namespace nexigen::tracing {

/// Outcome of a span; Unset is the start-time default.
enum class SpanStatus : std::uint8_t {
    Unset, ///< No explicit outcome recorded (default).
    Ok,    ///< Completed successfully.
    Error, ///< Completed with a failure.
};

[[nodiscard]] constexpr std::string_view to_string(SpanStatus status) noexcept {
    switch (status) {
        case SpanStatus::Unset: return "Unset";
        case SpanStatus::Ok:    return "Ok";
        case SpanStatus::Error: return "Error";
        default:                return "Unknown";
    }
}

[[nodiscard]] constexpr bool from_string(std::string_view name, SpanStatus& out) noexcept {
    if (name == "Unset") {
        out = SpanStatus::Unset;
        return true;
    }
    if (name == "Ok") {
        out = SpanStatus::Ok;
        return true;
    }
    if (name == "Error") {
        out = SpanStatus::Error;
        return true;
    }
    return false;
}

inline void from_json(const nlohmann::json& j, SpanStatus& out) noexcept {
    const auto* const name = j.get_ptr<const nlohmann::json::string_t*>();
    if (name != nullptr) {
        static_cast<void>(from_string(std::string_view(*name), out));
    }
}


inline void to_json(nlohmann::json& j, const SpanStatus spanStatus) {
    j = std::string(to_string(spanStatus));
}

} // namespace nexigen::tracing
