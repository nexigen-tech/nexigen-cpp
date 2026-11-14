// nexigen::sm — PlatformFG state vocabulary (change
// introduce-platformfg-process-model; spec sm/state-machine).
//
// The fixed states of the platform-foundation Function Group (`PlatformFG`).
// Own enum with the full project conversion set.
#pragma once

#include <array>
#include <cstdint>
#include <format>
#include <string_view>

#include <nlohmann/json.hpp>

#include <nexigen/core/enum_traits.hpp>
#include <nexigen/core/serialization_mandate.hpp>

namespace nexigen::sm {

/// @brief The platform-foundation Function Group's states.
///
/// `PlatformFG` owns the platform foundation (message broker, SM, PHM, EM and
/// the future DataRecorder). `Off` is the mandatory initial state with no
/// bound processes; `Startup` brings the foundation up, `Verify` runs its
/// self-diagnosis, `Run` is the target working state, `Shutdown` is the
/// transition back into `Off`, and `Restart` is a fast reboot that returns to
/// `Startup` without passing through `Off`. Values are byte-sized; the
/// canonical spelling is the enumerator name.
///
/// @note Spec `sm/state-machine`, requirement "State machines execution".
/// @note Spec `sm/state-machine`, requirement "Startup and shutdown
///       sequencing".
enum class PlatformFG : std::uint8_t {
    Off,      ///< Mandatory initial state; no platform process is bound.
    Startup,  ///< The platform foundation is being brought up.
    Verify,   ///< Self-diagnosis processes are running.
    Run,      ///< The platform foundation is ready; application groups may start.
    Shutdown, ///< Transition state into `Off`.
    Restart,  ///< Fast reboot; returns to `Startup` without passing through `Off`.
};

/// @brief Canonical name of a PlatformFG state.
///
/// @param value Enumerator to render.
/// @return Static-storage view of the enumerator spelling; an out-of-table
///         value renders the engine fallback. Never allocates or throws.
/// @note Spec `sm/state-machine`, requirement "State machines execution".
/// @note Diagnostic string only (project mandate 9).
[[nodiscard]] constexpr std::string_view to_string(PlatformFG value) noexcept {
    return core::enum_name(value);
}

/// @brief Parse a canonical PlatformFG state name.
///
/// @param name View to parse; matching is exact and case-sensitive.
/// @param out Destination, assigned only on success.
/// @return `true` on an exact canonical spelling; `false` otherwise, leaving
///         `out` UNCHANGED.
/// @note Spec `sm/state-machine`, requirement "State machines execution".
[[nodiscard]] constexpr bool from_string(std::string_view name, PlatformFG& out) noexcept {
    return core::enum_value(name, out);
}

/// @brief ADL JSON writer: the value is exactly `to_string(state)`.
///
/// @param j Output JSON value replaced with the name string.
/// @param value Enumerator to serialize.
/// @note Cold-path JSON only (project mandate 10).
inline void to_json(nlohmann::json& j, const PlatformFG value) {
    core::mandate::enum_to_json(j, value);
}

/// @brief ADL JSON reader, strictly non-throwing under `JSON_NOEXCEPTION=1`.
///
/// A non-string or unrecognized value leaves the destination unchanged.
///
/// @param j JSON value to read.
/// @param out Destination, unchanged on any failure.
/// @note Cold-path JSON only (project mandate 10); safe under
///       `JSON_NOEXCEPTION=1`.
inline void from_json(const nlohmann::json& j, PlatformFG& out) noexcept {
    core::mandate::enum_from_json(j, out);
}

} // namespace nexigen::sm

// Vocabulary table for core::enum_name/enum_value (change refactor-enum-traits-helper).
namespace nexigen::core {

/// @brief Vocabulary table mapping `PlatformFG` enumerators to their names.
///
/// Backs `core::enum_name`/`enum_value`, hence `to_string`, `from_string`, and
/// the JSON adapters. The table is `constexpr` static storage and lookup is a
/// linear scan over six rows, so no allocation occurs.
///
/// @note Spec `sm/state-machine`, requirement "State machines execution".
template <> struct EnumTraits<nexigen::sm::PlatformFG> {
    /// @brief One enumerator/name pair of the vocabulary table.
    struct Entry {
        nexigen::sm::PlatformFG value; ///< Enumerator being named.
        std::string_view name;         ///< Canonical PascalCase spelling.
    };
    /// @brief Canonical enumerator table, in declaration order.
    static constexpr std::array<Entry, 6> entries{{
        {nexigen::sm::PlatformFG::Off, "Off"},
        {nexigen::sm::PlatformFG::Startup, "Startup"},
        {nexigen::sm::PlatformFG::Verify, "Verify"},
        {nexigen::sm::PlatformFG::Run, "Run"},
        {nexigen::sm::PlatformFG::Shutdown, "Shutdown"},
        {nexigen::sm::PlatformFG::Restart, "Restart"},
    }};
};

} // namespace nexigen::core

/// @brief `std::formatter` for `PlatformFG` (project mandate 11).
///
/// Delegates to `std::formatter<std::string_view>` after rendering the
/// canonical name, so `std::format("{}", state)` equals `to_string(state)`.
///
/// @note Spec `sm/state-machine`, requirement "State machines execution".
template <> struct std::formatter<nexigen::sm::PlatformFG> : std::formatter<std::string_view> {
    /// @brief Format a PlatformFG state through its canonical name.
    ///
    /// @tparam Ctx Format context type supplied by `std::format`.
    /// @param state State to render.
    /// @param ctx Output iterator context receiving the text.
    /// @return Iterator past the written characters.
    template <class Ctx> auto format(const nexigen::sm::PlatformFG state, Ctx& ctx) const {
        return std::formatter<std::string_view>::format(nexigen::sm::to_string(state), ctx);
    }
};
