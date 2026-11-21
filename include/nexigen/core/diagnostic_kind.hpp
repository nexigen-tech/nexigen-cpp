// nexigen::core — diagnostics kind vocabulary (change
// add-telemetry-diagnostics; spec core/diagnostics).
//
// Cold-path only: emitted on state transitions, never per record.
#pragma once

#include <array>
#include <cstdint>
#include <string_view>

#include <nlohmann/json.hpp>

#include <nexigen/core/enum_traits.hpp>

namespace nexigen::core {

/// @brief Kind of a diagnostics transition.
/// @note Spec `core/diagnostics`, requirement "Diagnostic vocabulary and
///       serialization".
enum class DiagnosticKind : std::uint8_t {
    Started,  ///< A component started or a binding was installed on a healthy one.
    Degraded, ///< A component entered a failing state.
    Recovered,///< A component returned to a healthy state.
    Quiesced, ///< A component stopped for good.
};

/// @brief Vocabulary table for `DiagnosticKind`.
template <> struct EnumTraits<nexigen::core::DiagnosticKind> {
    /// @brief One table row: an enumerator and its canonical spelling.
    struct Entry {
        nexigen::core::DiagnosticKind value; ///< Enumerator described by the row.
        std::string_view name;               ///< Canonical lowercase wire name.
    };
    /// @brief Name table covering every enumerator.
    static constexpr std::array<Entry, 4> entries{{
        {nexigen::core::DiagnosticKind::Started, "started"},
        {nexigen::core::DiagnosticKind::Degraded, "degraded"},
        {nexigen::core::DiagnosticKind::Recovered, "recovered"},
        {nexigen::core::DiagnosticKind::Quiesced, "quiesced"},
    }};
};

/// @brief Canonical lowercase name of a diagnostics kind.
///
/// @param kind Kind to render.
/// @return Static-storage view; out-of-table values render `"Unknown"`.
/// @note Spec `core/diagnostics`, requirement "Diagnostic vocabulary and
///       serialization"; allocation-free, cold path.
[[nodiscard]] constexpr std::string_view to_string(DiagnosticKind kind) noexcept {
    return core::enum_name(kind);
}

/// @brief Parse a canonical lowercase kind name.
///
/// @param name Candidate spelling.
/// @param out Destination, untouched on a miss.
/// @return `true` on an exact, case-sensitive match.
/// @note Spec `core/diagnostics`, requirement "Diagnostic vocabulary and
///       serialization".
[[nodiscard]] constexpr bool from_string(std::string_view name, DiagnosticKind& out) noexcept {
    return core::enum_value(name, out);
}

/// @brief ADL JSON writer: the canonical lowercase name.
///
/// @param j JSON value overwritten with the name.
/// @param kind Kind to serialize.
/// @note Cold path only; mandate 10.
inline void to_json(nlohmann::json& j, const DiagnosticKind kind) {
    core::mandate::enum_to_json(j, kind);
}

/// @brief ADL JSON reader, non-throwing under `JSON_NOEXCEPTION=1`.
///
/// @param j JSON value to read; non-string or unknown values leave `out` alone.
/// @param out Destination.
/// @note Cold path only; mandate 10.
inline void from_json(const nlohmann::json& j, DiagnosticKind& out) noexcept {
    core::mandate::enum_from_json(j, out);
}

} // namespace nexigen::core
