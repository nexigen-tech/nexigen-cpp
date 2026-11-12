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

/// @brief The three states an interval timer occupies; nothing else exists.
///
/// `Idle` is the fresh/reset state with no measurement, `Running` owns an open
/// interval that every sample advances, and `Stopped` is frozen with the
/// accumulated total kept. `restart()` and lap bookkeeping never add a fourth
/// state. The `uint8_t` underlying type keeps the enum inside `Stopwatch`'s
/// single cache line and inside `LapSnapshot`.
///
/// @note Spec `diagnostics/stopwatch-state`, requirement "StopwatchState
///       enumerates the stopwatch lifecycle".
/// @note Spec `diagnostics/stopwatch-state`, requirement "StopwatchState formats
///       through std::format".
/// @note String, JSON and `std::formatter` support follow project mandates 9,
///       10 and 11; all four entry points are cold path only.
enum class StopwatchState : std::uint8_t {
    Idle,    ///< Fresh or reset: no open interval, no accumulated time.
    Running, ///< An interval is open; elapsed samples advance the reading.
    Stopped, ///< Frozen: accumulated total kept, boundary reads stay put.
};

} // namespace nexigen::diagnostics

// Vocabulary table for core::enum_name/enum_value (change refactor-enum-traits-helper).
namespace nexigen::core {

/// @brief Vocabulary table binding `StopwatchState` to its PascalCase names.
///
/// Backs `core::enum_name`/`core::enum_value`, which in turn implement
/// `to_string`, `from_string` and the JSON pair. Static storage, allocation-free,
/// cold path only.
template <> struct EnumTraits<nexigen::diagnostics::StopwatchState> {
    /// @brief One name/value row of the vocabulary table.
    struct Entry {
        nexigen::diagnostics::StopwatchState value; ///< Enumerator described by the row.
        std::string_view name;                      ///< Canonical PascalCase spelling.
    };

    /// @brief Ordered rows; the names are the wire contract, the order is not.
    static constexpr std::array<Entry, 3> entries{{
        {nexigen::diagnostics::StopwatchState::Idle, "Idle"},
        {nexigen::diagnostics::StopwatchState::Running, "Running"},
        {nexigen::diagnostics::StopwatchState::Stopped, "Stopped"},
    }};
};

} // namespace nexigen::core

namespace nexigen::diagnostics {

/// @brief Canonical PascalCase name of a state.
///
/// The exact spelling used by `from_string`, the JSON payload and diagnostic
/// strings. Static storage, allocation-free; a value outside the table renders
/// `"Unknown"` (a rendering, not an enumerator) instead of invoking UB.
///
/// @param state Enumerator to render.
/// @return Static-storage view of the matching name, or `"Unknown"`.
/// @note Spec `diagnostics/stopwatch-state`, requirement "StopwatchState
///       enumerates the stopwatch lifecycle"; project mandate 9.
[[nodiscard]] constexpr std::string_view to_string(StopwatchState state) noexcept {
    return core::enum_name(state);
}

/// @brief Parse a canonical PascalCase state name.
///
/// @param name Candidate spelling, matched exactly and case-sensitively.
/// @param out Destination, left UNCHANGED on a miss.
/// @return `true` when a name matched and `out` was assigned; `false`
///         otherwise.
/// @note Spec `diagnostics/stopwatch-state`, requirement "StopwatchState
///       enumerates the stopwatch lifecycle".
[[nodiscard]] constexpr bool from_string(std::string_view name, StopwatchState& out) noexcept {
    return core::enum_value(name, out);
}

/// @brief Write the state as its canonical PascalCase name (cold path).
///
/// @param j Output JSON value overwritten with the name string.
/// @param state Enumerator to serialize.
/// @note Spec `diagnostics/stopwatch-state`, requirement "StopwatchState
///       serializes to nlohmann JSON without throwing"; project mandate 10.
inline void to_json(nlohmann::json& j, const StopwatchState state) {
    core::mandate::enum_to_json(j, state);
}

/// @brief Read a state from its canonical PascalCase name (cold path).
///
/// Strictly non-throwing under `JSON_NOEXCEPTION=1`: non-string values yield
/// nullptr from `get_ptr` and unknown names leave `out` unchanged.
///
/// @param j Input JSON value; a non-string or unknown name leaves `out`
///        unchanged.
/// @param out Destination, untouched unless the name parses.
/// @note Spec `diagnostics/stopwatch-state`, requirement "StopwatchState
///       serializes to nlohmann JSON without throwing"; project mandate 10.
inline void from_json(const nlohmann::json& j, StopwatchState& out) noexcept {
    core::mandate::enum_from_json(j, out);
}

} // namespace nexigen::diagnostics

// StopwatchState formats as its canonical PascalCase name (mandate 11): parse
// and fill/align are handled by the string_view formatter, and the generic
// mandate formatter derives it from the free `to_string` above. The rendering
// lives outside this header's declarations, so no specialization is added here.

namespace nexigen::diagnostics {

// Layout/name pins (mandate 8; the enum rides single-cache-line aggregates).
static_assert(to_string(StopwatchState::Idle) == "Idle");
static_assert(to_string(StopwatchState::Running) == "Running");
static_assert(to_string(StopwatchState::Stopped) == "Stopped");
static_assert(to_string(static_cast<StopwatchState>(99U)) == "Unknown");
static_assert(sizeof(StopwatchState) == 1U);

} // namespace nexigen::diagnostics
