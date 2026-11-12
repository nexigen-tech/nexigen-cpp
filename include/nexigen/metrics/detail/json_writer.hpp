// nexigen::metrics — bounds-checked JSON writer (design D13).
//
// FixedWriter is the shared core implementation
// (<nexigen/core/fixed_writer.hpp>, change
// fix-p2-observability-and-writers); this header re-exports it under
// metrics::detail and owns the module's scratch-size contract:
// metrics batches (all counters/gauges/histograms of one harvest window)
// can carry many records, so the metric sink budget is 16 KiB — a skipped
// batch is a counted loss ("Scratch overflow discards the batch and counts
// it").
//
// FixedWriter stages into caller-owned fixed storage: every append is
// bounds-checked, a rejected append marks overflow and the caller skips the
// record. No reallocation, no exceptions, no allocation.
//
// Escaping mirrors the shared policy: '"' and '\\' in two-char form;
// '\b' '\t' '\n' '\f' '\r' in short form; other bytes < 0x20 as \u00XX;
// everything else passes through as raw bytes (UTF-8 round-trips verbatim
// inside valid JSON).
#pragma once

#include <cstddef>
#include <cstdint>
#include <string_view>

#include <nexigen/core/fixed_writer.hpp>

namespace nexigen::metrics::detail {

/// @brief Shared bounds-checked JSON appender used by the metrics sinks.
///
/// Stages into caller-owned fixed storage: every append is bounds-checked, a
/// rejected append marks the writer failed permanently, and the caller drops the
/// record or batch. The escaping policy matches the project-wide JSON writer
/// (`"` and `\` in two-char form, short escapes for control characters, other
/// bytes below `0x20` as `\u00XX`, UTF-8 passed through verbatim). No
/// reallocation, no exceptions, no allocation.
///
/// @note Spec `metrics/metric-sinks`, scenario "Sink callbacks are
///       allocation-free"; project mandate 10 (JSON).
using nexigen::core::FixedWriter;

/// @brief Scratch size for one metric batch line — 16 KiB (design D13).
///
/// A batch can carry every counter, gauge and histogram of one harvest window,
/// so the metrics budget is larger than the logging/tracing ones. A rendering
/// that does not fit is a counted, whole-batch loss.
///
/// @note Spec `metrics/metric-sinks`, scenario "Scratch overflow discards the
///       batch and counts it".
inline constexpr std::size_t json_scratch_capacity = 16384;

} // namespace nexigen::metrics::detail
