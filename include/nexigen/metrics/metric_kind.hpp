// nexigen::metrics — MetricKind enum.
#pragma once

#include <array>
#include <cstdint>
#include <string>
#include <string_view>

#include <nlohmann/json.hpp>

#include <nexigen/core/enum_traits.hpp>
namespace nexigen::metrics {

/// @brief Instrument family held by one registry slot and reported by a record.
///
/// Numeric values are append-only and stable, and the byte-sized underlying
/// type lets the value travel inside a fixed cross-process `MetricFrame`. The
/// enumerator spelling is the canonical diagnostic and JSON form; the mapping
/// lives in the `core::EnumTraits` table below. Classification only: reading
/// this value is allocation-free.
///
/// @note Spec `metrics/policy-enums`, requirement "Policy enum catalogue".
enum class MetricKind : std::uint8_t {
    Counter,               ///< Monotonic counter instrument.
    Gauge,                 ///< Up/down gauge instrument.
    DescriptiveStatistics, ///< Online mean/stddev/min/max accumulator.
    ExplicitHistogram,     ///< Histogram with explicit bucket bounds.
    ExponentialHistogram,  ///< Histogram with exponentially spaced buckets.
    State,                 ///< Last-writer-wins named state.
};

} // namespace nexigen::metrics

// Vocabulary table for core::enum_name/enum_value (change refactor-enum-traits-helper).
namespace nexigen::core {

/// @brief Vocabulary table mapping `MetricKind` enumerators to canonical names.
///
/// Backs `core::enum_name`/`core::enum_value`, hence `to_string`,
/// `from_string`, and the JSON adapters below. The table is `constexpr`
/// static storage and lookup is a linear scan over six rows, so no allocation
/// occurs.
///
/// @note Spec `metrics/policy-enums`, requirement "Constexpr policy names".
template <> struct EnumTraits<nexigen::metrics::MetricKind> {
    /// @brief One enumerator/name pair of the vocabulary table.
    struct Entry {
        nexigen::metrics::MetricKind value; ///< Enumerator being named.
        std::string_view name;              ///< Canonical PascalCase spelling.
    };
    /// @brief Canonical enumerator table, in declaration order.
    static constexpr std::array<Entry, 6> entries{{
        {nexigen::metrics::MetricKind::Counter, "Counter"},
        {nexigen::metrics::MetricKind::Gauge, "Gauge"},
        {nexigen::metrics::MetricKind::DescriptiveStatistics, "DescriptiveStatistics"},
        {nexigen::metrics::MetricKind::ExplicitHistogram, "ExplicitHistogram"},
        {nexigen::metrics::MetricKind::ExponentialHistogram, "ExponentialHistogram"},
        {nexigen::metrics::MetricKind::State, "State"},
    }};
};

} // namespace nexigen::core

namespace nexigen::metrics {

/// @brief Render the canonical PascalCase name of a metric kind.
///
/// @param kind Enumerator to render.
/// @return Static-storage view of the canonical spelling; an out-of-table value
///         renders `"Unknown"`. Never allocates or throws.
/// @note Spec `metrics/policy-enums`, requirement "Constexpr policy names".
/// @note Diagnostic string only (project mandate 9).
[[nodiscard]] constexpr std::string_view to_string(MetricKind kind) noexcept {
    return core::enum_name(kind);
}

/// @brief Parse a canonical metric-kind spelling.
///
/// @param name View to parse; matching is exact and case-sensitive.
/// @param out Destination, assigned only on success.
/// @return `true` when `name` is a canonical spelling, `false` otherwise.
/// @note Spec `metrics/policy-enums`, requirement "Constexpr policy names".
[[nodiscard]] constexpr bool from_string(std::string_view name, MetricKind& out) noexcept {
    return core::enum_value(name, out);
}

/// @brief Parse a metric kind from a JSON string without throwing.
///
/// @param j JSON value to read; a non-string or unrecognized value is ignored.
/// @param out Destination, left at its prior value on any failure.
/// @note Spec `metrics/policy-enums`, requirement "Non-throwing policy JSON
///       serialization".
/// @note Cold-path JSON only (project mandate 10); safe under
///       `JSON_NOEXCEPTION=1`.
inline void from_json(const nlohmann::json& j, MetricKind& out) noexcept {
    core::mandate::enum_from_json(j, out);
}

/// @brief Write a metric kind as its canonical JSON string.
///
/// @param j Output JSON value, replaced with the name string.
/// @param metricKind Enumerator to serialize.
/// @note Spec `metrics/policy-enums`, requirement "Non-throwing policy JSON
///       serialization".
/// @note Cold-path JSON only (project mandate 10).
inline void to_json(nlohmann::json& j, const MetricKind metricKind) {
    core::mandate::enum_to_json(j, metricKind);
}

} // namespace nexigen::metrics
