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

#include <cstdint>
#include <format>
#include <string>
#include <string_view>

#include <nlohmann/json.hpp>

namespace nexigen::diagnostics {

/// The three states an interval timer occupies; nothing else exists.
enum class StopwatchState : std::uint8_t {
    Idle,    ///< Fresh or reset: no open interval, no accumulated time.
    Running, ///< An interval is open; elapsed samples advance the reading.
    Stopped, ///< Frozen: accumulated total kept, boundary reads stay put.
};

/// Compile-time PascalCase name of a state — the exact spelling used by
/// from_string, the JSON `"state"` payload and diagnostic strings. Static
/// storage, allocation-free; the `default:` arm keeps a garbage-cast value
/// well-defined ("Unknown" is a rendering, not an enumerator).
[[nodiscard]] constexpr std::string_view to_string(StopwatchState state) noexcept {
    switch (state) {
        case StopwatchState::Idle:    return "Idle";
        case StopwatchState::Running: return "Running";
        case StopwatchState::Stopped: return "Stopped";
        default:                      return "Unknown";
    }
}

/// Parse a canonical PascalCase state name (the exact to_string spelling).
/// Returns false leaving the out-parameter UNCHANGED otherwise.
[[nodiscard]] constexpr bool from_string(std::string_view name, StopwatchState& out) noexcept {
    if (name == "Idle")    { out = StopwatchState::Idle;    return true; }
    if (name == "Running") { out = StopwatchState::Running; return true; }
    if (name == "Stopped") { out = StopwatchState::Stopped; return true; }
    return false;
}

/// ADL JSON writer (cold path only): the JSON value is exactly to_string(state).
inline void to_json(nlohmann::json& j, const StopwatchState state) {
    j = std::string(to_string(state));
}

/// ADL JSON reader, strictly non-throwing under JSON_NOEXCEPTION=1: non-string
/// values yield nullptr from get_ptr, unknown names leave `out` unchanged.
inline void from_json(const nlohmann::json& j, StopwatchState& out) noexcept {
    const auto* const name = j.get_ptr<const nlohmann::json::string_t*>();
    if (name != nullptr) {
        static_cast<void>(from_string(std::string_view(*name), out));
    }
}

} // namespace nexigen::diagnostics

/// StopwatchState formats as its canonical PascalCase name (mandate 11):
/// parse and fill/align handled by the string_view formatter; context
/// templated because libstdc++ 15 evaluates formattability against a
/// substituted context.
template <>
struct std::formatter<nexigen::diagnostics::StopwatchState>
    : std::formatter<std::string_view> {
    template <class Ctx>
    constexpr auto format(const nexigen::diagnostics::StopwatchState state, Ctx& ctx) const {
        return std::formatter<std::string_view>::format(
            nexigen::diagnostics::to_string(state), ctx);
    }
};

namespace nexigen::diagnostics {

// Layout/name pins (mandate 8; the enum rides single-cache-line aggregates).
static_assert(to_string(StopwatchState::Idle) == "Idle");
static_assert(to_string(StopwatchState::Running) == "Running");
static_assert(to_string(StopwatchState::Stopped) == "Stopped");
static_assert(to_string(static_cast<StopwatchState>(99U)) == "Unknown");
static_assert(sizeof(StopwatchState) == 1U);

} // namespace nexigen::diagnostics
