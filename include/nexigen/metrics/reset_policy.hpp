// nexigen::metrics — ResetPolicy enum.
#pragma once

#include <array>
#include <cstdint>
#include <string>
#include <string_view>

#include <nlohmann/json.hpp>

#include <nexigen/core/enum_traits.hpp>
namespace nexigen::metrics {

/// Whether observations span the instrument's lifetime or the last window.
enum class ResetPolicy : std::uint8_t {
    Cumulative,
    ResetOnRead,
};

} // namespace nexigen::metrics

// Vocabulary table for core::enum_name/enum_value (change refactor-enum-traits-helper).
namespace nexigen::core {

template <> struct EnumTraits<nexigen::metrics::ResetPolicy> {
    struct Entry {
        nexigen::metrics::ResetPolicy value;
        std::string_view name;
    };
    static constexpr std::array<Entry, 2> entries{{
        {nexigen::metrics::ResetPolicy::Cumulative, "Cumulative"},
        {nexigen::metrics::ResetPolicy::ResetOnRead, "ResetOnRead"},
    }};
};

} // namespace nexigen::core

namespace nexigen::metrics {

[[nodiscard]] constexpr std::string_view to_string(ResetPolicy policy) noexcept {
    return core::enum_name(policy);
}

[[nodiscard]] constexpr bool from_string(std::string_view name, ResetPolicy& out) noexcept {
    return core::enum_value(name, out);
}

inline void from_json(const nlohmann::json& j, ResetPolicy& out) noexcept {
    core::mandate::enum_from_json(j, out);
}

inline void to_json(nlohmann::json& j, const ResetPolicy resetPolicy) {
    core::mandate::enum_to_json(j, resetPolicy);
}

} // namespace nexigen::metrics
