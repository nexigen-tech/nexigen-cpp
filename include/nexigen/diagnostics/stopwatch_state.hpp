// nexigen::diagnostics — the stopwatch lifecycle enum (design.md D5; spec
// diagnostics/stopwatch-state).
//
// StopwatchState is the whole state vocabulary of the module's interval timer:
// Idle (fresh / reset, no measurement yet), Running (an interval is open and
// every sample advances the reading), Stopped (frozen: accumulated total kept,
// no sample advances it). Exactly `states.size()` states — the strictly
// three-state lifecycle; `restart()` and lap bookkeeping live on the Stopwatch
// itself and never mint a fourth state.
//
// uint8_t underlying type: the enum lands inside Stopwatch's single cache line
// and inside LapSnapshot, and is the shape std::atomic<StopwatchState> could
// hold in one byte if a future change ever needs a shared flag (scoped for now:
// Stopwatch is a single-thread measuring domain, design D2/D3).
//
// Serialization contract (mandates 9–11; same pattern as span_enums.hpp /
// log_level.hpp): constexpr PascalCase to_string with an "Unknown" default arm
// for garbage-cast values, exact-match constexpr from_string leaving the
// out-parameter untouched on failure, ADL to_json/from_json that run under
// JSON_NOEXCEPTION=1 (-fno-exceptions builds), and a std::formatter
// specialization that renders through the canonical name.
#pragma once

#include <array>
#include <cstdint>
#include <format>
#include <string>
#include <string_view>

#include <nlohmann/json.hpp>

#include <nexigen/core/enum_traits.hpp>
namespace nexigen::diagnostics {

/// The three states an interval timer occupies; nothing else exists.
enum class StopwatchState : std::uint8_t {
    Idle,    ///< Fresh or reset: no open interval, no accumulated time.
    Running, ///< An interval is open; elapsed samples advance the reading.
    Stopped, ///< Frozen: accumulated total kept, boundary reads stay put.
};

} // namespace nexigen::diagnostics

namespace nexigen::diagnostics {

/// Compile-time PascalCase name of a state — the exact spelling used by
/// from_string, the JSON `"state"` payload and diagnostic strings. Static
/// storage, allocation-free; the `default:` arm keeps a garbage-cast value
/// well-defined ("Unknown" is a rendering, not an enumerator).
[[nodiscard]] constexpr std::string_view to_string(StopwatchState state) noexcept {
    return core::enum_name(state);
}

/// Parse a canonical PascalCase state name (the exact to_string spelling).
/// Returns false leaving the out-parameter UNCHANGED otherwise.
[[nodiscard]] constexpr bool from_string(std::string_view name, StopwatchState& out) noexcept {
    return core::enum_value(name, out);
}

/// ADL JSON writer (cold path only): the JSON value is exactly to_string(state).
inline void to_json(nlohmann::json& j, const StopwatchState state) {
    core::mandate::enum_to_json(j, state);
}

/// ADL JSON reader, strictly non-throwing under JSON_NOEXCEPTION=1: non-string
/// values yield nullptr from get_ptr, unknown names leave `out` unchanged.
inline void from_json(const nlohmann::json& j, StopwatchState& out) noexcept {
    core::mandate::enum_from_json(j, out);
}

} // namespace nexigen::diagnostics

// Vocabulary table for core::enum_name/enum_value (change refactor-enum-traits-helper).
namespace nexigen::core {

template <> struct EnumTraits<nexigen::diagnostics::StopwatchState> {
    struct Entry {
        nexigen::diagnostics::StopwatchState value;
        std::string_view name;
    };
    static constexpr std::array<Entry, 3> entries{{
        {nexigen::diagnostics::StopwatchState::Idle, "Idle"},
        {nexigen::diagnostics::StopwatchState::Running, "Running"},
        {nexigen::diagnostics::StopwatchState::Stopped, "Stopped"},
    }};
};

} // namespace nexigen::core

/// StopwatchState formats as its canonical PascalCase name (mandate 11):
/// parse and fill/align handled by the string_view formatter; context
/// templated because libstdc++ 15 evaluates formattability against a
/// substituted context.

namespace nexigen::diagnostics {

// Layout/name pins (mandate 8; the enum rides single-cache-line aggregates).
static_assert(to_string(StopwatchState::Idle) == "Idle");
static_assert(to_string(StopwatchState::Running) == "Running");
static_assert(to_string(StopwatchState::Stopped) == "Stopped");
static_assert(to_string(static_cast<StopwatchState>(99U)) == "Unknown");
static_assert(sizeof(StopwatchState) == 1U);

} // namespace nexigen::diagnostics
