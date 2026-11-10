// nexigen::com — wait-set callback control flow (change add-com-waitset;
// spec com/waitset "Callback dispatch and source identity").
//
// The return type of a WaitSet callback: Continue keeps the processing loop
// running to handle the next fired source, Stop ends it. Own enum so business
// code never names iox2::CallbackProgression.
#pragma once

#include <array>
#include <cstdint>
#include <string_view>

#include <nlohmann/json.hpp>

#include <nexigen/core/enum_traits.hpp>

namespace nexigen::com {

/// Controls the WaitSet processing loop from inside the callback.
enum class CallbackProgression : std::uint8_t {
    Continue,  ///< Handle the next fired source.
    Stop,      ///< Return from wait_and_process.
};

/// Canonical name of a progression value; allocation-free, constexpr.
[[nodiscard]] constexpr std::string_view to_string(CallbackProgression value) noexcept {
    return core::enum_name(value);
}

/// Parse a canonical progression name; false leaves the out-parameter UNCHANGED.
[[nodiscard]] constexpr bool from_string(std::string_view name, CallbackProgression& out) noexcept {
    return core::enum_value(name, out);
}

/// ADL JSON writer (cold path only): the value is exactly to_string(progression).
inline void to_json(nlohmann::json& j, const CallbackProgression value) {
    core::mandate::enum_to_json(j, value);
}

/// ADL JSON reader, strictly non-throwing under JSON_NOEXCEPTION=1.
inline void from_json(const nlohmann::json& j, CallbackProgression& out) noexcept {
    core::mandate::enum_from_json(j, out);
}

} // namespace nexigen::com

// Vocabulary table for core::enum_name/enum_value (change refactor-enum-traits-helper).
namespace nexigen::core {

template <> struct EnumTraits<nexigen::com::CallbackProgression> {
    struct Entry {
        nexigen::com::CallbackProgression value;
        std::string_view name;
    };
    static constexpr std::array<Entry, 2> entries{{
        {nexigen::com::CallbackProgression::Continue, "Continue"},
        {nexigen::com::CallbackProgression::Stop, "Stop"},
    }};
};

} // namespace nexigen::core
