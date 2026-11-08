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

/// Period identity for an UTC timestamp (monotonic in utc_ns).
[[nodiscard]] std::int64_t period_key(RotationSchedule kind, std::uint64_t custom_ns,
                                      std::int64_t utc_ns) noexcept;

/// Render the human-readable period label into buf (NUL-terminated).
/// Returns the label length, or 0 when buf is too small (never partial).
std::size_t period_label(RotationSchedule kind, std::uint64_t custom_ns, std::int64_t utc_ns,
                         char (&buf)[64]) noexcept;

/// Create (if needed) and open the descriptor for the period containing
/// utc_ns:  dir  = "<base_dir>/<label>",  file = "<dir>/<file_name>".
/// mkdirat failures other than EEXIST and openat failures map to SystemError.
/// kind == None: dir = base_dir directly (single fixed directory).
/// The PREVIOUS descriptor is NOT touched here — the caller closes it after
/// this open succeeds, so a failed rotation leaves the old file live.
[[nodiscard]] core::Status open_period(RotationSchedule kind, std::uint64_t custom_ns,
                                       std::int64_t utc_ns, std::string_view base_dir,
                                       std::string_view file_name, RotationScratch& scratch,
                                       int& fd_out) noexcept;

/// Same as open_period, but selects a numbered size-based segment inside the
/// period directory: segment 0 keeps `file_name`; segment N inserts `.<N>`
/// before the last extension (`app.jsonl` -> `app.1.jsonl`) or appends it when
/// the name has no usable extension. A rendered name that would overflow the
/// pre-allocated scratch maps to InvalidArgument (never truncation).
[[nodiscard]] core::Status open_segment(RotationSchedule kind, std::uint64_t custom_ns,
                                        std::int64_t utc_ns, std::string_view base_dir,
                                        std::string_view file_name, std::uint64_t segment,
                                        RotationScratch& scratch, int& fd_out) noexcept;

} // namespace nexigen::logging::detail
