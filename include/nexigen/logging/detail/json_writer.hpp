// nexigen::logging — strict JSON line formatter (design.md D12, spec
// logging/file-logger "Strict JSON record schema").
//
// FixedWriter is the shared core implementation
// (<nexigen/core/fixed_writer.hpp>, change
// fix-p2-observability-and-writers); this header re-exports it under
// logging::detail and owns the module's scratch-size contract:
//
// format_json_line emits the EXACT schema and field order:
//   {"timestamp":"YYYY-MM-DDTHH:MM:SS.nnnnnnnnnZ","level":"<lowercase>",
//    "app":"<escaped>","thread_id":<int>,
//    "message":"[<basename>:<line>] (<function>) <user message>"}\n
//
// Escaping: '"' and '\\' two-char form; '\b' '\t' '\n' '\f' '\r' short forms;
// other bytes < 0x20 as \u00XX; everything else passes through as raw bytes —
// so UTF-8 (Cyrillic included) round-trips verbatim inside valid JSON.
//
// The function name is written exactly as std::source_location reports it
// (GCC's readable __PRETTY_FUNCTION__-style text, no libiberty demangling
// here — design Open Questions).
#pragma once

#include <cstddef>
#include <cstdint>
#include <string_view>

#include <nexigen/core/fixed_writer.hpp>
#include <nexigen/core/log_level.hpp>

namespace nexigen::logging::detail {

/// @brief Shared bounds-checked appender re-exported for logging JSON lines.
///
/// Stages bytes into caller-owned fixed storage; the first overflow marks the
/// writer failed permanently so the caller skips the whole record. Re-exporting
/// this single core type keeps the module free of a private escaping copy.
///
/// @note Spec `logging/file-logger`, requirement "Strict JSON record schema";
///       the escaping policy is inherited from the shared core writer.
using nexigen::core::FixedWriter;

/// @brief Scratch capacity for one fully escaped JSON line, in bytes.
///
/// Budget: timestamp (29) + decoration (~70) + app (≤64) + thread id (≤10) +
/// function name (bounded by escaping) + message (≤128 × 6 worst-case escape
/// expansion); 4 KiB is a generous static bound.
///
/// @note Spec `logging/file-logger`, requirement "Asynchronous wait-free
///       JSON-lines file output": the buffer is pre-allocated, so line
///       rendering never allocates.
inline constexpr std::size_t json_scratch_capacity = 4096;

/// @brief Format one record as a complete JSON line in `scratch`.
///
/// Emits the exact schema and field order
/// `timestamp, level, app, thread_id, message`; the message is decorated as
/// `"[<basename>:<line>] (<function>) <user message>"`. Escaping uses the
/// shared `FixedWriter` policy, so UTF-8 passes through verbatim.
///
/// @param utc_ns UTC timestamp in nanoseconds since the Unix epoch.
/// @param level Severity whose lowercase name is emitted.
/// @param app Configured application name, escaped.
/// @param thread_id Producing thread id, emitted as a JSON number.
/// @param file_name Source path; only its basename is emitted.
/// @param line Source line number.
/// @param function_name Function text exactly as `std::source_location`
///        reports it (no demangling).
/// @param message Rendered user message, escaped.
/// @param scratch Caller-owned buffer of exactly `json_scratch_capacity` bytes.
/// @return Line length in bytes including the trailing `'\n'`, or 0 when the
///         line would not fit; the caller then skips the record.
/// @note Spec `logging/file-logger`, requirement "Strict JSON record schema"
///       and requirement "Nanosecond UTC timestamps via calibrated clocks".
///       Mandate 10 (JSON serialization): rendering is allocation-free and
///       never emits a half line.
[[nodiscard]] std::size_t format_json_line(std::int64_t utc_ns, core::LogLevel level,
                                           std::string_view app, std::uint32_t thread_id,
                                           std::string_view file_name, std::uint32_t line,
                                           std::string_view function_name, std::string_view message,
                                           char (&scratch)[json_scratch_capacity]) noexcept;

} // namespace nexigen::logging::detail
