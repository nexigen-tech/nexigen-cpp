// nexigen::metrics — one collection window of harvested records (design D12;
// spec metrics/metric-provider).
#pragma once

#include <cstdint>
#include <format>
#include <string>
#include <string_view>
#include <type_traits>

#include <nlohmann/json.hpp>

#include <nexigen/chrono/timestamp.hpp>
#include <nexigen/metrics/metric_record.hpp>

namespace nexigen::metrics {

/// @brief One collection window: the stamp plus the harvested records.
///
/// The record buffer is owned by the provider (a pre-allocated, cache-line
/// aligned pool); the batch only points at it. A batch is valid only while that
/// buffer is alive and not reused, so sinks must finish with it before
/// returning. `from_json` fills a caller-provided buffer, bounded by
/// `record_capacity`.
///
/// @note Spec `metrics/metric-provider`, requirement "Background harvesting and
///       collection period".
/// @note Spec `metrics/snapshot-format`, requirement "MetricBatchSnapshot
///       deserialization is array-length safe".
struct MetricBatchSnapshot {
    chrono::Timestamp stamp{};        ///< Window start stamp (monotonic clock).
    std::int64_t utc_ns{0};           ///< Same window in UTC nanoseconds.
    MetricRecord* records{nullptr};   ///< Borrowed record buffer; may be null.
    std::uint32_t record_count{0};    ///< Records written into `records`.
    std::uint32_t record_capacity{0}; ///< Capacity of the borrowed buffer.

    /// @brief Render the batch summary as a cold-path diagnostic string.
    ///
    /// @return Human-readable text naming the stamp and record count.
    /// @note Spec `metrics/policy-enums`, requirement "Public type diagnostic
    ///       strings"; diagnostic only (project mandate 9).
    [[nodiscard]] std::string to_string() const;
};

static_assert(std::is_trivially_copyable_v<MetricBatchSnapshot>);

/// @brief Free diagnostic helper mirroring `MetricBatchSnapshot::to_string()`.
///
/// @param batch Batch to render.
/// @return The same text the member returns.
/// @note Spec `metrics/policy-enums`, requirement "Public type diagnostic
///       strings"; diagnostic only (project mandate 9).
[[nodiscard]] std::string to_string(const MetricBatchSnapshot& batch);

/// @brief Write a batch as JSON (cold path).
///
/// @param j Output JSON object with the window stamps and the `records` array.
/// @param batch Batch to serialize.
/// @note Spec `metrics/policy-enums`, requirement "Public type JSON
///       serialization"; JSON only (project mandate 10).
void to_json(nlohmann::json& j, const MetricBatchSnapshot& batch);
/// @brief Parse a batch into a caller-provided buffer without throwing.
///
/// Sets `stamp`, `utc_ns`, and `record_count` only from valid numbers. The
/// `records` array is walked only when `out.records` is non-null and stops at
/// `record_capacity`; the actual parsed count replaces a larger claimed count.
///
/// @param j JSON object to read.
/// @param out Destination; its `records` pointer and capacity are inputs.
/// @note Spec `metrics/snapshot-format`, requirement "MetricBatchSnapshot
///       deserialization is array-length safe".
/// @note Cold-path JSON only (project mandate 10); safe under
///       `JSON_NOEXCEPTION=1`.
void from_json(const nlohmann::json& j, MetricBatchSnapshot& out) noexcept;

} // namespace nexigen::metrics

/// @brief `std::formatter` for `MetricBatchSnapshot`, rendering its diagnostics.
///
/// @note Project mandate 11; formatting is cold path only.
template <>
struct std::formatter<nexigen::metrics::MetricBatchSnapshot> : std::formatter<std::string_view> {
    /// @brief Format the batch through its `to_string()` text.
    ///
    /// @tparam Ctx Format context type supplied by `std::format`.
    /// @param value Batch to render.
    /// @param ctx Output context receiving the text.
    /// @return Iterator past the last written character.
    template <class Ctx>
    auto format(const nexigen::metrics::MetricBatchSnapshot& value, Ctx& ctx) const {
        const std::string text = value.to_string();
        return std::formatter<std::string_view>::format(text, ctx);
    }
};
