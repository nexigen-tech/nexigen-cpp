// nexigen::tracing — SpanKind enum.
#pragma once

#include <array>
#include <cstdint>
#include <string>
#include <string_view>

#include <nlohmann/json.hpp>

#include <nexigen/core/enum_traits.hpp>
namespace nexigen::tracing {

/// @brief Causal role of a span inside the traced system (OTel-compatible names).
///
/// A `std::uint8_t` scoped enum, so it packs into the pinned `SpanRecord::kind`
/// and `SpanFrame::kind` bytes without widening either record. The canonical
/// spelling of every enumerator is the enumerator name itself and is what
/// `to_string`, `to_json` and the shared `std::formatter` render.
///
/// @note Spec `tracing/span-enums`, requirement "Span enum catalogue".
enum class SpanKind : std::uint8_t {
    Internal, ///< Work not attributed to an external transaction (default).
    Server,   ///< Synchronous handler of an inbound request.
    Client,   ///< Synchronous issuer of an outbound request.
    Producer, ///< Asynchronous producer of a message/event.
    Consumer, ///< Asynchronous consumer of a message/event.
};

} // namespace nexigen::tracing

// Vocabulary table for core::enum_name/enum_value (change refactor-enum-traits-helper).
namespace nexigen::core {

/// @brief Vocabulary table mapping every `SpanKind` enumerator to its canonical name.
///
/// The shared enum engine (`core::enum_name`/`core::enum_value`) scans this
/// table, so `to_string`/`from_string`/JSON/format all agree on one spelling
/// set by construction. Static constexpr storage; cold path only.
///
/// @note Spec `tracing/span-enums`, requirement "Constexpr span enum names".
template <> struct EnumTraits<nexigen::tracing::SpanKind> {
    /// @brief One table row: an enumerator and the exact text it renders as.
    struct Entry {
        nexigen::tracing::SpanKind value; ///< Enumerator described by the row.
        std::string_view name;            ///< Canonical, case-sensitive spelling.
    };
    /// @brief One row per enumerator, in declaration order (append-only).
    static constexpr std::array<Entry, 5> entries{{
        {nexigen::tracing::SpanKind::Internal, "Internal"},
        {nexigen::tracing::SpanKind::Server, "Server"},
        {nexigen::tracing::SpanKind::Client, "Client"},
        {nexigen::tracing::SpanKind::Producer, "Producer"},
        {nexigen::tracing::SpanKind::Consumer, "Consumer"},
    }};
};

} // namespace nexigen::core

namespace nexigen::tracing {

/// @brief Canonical name of a span kind; static storage, never allocates.
///
/// @param kind Enumerator to render.
/// @return Exact enumerator spelling (`"Internal"`, `"Server"`, ...); a value
///         obtained by casting an integer outside the table renders `"Unknown"`.
/// @note Spec `tracing/span-enums`, requirement "Constexpr span enum names".
/// @note Mandates 9/10/11: this is the single text source behind `to_json` and
///       the shared `std::formatter`, so every surface renders the same word.
[[nodiscard]] constexpr std::string_view to_string(SpanKind kind) noexcept {
    return core::enum_name(kind);
}

/// @brief Parse a canonical span-kind spelling into an enumerator.
///
/// @param name Exact, case-sensitive spelling (`"Client"`, never `"client"`).
/// @param out Destination updated only on a successful match.
/// @return `true` when a row matched; `false` leaves `out` unchanged.
/// @note Spec `tracing/span-enums`, requirement "Constexpr span enum names".
[[nodiscard]] constexpr bool from_string(std::string_view name, SpanKind& out) noexcept {
    return core::enum_value(name, out);
}

/// @brief Non-throwing JSON reader for `SpanKind` (cold path only).
///
/// Reads a JSON string through the shared `from_string`; non-string, missing
/// and unknown values are ignored rather than thrown or aborted.
///
/// @param j JSON value to read.
/// @param out Destination left untouched when the value is not a known name.
/// @note Spec `tracing/span-enums`, requirement "Non-throwing span enum JSON
///       serialization".
inline void from_json(const nlohmann::json& j, SpanKind& out) noexcept {
    core::mandate::enum_from_json(j, out);
}

/// @brief JSON writer rendering a span kind as its canonical-name string.
///
/// @param j Destination document or field to overwrite.
/// @param spanKind Enumerator to serialize (by value, matching the ADL shape).
/// @note Spec `tracing/span-enums`, requirement "Non-throwing span enum JSON
///       serialization".
/// @note Mandate 10: serialization is cold path; a span start never reaches it.
inline void to_json(nlohmann::json& j, const SpanKind spanKind) {
    core::mandate::enum_to_json(j, spanKind);
}

} // namespace nexigen::tracing
