// nexigen::sm — arbitration outcome vocabulary (change add-sm;
// spec sm/state-machine "Transition result vocabulary").
//
// The result of one state-change request. Own enum with the full project
// conversion set (mandates 9-11).
#pragma once

#include <array>
#include <cstdint>
#include <string_view>

#include <nlohmann/json.hpp>

#include <nexigen/core/enum_traits.hpp>

namespace nexigen::sm {

/// Outcome of a state-change request processed by the arbitration engine.
enum class TransitionResult : std::uint8_t {
    Success,             ///< The transition was accepted and applied.
    RejectedByPriority,  ///< A higher-priority requester holds the state.
    InvalidGraph,        ///< The graph forbids the requested transition.
    Timeout,             ///< The EM did not confirm the switch in time.
};

/// Canonical name of a transition result; allocation-free, constexpr.
[[nodiscard]] constexpr std::string_view to_string(TransitionResult value) noexcept {
    return core::enum_name(value);
}

/// Parse a canonical transition-result name; false leaves the out-parameter UNCHANGED.
[[nodiscard]] constexpr bool from_string(std::string_view name, TransitionResult& out) noexcept {
    return core::enum_value(name, out);
}

/// ADL JSON writer (cold path only): the value is exactly to_string(result).
inline void to_json(nlohmann::json& j, const TransitionResult value) {
    core::mandate::enum_to_json(j, value);
}

/// ADL JSON reader, strictly non-throwing under JSON_NOEXCEPTION=1.
inline void from_json(const nlohmann::json& j, TransitionResult& out) noexcept {
    core::mandate::enum_from_json(j, out);
}

} // namespace nexigen::sm

// Vocabulary table for core::enum_name/enum_value (change refactor-enum-traits-helper).
namespace nexigen::core {

template <> struct EnumTraits<nexigen::sm::TransitionResult> {
    struct Entry {
        nexigen::sm::TransitionResult value;
        std::string_view name;
    };
    static constexpr std::array<Entry, 4> entries{{
        {nexigen::sm::TransitionResult::Success, "Success"},
        {nexigen::sm::TransitionResult::RejectedByPriority, "RejectedByPriority"},
        {nexigen::sm::TransitionResult::InvalidGraph, "InvalidGraph"},
        {nexigen::sm::TransitionResult::Timeout, "Timeout"},
    }};
};

} // namespace nexigen::core
