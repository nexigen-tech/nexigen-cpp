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

using nexigen::core::FixedWriter; ///< shared bounds-checked appender

/// Scratch size for one JSON span line — 8 KiB (design D11).
inline constexpr std::size_t json_scratch_capacity = 8192;

} // namespace nexigen::tracing::detail
