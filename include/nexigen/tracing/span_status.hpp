// nexigen::tracing — SpanStatus enum.
#pragma once

#include <array>
#include <cstdint>
#include <string>
#include <string_view>

#include <nlohmann/json.hpp>

#include <nexigen/core/enum_traits.hpp>
namespace nexigen::tracing {

/// Outcome of a span; Unset is the start-time default.
enum class SpanStatus : std::uint8_t {
    Unset, ///< No explicit outcome recorded (default).
    Ok,    ///< Completed successfully.
    Error, ///< Completed with a failure.
};

} // namespace nexigen::tracing

// Vocabulary table for core::enum_name/enum_value (change refactor-enum-traits-helper).
namespace nexigen::core {

template <> struct EnumTraits<nexigen::tracing::SpanStatus> {
    struct Entry {
        nexigen::tracing::SpanStatus value;
        std::string_view name;
    };
    static constexpr std::array<Entry, 3> entries{{
        {nexigen::tracing::SpanStatus::Unset, "Unset"},
        {nexigen::tracing::SpanStatus::Ok, "Ok"},
        {nexigen::tracing::SpanStatus::Error, "Error"},
    }};
};

} // namespace nexigen::core

namespace nexigen::tracing {

[[nodiscard]] constexpr std::string_view to_string(SpanStatus status) noexcept {
    return core::enum_name(status);
}

[[nodiscard]] constexpr bool from_string(std::string_view name, SpanStatus& out) noexcept {
    return core::enum_value(name, out);
}

inline void from_json(const nlohmann::json& j, SpanStatus& out) noexcept {
    core::mandate::enum_from_json(j, out);
}

inline void to_json(nlohmann::json& j, const SpanStatus spanStatus) {
    core::mandate::enum_to_json(j, spanStatus);
}

} // namespace nexigen::tracing
