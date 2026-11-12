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

/// @brief Return value of a `WaitSet` callback that controls the processing loop.
///
/// An own enum so business code never names `iox2::CallbackProgression`. The
/// callback receives it per fired source and the wait entry point translates
/// it into the transport's control flow.
///
/// @note Spec `com/waitset`, requirement "Callback dispatch and source
///       identity". Mandates 9/10/11 cover `to_string`, the ADL JSON pair, and
///       the generic `std::formatter`.
enum class CallbackProgression : std::uint8_t {
    Continue, ///< Handle the next fired source.
    Stop,     ///< Return from wait_and_process.
};

/// @brief Canonical name of a progression value.
///
/// @param value Progression to render.
/// @return The canonical enumerator name; allocation-free and `constexpr`.
/// @note Mandate 9 (universal `to_string`); spec `com/waitset`, requirement
///       "WaitSet diagnostics".
[[nodiscard]] constexpr std::string_view to_string(CallbackProgression value) noexcept {
    return core::enum_name(value);
}

/// @brief Parse a canonical progression name.
///
/// @param name Candidate canonical name, as produced by `to_string`.
/// @param out Receives the parsed value; left UNCHANGED when parsing fails.
/// @return `true` when `name` matched an enumerator, `false` otherwise.
/// @note Mandate 9; allocation-free and `constexpr`. Spec `com/waitset`,
///       requirement "WaitSet diagnostics".
[[nodiscard]] constexpr bool from_string(std::string_view name, CallbackProgression& out) noexcept {
    return core::enum_value(name, out);
}

/// @brief ADL JSON writer: the value is exactly `to_string(progression)`.
///
/// @param j Output JSON value, replaced by the canonical name `string`.
/// @param value Progression to serialize.
/// @note Mandate 10 (universal JSON); allocates, so cold path only. Spec
///       `com/waitset`, requirement "WaitSet diagnostics".
inline void to_json(nlohmann::json& j, const CallbackProgression value) {
    core::mandate::enum_to_json(j, value);
}

/// @brief ADL JSON reader, strictly non-throwing under `JSON_NOEXCEPTION=1`.
///
/// @param j Input JSON value; a non-string or unknown name leaves `out`
///        unchanged.
/// @param out Receives the parsed progression on success.
/// @note Mandate 10 (universal JSON); spec `com/waitset`, requirement
///       "WaitSet diagnostics".
inline void from_json(const nlohmann::json& j, CallbackProgression& out) noexcept {
    core::mandate::enum_from_json(j, out);
}

} // namespace nexigen::com

// Vocabulary table for core::enum_name/enum_value (change refactor-enum-traits-helper).
namespace nexigen::core {

/// @brief Vocabulary table binding `CallbackProgression` to its canonical names.
///
/// Consumed by `core::enum_name` / `core::enum_value`.
///
/// @note Spec `com/waitset`, requirement "WaitSet diagnostics".
template <> struct EnumTraits<nexigen::com::CallbackProgression> {
    /// @brief One value/name pair of the vocabulary table.
    struct Entry {
        nexigen::com::CallbackProgression value; ///< Enumerator.
        std::string_view name;                   ///< Canonical enumerator name.
    };
    /// @brief Canonical table; exactly one entry per enumerator.
    static constexpr std::array<Entry, 2> entries{{
        {nexigen::com::CallbackProgression::Continue, "Continue"},
        {nexigen::com::CallbackProgression::Stop, "Stop"},
    }};
};

} // namespace nexigen::core
