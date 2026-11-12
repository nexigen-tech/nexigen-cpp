// nexigen::metrics — bucket table plus summary and percentiles of a histogram
// instrument (design D12; spec metrics/metric-provider).
#pragma once

#include <cstdint>
#include <format>
#include <string>
#include <string_view>
#include <type_traits>

#include <nlohmann/json.hpp>

#include <nexigen/metrics/metric_identity.hpp>

namespace nexigen::metrics {

/// @brief Bucket table plus summary and percentiles of a histogram instrument.
///
/// Serves both histogram kinds: explicit histograms fill the bucket arrays with
/// their configured bounds, exponential histograms with their coarsened bucket
/// table. `effective_scale` reports the scale actually in force, which may be
/// coarser than configured. The arrays are inline, so the snapshot stays
/// trivially copyable and fixed-size.
///
/// @note Spec `metrics/explicit-histogram`, requirement "Explicit histogram
///       percentile snapshots".
/// @note Spec `metrics/exponential-histogram`, requirement "Bounded bucket
///       storage and coarsening".
struct HistogramSnapshot {
    std::uint64_t count{0};           ///< Accepted observations in the window.
    double sum{0.0};                  ///< Sum of accepted observations.
    double min{0.0};                  ///< Smallest accepted observation.
    double max{0.0};                  ///< Largest accepted observation.
    std::uint64_t ignored_samples{0}; ///< Rejected (non-finite / negative) inputs.
    std::uint64_t zero_count{0};      ///< Zero samples (rank below every bucket).
    std::int32_t effective_scale{0};  ///< Scale in force (may be coarser than configured).
    double p50{0.0};                  ///< Median estimate from the bucket table.
    double p90{0.0};                  ///< 90th percentile estimate.
    double p99{0.0};                  ///< 99th percentile estimate.
    double p999{0.0};                 ///< 99.9th percentile estimate.
    std::uint32_t bucket_count{0};    ///< Number of valid entries in the arrays.
    double buckets_lower[max_snapshot_buckets]{};        ///< Exclusive lower bound per bucket.
    std::uint64_t buckets_count[max_snapshot_buckets]{}; ///< Observation count per bucket.

    /// @brief Render the histogram summary as a cold-path diagnostic string.
    ///
    /// @return Human-readable text carrying the summary, percentiles, and
    ///         bucket table.
    /// @note Spec `metrics/policy-enums`, requirement "Public type diagnostic
    ///       strings"; diagnostic only (project mandate 9).
    [[nodiscard]] std::string to_string() const;
};

static_assert(std::is_trivially_copyable_v<HistogramSnapshot>);

/// @brief Free diagnostic helper mirroring `HistogramSnapshot::to_string()`.
///
/// @param snapshot Summary to render.
/// @return The same text the member returns.
/// @note Spec `metrics/policy-enums`, requirement "Public type diagnostic
///       strings"; diagnostic only (project mandate 9).
[[nodiscard]] std::string to_string(const HistogramSnapshot& snapshot);

/// @brief Write a histogram snapshot as JSON (cold path).
///
/// @param j Output JSON object with the summary, percentiles, and bucket
///        arrays; the `p999` field is written as `p99.9`.
/// @param snapshot Summary to serialize.
/// @note Spec `metrics/explicit-histogram`, requirement "Explicit histogram
///       serialization contract"; JSON only (project mandate 10).
void to_json(nlohmann::json& j, const HistogramSnapshot& snapshot);
/// @brief Parse a histogram snapshot from JSON without throwing.
///
/// Reads numeric fields through typed guards, maps the JSON field `p99.9` to
/// `p999`, and caps bucket arrays at `bucket_count` and `max_snapshot_buckets`.
///
/// @param j JSON object to read.
/// @param out Destination, fields updated independently.
/// @note Spec `metrics/snapshot-format`, requirement "Statistics and histogram
///       snapshots use typed numeric readers".
/// @note Cold-path JSON only (project mandate 10); safe under
///       `JSON_NOEXCEPTION=1`.
void from_json(const nlohmann::json& j, HistogramSnapshot& out) noexcept;

} // namespace nexigen::metrics

/// @brief `std::formatter` for `HistogramSnapshot`, rendering its diagnostics.
///
/// @note Project mandate 11; formatting is cold path only.
template <>
struct std::formatter<nexigen::metrics::HistogramSnapshot> : std::formatter<std::string_view> {
    /// @brief Format the snapshot through its `to_string()` text.
    ///
    /// @tparam Ctx Format context type supplied by `std::format`.
    /// @param value Summary to render.
    /// @param ctx Output context receiving the text.
    /// @return Iterator past the last written character.
    template <class Ctx>
    auto format(const nexigen::metrics::HistogramSnapshot& value, Ctx& ctx) const {
        const std::string text = value.to_string();
        return std::formatter<std::string_view>::format(text, ctx);
    }
};
