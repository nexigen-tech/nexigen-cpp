// nexigen::metrics — Prometheus text exposition renderer (change
// add-metrics-daemon; spec metrics/metrics-daemon "Daemon renders Prometheus
// text exposition" / "Prometheus labels and metadata"; design D6).
//
// Cold-path, allocation-free rendering of one MetricFrame into the Prometheus
// text exposition format. The client already computed every value (mean,
// stddev, percentiles, per-bucket counts); this unit only formats them.
#pragma once

#include <cstddef>

#include <nexigen/metrics/metric_frame.hpp>

namespace nexigen::metrics::detail {

/// @brief Render one frame's exposition lines into `out[used..cap)`.
///
/// Appends the `# TYPE`/`# UNIT` comments and every sample line for the record,
/// including the `app`, `meter`, `version` and `pid` labels. The client already
/// computed every value (mean, stddev, percentiles, per-bucket counts); this
/// unit only formats them as text.
///
/// @param frame Aggregated frame to render.
/// @param out Destination buffer.
/// @param used Current write offset; also the fallback return value.
/// @param cap Total destination capacity in bytes.
/// @return The new length on success, or `used` unchanged when the lines do not
///         fit, so the caller may skip the record. Never allocates, never
///         throws.
/// @note Spec `metrics/metrics-daemon`, requirement "Daemon renders Prometheus
///       text exposition" and requirement "Prometheus labels and metadata".
[[nodiscard]] std::size_t render_prometheus(const MetricFrame& frame, char* out, std::size_t used,
                                            std::size_t cap) noexcept;

} // namespace nexigen::metrics::detail
