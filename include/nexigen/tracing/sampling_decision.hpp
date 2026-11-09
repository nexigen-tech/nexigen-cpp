// nexigen::tracing — SamplingDecision enum.
#pragma once

#include <array>
#include <cstdint>
#include <string>
#include <string_view>

#include <nlohmann/json.hpp>

#include <nexigen/core/enum_traits.hpp>
namespace nexigen::tracing {

/// The whole sampling vocabulary (design non-goal: no policy engines).
enum class SamplingDecision : std::uint8_t {
    Drop,            ///< Do not record: start_span short-circuits.
    RecordAndSample, ///< Record the span (default).
};

} // namespace nexigen::tracing

// Vocabulary table for core::enum_name/enum_value (change refactor-enum-traits-helper).
namespace nexigen::core {

template <> struct EnumTraits<nexigen::tracing::SamplingDecision> {
    struct Entry {
        nexigen::tracing::SamplingDecision value;
        std::string_view name;
    };
    static constexpr std::array<Entry, 2> entries{{
        {nexigen::tracing::SamplingDecision::Drop, "Drop"},
        {nexigen::tracing::SamplingDecision::RecordAndSample, "RecordAndSample"},
    }};
};

} // namespace nexigen::core

namespace nexigen::tracing {

[[nodiscard]] constexpr std::string_view to_string(SamplingDecision decision) noexcept {
    return core::enum_name(decision);
}

[[nodiscard]] constexpr bool from_string(std::string_view name, SamplingDecision& out) noexcept {
    return core::enum_value(name, out);
}

inline void from_json(const nlohmann::json& j, SamplingDecision& out) noexcept {
    core::mandate::enum_from_json(j, out);
}

inline void to_json(nlohmann::json& j, const SamplingDecision samplingDecision) {
    core::mandate::enum_to_json(j, samplingDecision);
}

} // namespace nexigen::tracing
