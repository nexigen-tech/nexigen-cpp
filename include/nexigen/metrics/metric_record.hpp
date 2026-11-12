// nexigen::metrics — one harvested metric (design D12; spec
// metrics/metric-provider "Record and batch snapshot serialization").
#pragma once

#include <format>
#include <string>
#include <string_view>
#include <type_traits>

#include <nlohmann/json.hpp>

#include <nexigen/metrics/histogram_snapshot.hpp>
#include <nexigen/metrics/metric_identity.hpp>
#include <nexigen/metrics/policy_enums.hpp>
#include <nexigen/metrics/scalar_value.hpp>
#include <nexigen/metrics/state_value.hpp>
#include <nexigen/metrics/statistics_snapshot.hpp>

namespace nexigen::metrics {

/// @brief One harvested metric: identity, classification, and payload.
///
/// The kind-specific payload is a union sized to the largest variant; `kind`
/// selects the active member, and the policy fields record the compile-time
/// template arguments the instrument was created with. The whole record is
/// trivially copyable and fixed-size so it can be embedded in a `MetricFrame`.
///
/// @note Spec `metrics/metric-provider`, requirement "Background harvesting
///       and collection period" (each record carries the full identity).
/// @note Spec `metrics/snapshot-format`, requirement "MetricRecord
///       deserialization selects the union member by kind".
struct MetricRecord {
    MetricIdentity identity{};            ///< Owning meter, metric name, and unit.
    MetricKind kind{MetricKind::Counter}; ///< Instrument family / active payload.
    ConcurrencyPolicy concurrency{ConcurrencyPolicy::SingleThreaded}; ///< Writer policy.
    AccessPolicy access{AccessPolicy::Readable};                      ///< Read surface.
    ResetPolicy reset{ResetPolicy::Cumulative};                       ///< Window policy.
    /// @brief Kind-specific payload; only the member selected by `kind` is valid.
    union {
        ScalarValue scalar{};          ///< Active for `Counter` and `Gauge`.
        StatisticsSnapshot statistics; ///< Active for `DescriptiveStatistics`.
        HistogramSnapshot histogram;   ///< Active for both histogram kinds.
        StateValue state;              ///< Active for `State`.
    };

    /// @brief Render the record as a cold-path diagnostic string.
    ///
    /// @return Human-readable text carrying the identity and active payload.
    /// @note Spec `metrics/policy-enums`, requirement "Public type diagnostic
    ///       strings"; diagnostic only (project mandate 9).
    [[nodiscard]] std::string to_string() const;
};

static_assert(std::is_trivially_copyable_v<MetricRecord>);

/// @brief Free diagnostic helper mirroring `MetricRecord::to_string()`.
///
/// @param record Record to render.
/// @return The same text the member returns.
/// @note Spec `metrics/policy-enums`, requirement "Public type diagnostic
///       strings"; diagnostic only (project mandate 9).
[[nodiscard]] std::string to_string(const MetricRecord& record);

/// @brief Write a record as JSON (cold path).
///
/// @param j Output JSON object with identity, the four policies, and the
///        payload selected by `kind`.
/// @param record Record to serialize.
/// @note Spec `metrics/snapshot-format`, requirement "MetricRecord
///       deserialization selects the union member by kind"; JSON only (project
///       mandate 10).
void to_json(nlohmann::json& j, const MetricRecord& record);
/// @brief Parse a record from JSON without throwing.
///
/// Reads `identity` and the four policy enums first, then exactly one payload
/// object according to `kind`: `scalar`, `statistics`, `state`, or `histogram`.
/// An unknown kind leaves the payload untouched.
///
/// @param j JSON object to read.
/// @param out Destination, fields updated independently.
/// @note Spec `metrics/snapshot-format`, requirement "MetricRecord
///       deserialization selects the union member by kind".
/// @note Cold-path JSON only (project mandate 10); safe under
///       `JSON_NOEXCEPTION=1`.
void from_json(const nlohmann::json& j, MetricRecord& out) noexcept;

} // namespace nexigen::metrics

/// @brief `std::formatter` for `MetricRecord`, rendering its diagnostic text.
///
/// @note Project mandate 11; formatting is cold path only.
template <>
struct std::formatter<nexigen::metrics::MetricRecord> : std::formatter<std::string_view> {
    /// @brief Format the record through its `to_string()` text.
    ///
    /// @tparam Ctx Format context type supplied by `std::format`.
    /// @param value Record to render.
    /// @param ctx Output context receiving the text.
    /// @return Iterator past the last written character.
    template <class Ctx> auto format(const nexigen::metrics::MetricRecord& value, Ctx& ctx) const {
        const std::string text = value.to_string();
        return std::formatter<std::string_view>::format(text, ctx);
    }
};
