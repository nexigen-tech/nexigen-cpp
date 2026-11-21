// nexigen::core — diagnostics source vocabulary (change
// add-telemetry-diagnostics; spec core/diagnostics).
//
// Cold-path only: rendered by diagnostics, never by a recording path.
#pragma once

#include <array>
#include <cstdint>
#include <string_view>

#include <nlohmann/json.hpp>

#include <nexigen/core/enum_traits.hpp>

namespace nexigen::core {

/// @brief Subsystem that produced a diagnostic event.
/// @note Spec `core/diagnostics`, requirement "Diagnostic vocabulary and
///       serialization".
enum class DiagnosticSource : std::uint8_t {
    Chrono,  ///< Time-base client.
    Metrics, ///< Metric provider.
    Tracing, ///< Tracer provider or trace daemon.
};

/// @brief Vocabulary table for `DiagnosticSource`.
template <> struct EnumTraits<nexigen::core::DiagnosticSource> {
    /// @brief One table row: an enumerator and its canonical spelling.
    struct Entry {
        nexigen::core::DiagnosticSource value; ///< Enumerator described by the row.
        std::string_view name;                 ///< Canonical lowercase wire name.
    };
    /// @brief Name table covering every enumerator.
    static constexpr std::array<Entry, 3> entries{{
        {nexigen::core::DiagnosticSource::Chrono, "chrono"},
        {nexigen::core::DiagnosticSource::Metrics, "metrics"},
        {nexigen::core::DiagnosticSource::Tracing, "tracing"},
    }};
};

/// @brief Canonical lowercase name of a diagnostics source.
///
/// @param source Source to render.
/// @return Static-storage view; out-of-table values render `"Unknown"`.
/// @note Spec `core/diagnostics`, requirement "Diagnostic vocabulary and
///       serialization"; allocation-free, cold path.
[[nodiscard]] constexpr std::string_view to_string(DiagnosticSource source) noexcept {
    return core::enum_name(source);
}

/// @brief Parse a canonical lowercase source name.
///
/// @param name Candidate spelling.
/// @param out Destination, untouched on a miss.
/// @return `true` on an exact, case-sensitive match.
/// @note Spec `core/diagnostics`, requirement "Diagnostic vocabulary and
///       serialization".
[[nodiscard]] constexpr bool from_string(std::string_view name, DiagnosticSource& out) noexcept {
    return core::enum_value(name, out);
}

/// @brief ADL JSON writer: the canonical lowercase name.
///
/// @param j JSON value overwritten with the name.
/// @param source Source to serialize.
/// @note Cold path only; mandate 10.
inline void to_json(nlohmann::json& j, const DiagnosticSource source) {
    core::mandate::enum_to_json(j, source);
}

/// @brief ADL JSON reader, non-throwing under `JSON_NOEXCEPTION=1`.
///
/// @param j JSON value to read; non-string or unknown values leave `out` alone.
/// @param out Destination.
/// @note Cold path only; mandate 10.
inline void from_json(const nlohmann::json& j, DiagnosticSource& out) noexcept {
    core::mandate::enum_from_json(j, out);
}

} // namespace nexigen::core
