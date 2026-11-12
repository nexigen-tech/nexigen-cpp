// nexigen::app — why the infrastructure stopped (change
// rework-app-as-infrastructure; spec app/application).
//
// Full enum contract: constexpr to_string/from_string, non-throwing ADL
// to_json/from_json under JSON_NOEXCEPTION=1, and std::formatter. The values
// are stable and append-only.
#pragma once

#include <array>
#include <cstdint>
#include <format>
#include <string>
#include <string_view>

#include <nlohmann/json.hpp>

#include <nexigen/core/enum_traits.hpp>
namespace nexigen::app {

/// @brief Why the infrastructure run loop stopped.
///
/// `None` is the sentinel meaning "no stop requested yet"; the first recorded
/// cause wins and later requests never replace it. The vocabulary spellings are
/// `none`, `requested`, `sigint` and `sigterm`, and the values are stable and
/// append-only. Only a single `std::atomic` write records a cause, so the type
/// is safe for the signal watcher and the host tick to publish concurrently.
///
/// @note Spec `app/application`, requirement "Infrastructure stop reason is
///       serializable".
/// @note String, JSON and `std::formatter` support follow project mandates 9,
///       10 and 11; formatting and serialization are cold path only.
enum class InfrastructureStopReason : std::uint8_t {
    None,      ///< No stop requested yet.
    Requested, ///< Host code requested a stop (explicit call or false tick).
    Sigint,    ///< SIGINT received.
    Sigterm,   ///< SIGTERM received.
};

} // namespace nexigen::app

// Vocabulary table for core::enum_name/enum_value (change refactor-enum-traits-helper).
namespace nexigen::core {

/// @brief Vocabulary table binding `InfrastructureStopReason` to its names.
///
/// Backs `core::enum_name`/`core::enum_value`, which in turn implement
/// `to_string`, `from_string` and the JSON pair. Cold path only, static storage,
/// allocation-free.
template <> struct EnumTraits<nexigen::app::InfrastructureStopReason> {
    /// @brief One name/value row of the vocabulary table.
    struct Entry {
        nexigen::app::InfrastructureStopReason value; ///< Enumerator described by the row.
        std::string_view name;                        ///< Canonical lowercase spelling.
    };

    /// @brief Ordered rows; the names are the wire contract, the order is not.
    static constexpr std::array<Entry, 4> entries{{
        {nexigen::app::InfrastructureStopReason::None, "none"},
        {nexigen::app::InfrastructureStopReason::Requested, "requested"},
        {nexigen::app::InfrastructureStopReason::Sigint, "sigint"},
        {nexigen::app::InfrastructureStopReason::Sigterm, "sigterm"},
    }};
};

} // namespace nexigen::core

namespace nexigen::app {

/// @brief Canonical lowercase name of a stop reason.
///
/// @param reason Enumerator to render.
/// @return Static-storage view of the matching name; a value outside the table
///         renders `"Unknown"`.
/// @note Spec `app/application`, requirement "Infrastructure stop reason is
///       serializable"; project mandate 9. `constexpr`, allocation-free, cold
///       path only.
[[nodiscard]] constexpr std::string_view to_string(InfrastructureStopReason reason) noexcept {
    return core::enum_name(reason);
}

/// @brief Parse a canonical lowercase stop-reason name.
///
/// @param name Candidate spelling, matched exactly and case-sensitively.
/// @param out Destination, left unchanged on a miss.
/// @return `true` when a name matched and `out` was assigned; `false`
///         otherwise.
/// @note Spec `app/application`, requirement "Infrastructure stop reason is
///       serializable"; project mandates 9 and 10.
[[nodiscard]] constexpr bool from_string(std::string_view name,
                                         InfrastructureStopReason& out) noexcept {
    return core::enum_value(name, out);
}

/// @brief Write the stop reason as its canonical lowercase name (cold path).
///
/// @param j Output JSON value overwritten with the name string.
/// @param reason Enumerator to serialize.
/// @note Spec `app/application`, requirement "Infrastructure stop reason is
///       serializable"; project mandate 10. Non-throwing under
///       `JSON_NOEXCEPTION=1`.
inline void to_json(nlohmann::json& j, const InfrastructureStopReason reason) {
    core::mandate::enum_to_json(j, reason);
}

/// @brief Read a stop reason from its canonical lowercase name (cold path).
///
/// @param j Input JSON value; a non-string or unknown name leaves `out`
///        unchanged.
/// @param out Destination, untouched unless the name parses.
/// @note Spec `app/application`, requirement "Infrastructure stop reason is
///       serializable"; project mandate 10. Never throws or aborts under
///       `JSON_NOEXCEPTION=1`.
inline void from_json(const nlohmann::json& j, InfrastructureStopReason& out) noexcept {
    core::mandate::enum_from_json(j, out);
}

} // namespace nexigen::app
