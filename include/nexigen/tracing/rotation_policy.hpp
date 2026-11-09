// nexigen::tracing — RotationPolicy enum (change
// refactor-hub-model-config-headers; extracted verbatim from
// json_file_trace_sink.hpp).
#pragma once

#include <array>
#include <cstdint>
#include <string_view>

#include <nlohmann/json.hpp>

#include <nexigen/core/enum_traits.hpp>

namespace nexigen::tracing {

/// Rotation schedule selector (enum contract of mandates 9–11).
enum class RotationPolicy : std::uint8_t {
    None,   ///< Single file for the sink's lifetime.
    Hourly, ///< A new file every UTC hour.
    Daily,  ///< A new file every UTC day.
};

} // namespace nexigen::tracing

// Vocabulary table for core::enum_name/enum_value (change refactor-enum-traits-helper).
namespace nexigen::core {

template <> struct EnumTraits<nexigen::tracing::RotationPolicy> {
    struct Entry {
        nexigen::tracing::RotationPolicy value;
        std::string_view name;
    };
    static constexpr std::array<Entry, 3> entries{{
        {nexigen::tracing::RotationPolicy::None, "None"},
        {nexigen::tracing::RotationPolicy::Hourly, "Hourly"},
        {nexigen::tracing::RotationPolicy::Daily, "Daily"},
    }};
};

} // namespace nexigen::core

namespace nexigen::tracing {

/// Compile-time canonical name; "Unknown" for garbage casts.
[[nodiscard]] constexpr std::string_view to_string(RotationPolicy policy) noexcept {
    return core::enum_name(policy);
}

/// Parse a canonical policy name; false leaves the out-parameter unchanged.
[[nodiscard]] constexpr bool from_string(std::string_view name, RotationPolicy& out) noexcept {
    return core::enum_value(name, out);
}

/// ADL JSON writer/reader (cold path; result.hpp get_ptr pattern).
inline void to_json(nlohmann::json& j, const RotationPolicy policy) {
    core::mandate::enum_to_json(j, policy);
}
inline void from_json(const nlohmann::json& j, RotationPolicy& out) noexcept {
    core::mandate::enum_from_json(j, out);
}

} // namespace nexigen::tracing
