// nexigen::tracing — SpanStatus enum.
#pragma once

#include <array>
#include <cstdint>
#include <string>
#include <string_view>

#include <nlohmann/json.hpp>

#include <nexigen/core/enum_traits.hpp>
namespace nexigen::tracing {

/// @brief Outcome of a span; `Unset` is the start-time default.
///
/// A `std::uint8_t` scoped enum that packs into the pinned `SpanRecord::status`
/// and `SpanFrame::status` bytes. A span starts `Unset`; the owner sets `Ok` or
/// `Error` through `ScopedSpan::set_status`, and a rejected update leaves the
/// previous value in place. Canonical spellings are the enumerator names.
///
/// @note Spec `tracing/span-enums`, requirement "Span enum catalogue".
enum class SpanStatus : std::uint8_t {
    Unset, ///< No explicit outcome recorded (default).
    Ok,    ///< Completed successfully.
    Error, ///< Completed with a failure.
};

} // namespace nexigen::tracing

// Vocabulary table for core::enum_name/enum_value (change refactor-enum-traits-helper).
namespace nexigen::core {

/// @brief Vocabulary table mapping every `SpanStatus` enumerator to its canonical name.
///
/// Backs `core::enum_name`/`core::enum_value`, hence `to_string`/`from_string`
/// and every serialization surface. Static constexpr storage; cold path only.
///
/// @note Spec `tracing/span-enums`, requirement "Constexpr span enum names".
template <> struct EnumTraits<nexigen::tracing::SpanStatus> {
    /// @brief One table row: an enumerator and the exact text it renders as.
    struct Entry {
        nexigen::tracing::SpanStatus value; ///< Enumerator described by the row.
        std::string_view name;              ///< Canonical, case-sensitive spelling.
    };
    /// @brief One row per enumerator, in declaration order (append-only).
    static constexpr std::array<Entry, 3> entries{{
        {nexigen::tracing::SpanStatus::Unset, "Unset"},
        {nexigen::tracing::SpanStatus::Ok, "Ok"},
        {nexigen::tracing::SpanStatus::Error, "Error"},
    }};
};

} // namespace nexigen::core

namespace nexigen::tracing {

/// @brief Canonical name of a span status; static storage, never allocates.
///
/// @param status Enumerator to render.
/// @return Exact enumerator spelling (`"Unset"`, `"Ok"`, `"Error"`); an
///         out-of-table cast renders the `"Unknown"` fallback.
/// @note Spec `tracing/span-enums`, requirement "Constexpr span enum names".
/// @note Mandates 9/10/11: one text source for `to_json` and the shared
///       `std::formatter`, so diagnostics agree on every surface.
[[nodiscard]] constexpr std::string_view to_string(SpanStatus status) noexcept {
    return core::enum_name(status);
}

/// @brief Parse a canonical span-status spelling into an enumerator.
///
/// @param name Exact, case-sensitive spelling (`"Ok"`, never `"ok"`).
/// @param out Destination updated only on a successful match.
/// @return `true` when a row matched; `false` leaves `out` unchanged.
/// @note Spec `tracing/span-enums`, requirement "Constexpr span enum names".
[[nodiscard]] constexpr bool from_string(std::string_view name, SpanStatus& out) noexcept {
    return core::enum_value(name, out);
}

/// @brief Non-throwing JSON reader for `SpanStatus` (cold path only).
///
/// @param j JSON value to read.
/// @param out Destination left untouched when the value is not a known name.
/// @note Spec `tracing/span-enums`, requirement "Non-throwing span enum JSON
///       serialization".
inline void from_json(const nlohmann::json& j, SpanStatus& out) noexcept {
    core::mandate::enum_from_json(j, out);
}

/// @brief JSON writer rendering a span status as its canonical-name string.
///
/// @param j Destination document or field to overwrite.
/// @param spanStatus Enumerator to serialize (by value, matching the ADL shape).
/// @note Spec `tracing/span-enums`, requirement "Non-throwing span enum JSON
///       serialization".
/// @note Mandate 10: cold path only; never reached from a span start or end.
inline void to_json(nlohmann::json& j, const SpanStatus spanStatus) {
    core::mandate::enum_to_json(j, spanStatus);
}

} // namespace nexigen::tracing
