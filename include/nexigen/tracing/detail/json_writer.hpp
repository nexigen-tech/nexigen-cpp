// nexigen::tracing — bounds-checked JSON writer (design D2/D11).
//
// FixedWriter is the shared core implementation
// (<nexigen/core/fixed_writer.hpp>, change
// fix-p2-observability-and-writers); this header re-exports it under
// tracing::detail and owns the module's scratch-size contract:
//
// Scratch size for one JSON line: two ISO-8601 stamps (30 each), ids and
// names with worst-case escaping, the location trio, and four attributes —
// 8 KiB is a generous static bound (design D11). A skipped line is a counted
// loss ("Scratch overflow discards the line and counts it").
#pragma once

#include <cstddef>
#include <cstdint>
#include <string_view>

#include <nexigen/core/fixed_writer.hpp>

namespace nexigen::tracing::detail {

/// @brief Shared bounds-checked JSON appender, re-exported for tracing.
///
/// Stages a JSON line into caller-owned fixed storage; every append is
/// bounds-checked and a rejected append marks overflow permanently, after
/// which the caller skips the record or batch. No reallocation, no exceptions,
/// no allocation.
///
/// @note Spec `tracing/record-format`, requirement "Serialization helpers do
///       not change hot-path contracts"; design D2/D11. Project mandate 10
///       (non-throwing JSON under `JSON_NOEXCEPTION=1`) and the strict-JSON-line
///       escaping policy of `nexigen::core::FixedWriter`.
using nexigen::core::FixedWriter;

/// @brief Scratch size for one JSON span line — 8 KiB (design D11).
///
/// The bound covers two ISO-8601 stamps (30 bytes each), ids and names with
/// worst-case escaping, the location trio, and four attributes. A line that
/// overflows is discarded and counted as a loss, never truncated in place.
///
/// @note Spec `tracing/trace-sinks`, requirement "JsonFileTraceSink JSONL
///       output" (scenario "Scratch overflow discards the line and counts
///       it"); design D11.
inline constexpr std::size_t json_scratch_capacity = 8192;

} // namespace nexigen::tracing::detail
