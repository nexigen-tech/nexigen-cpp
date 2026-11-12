// nexigen::hub — StreamKind enum.
#pragma once
#include <array>
#include <cstdint>
#include <string>
#include <string_view>

#include <nlohmann/json.hpp>

#include <nexigen/core/enum_traits.hpp>
namespace nexigen::hub {

/// @brief The seven stream families the hub can tail.
///
/// The enumerator spelling, lowercase name, and JSON string are one canonical
/// vocabulary: `StreamKind` values travel as lowercase names through
/// `to_string`/`from_string` and the ADL JSON adapters. Values are byte-sized
/// and append-only so a descriptor stays small and stable.
///
/// @note Spec `hub/streaming`, requirement "Streams have stable identities"
///       (the seven kinds).
/// @note Spec `hub/streaming`, requirement "Streaming types follow the
///       serialization contract".
enum class StreamKind : std::uint8_t {
    App,              ///< Application JSONL records.
    Metrics,          ///< Harvested metrics batches.
    Traces,           ///< Trace records.
    Chrono,           ///< Chrono snapshots.
    Stdout,           ///< Captured standard output text.
    Stderr,           ///< Captured standard error text.
    ExecutionManager, ///< Suite execution_manager JSONL log.
};

} // namespace nexigen::hub

// Vocabulary table for core::enum_name/enum_value (change refactor-enum-traits-helper).
namespace nexigen::core {

/// @brief Vocabulary table mapping `StreamKind` enumerators to lowercase names.
///
/// Backs `core::enum_name`/`enum_value`, hence `to_string`, `from_string`, and
/// the JSON adapters. Lookup is a linear scan over seven rows with static
/// storage, so no allocation occurs.
///
/// @note Spec `hub/streaming`, requirement "Streaming types follow the
///       serialization contract".
template <> struct EnumTraits<nexigen::hub::StreamKind> {
    /// @brief One enumerator/name pair of the vocabulary table.
    struct Entry {
        nexigen::hub::StreamKind value; ///< Enumerator being named.
        std::string_view name;          ///< Canonical lowercase spelling.
    };
    /// @brief Canonical enumerator table, in declaration order.
    static constexpr std::array<Entry, 7> entries{{
        {nexigen::hub::StreamKind::App, "app"},
        {nexigen::hub::StreamKind::Metrics, "metrics"},
        {nexigen::hub::StreamKind::Traces, "traces"},
        {nexigen::hub::StreamKind::Chrono, "chrono"},
        {nexigen::hub::StreamKind::Stdout, "stdout"},
        {nexigen::hub::StreamKind::Stderr, "stderr"},
        {nexigen::hub::StreamKind::ExecutionManager, "execution_manager"},
    }};
    static constexpr std::string_view fallback = "unknown"; ///< Rendering for unknown values.
};

} // namespace nexigen::core

namespace nexigen::hub {

/// @brief Render the canonical lowercase name of a stream kind.
///
/// @param kind Enumerator to render.
/// @return Static-storage view; an out-of-table value renders `"unknown"`.
///         Never allocates or throws.
/// @note Spec `hub/streaming`, requirement "Streaming types follow the
///       serialization contract".
/// @note Diagnostic string only (project mandate 9).
[[nodiscard]] constexpr std::string_view to_string(StreamKind kind) noexcept {
    return core::enum_name(kind);
}

/// @brief Parse a canonical lowercase stream-kind name.
///
/// @param name View to parse; matching is exact and case-sensitive.
/// @param out Destination, assigned only on success.
/// @return `true` when `name` is a canonical spelling, `false` otherwise.
/// @note Spec `hub/streaming`, requirement "Streaming types follow the
///       serialization contract".
[[nodiscard]] constexpr bool from_string(std::string_view name, StreamKind& out) noexcept {
    return core::enum_value(name, out);
}

/// @brief ADL JSON writer: the value is exactly `to_string(kind)`.
///
/// @param j Output JSON value replaced with the lowercase name string.
/// @param value Enumerator to serialize.
/// @note Spec `hub/streaming`, requirement "Streaming types follow the
///       serialization contract".
/// @note Cold-path JSON only (project mandate 10).
void to_json(nlohmann::json& j, StreamKind value);

/// @brief ADL JSON reader, strictly non-throwing under `JSON_NOEXCEPTION=1`.
///
/// A non-string or unrecognized value leaves `value` unchanged.
///
/// @param j JSON value to read.
/// @param value Destination, unchanged on any failure.
/// @note Spec `hub/streaming`, requirement "Streaming types follow the
///       serialization contract".
/// @note Cold-path JSON only (project mandate 10); safe under
///       `JSON_NOEXCEPTION=1`.
void from_json(const nlohmann::json& j, StreamKind& value) noexcept;

} // namespace nexigen::hub
