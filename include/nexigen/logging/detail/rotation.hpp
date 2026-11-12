// nexigen::logging — scheduled directory rotation (design D11, spec
// logging/file-logger "Hitch-free scheduled directory rotation").
//
// All work here runs ON THE CONSUMER THREAD only: the producer never learns
// that rotation exists. The active period changes are detected by an integer
// key computed from each drained record's UTC time (no timer, no polling):
//   Hourly — floor(day * 24 + hour)
//   Daily  — floor(day)
//   Custom — floor(utc_ns / custom_ns)  (epoch-anchored windows)
// On a key change the consumer renders "<base_dir>/<label>/<file_name>" into
// pre-allocated buffers, mkdirat + openat the new descriptor, and closes the
// old one — fixed-size stack-free storage, zero allocations, zero hot-path
// impact by construction.
//
// Labels: Hourly "2026-07-23T23", Daily "2026-07-23", Custom "epoch-<n>".
#pragma once

#include <cstdint>
#include <string_view>

#include <nexigen/core/result.hpp>
#include <nexigen/logging/detail/rotation_scratch.hpp>
#include <nexigen/logging/rotation_schedule.hpp>

namespace nexigen::logging::detail {

/// @brief Period identity for a UTC timestamp, monotonic in `utc_ns`.
///
/// Hourly keys are `floor(day * 24 + hour)`, Daily keys `floor(day)`, Custom
/// keys `floor(utc_ns / custom_ns)`; None always yields 0. A key change is
/// exactly what the consumer treats as a rotation boundary, so the key and
/// the rendered label can never disagree.
///
/// @param kind Rotation schedule selector.
/// @param custom_ns Custom period length in nanoseconds; 0 returns 0 without
///        rotating (the config error is surfaced at initialization).
/// @param utc_ns UTC timestamp in nanoseconds since the Unix epoch.
/// @return Opaque window index, non-decreasing in `utc_ns`.
/// @note Spec `logging/file-logger`, requirement "Hitch-free scheduled
///       directory rotation"; consumer thread only, allocation-free.
[[nodiscard]] std::int64_t period_key(RotationSchedule kind, std::uint64_t custom_ns,
                                      std::int64_t utc_ns) noexcept;

/// @brief Render the human-readable period label into `buf`, NUL-terminated.
///
/// Hourly produces `"YYYY-MM-DDTHH"` (13 chars), Daily `"YYYY-MM-DD"` (10),
/// Custom `"epoch-<n>"`; None produces 0.
///
/// @param kind Rotation schedule selector.
/// @param custom_ns Custom period length in nanoseconds, used by Custom.
/// @param utc_ns UTC timestamp in nanoseconds since the Unix epoch.
/// @param buf Caller-owned 64-byte label buffer.
/// @return The label length, or 0 when `buf` is too small (never partial).
/// @note Spec `logging/file-logger`, requirement "Hitch-free scheduled
///       directory rotation"; the label becomes the period directory name.
std::size_t period_label(RotationSchedule kind, std::uint64_t custom_ns, std::int64_t utc_ns,
                         char (&buf)[64]) noexcept;

/// @brief Create (if needed) and open the file for the period containing
///        `utc_ns`.
///
/// Directory is `"<base_dir>/<label>"` and file `"<dir>/<file_name>"`; for
/// `RotationSchedule::None` the directory is `base_dir` directly (single fixed
/// directory). `mkdirat` failures other than `EEXIST` and `openat` failures map
/// to `SystemError`.
///
/// @param kind Rotation schedule selector.
/// @param custom_ns Custom period length in nanoseconds, used by Custom.
/// @param utc_ns UTC timestamp in nanoseconds since the Unix epoch.
/// @param base_dir Root log directory; ≤511 bytes, no embedded NUL.
/// @param file_name Active file name; ≤255 bytes, no embedded NUL.
/// @param scratch Caller-owned path buffers, reused in place.
/// @param fd_out Receives the opened append descriptor; set to -1 on failure.
/// @return `Status`: OK on success, `InvalidArgument` for empty/oversized/NUL
///         path inputs or a rendered path that does not fit the scratch,
///         `SystemError` for directory/open failures.
/// @warning The previous descriptor is not touched here; the caller closes it
///          only after this open succeeds, so a failed rotation leaves the
///          previous file live.
/// @note Spec `logging/file-logger`, requirement "Hitch-free scheduled
///       directory rotation": fixed scratch only, no allocation.
[[nodiscard]] core::Status open_period(RotationSchedule kind, std::uint64_t custom_ns,
                                       std::int64_t utc_ns, std::string_view base_dir,
                                       std::string_view file_name, RotationScratch& scratch,
                                       int& fd_out) noexcept;

/// @brief Open a numbered size-based segment inside the period directory.
///
/// Segment 0 keeps `file_name`; segment N inserts `.<N>` before the last
/// extension (`app.jsonl` -> `app.1.jsonl`) or appends it when the name has no
/// usable extension. The directory selection matches `open_period`.
///
/// @param kind Rotation schedule selector.
/// @param custom_ns Custom period length in nanoseconds, used by Custom.
/// @param utc_ns UTC timestamp in nanoseconds since the Unix epoch.
/// @param base_dir Root log directory; ≤511 bytes, no embedded NUL.
/// @param file_name Configured file name; ≤255 bytes, no embedded NUL.
/// @param segment Segment index; 0 is the active file, 1 the first rollover.
/// @param scratch Caller-owned path buffers, reused in place.
/// @param fd_out Receives the opened append descriptor; set to -1 on failure.
/// @return `Status`: OK on success, `InvalidArgument` when a rendered name
///         would overflow the pre-allocated scratch (never truncation) or the
///         path inputs are unusable, `SystemError` for directory/open
///         failures.
/// @note Spec `logging/file-logger`, requirement "Size-based file rotation";
///       the write itself stays on the consumer thread with no allocation.
[[nodiscard]] core::Status open_segment(RotationSchedule kind, std::uint64_t custom_ns,
                                        std::int64_t utc_ns, std::string_view base_dir,
                                        std::string_view file_name, std::uint64_t segment,
                                        RotationScratch& scratch, int& fd_out) noexcept;

} // namespace nexigen::logging::detail
