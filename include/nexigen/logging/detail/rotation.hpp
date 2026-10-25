// nexigen::logging — scheduled directory rotation (design.md D11, spec
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

namespace nexigen::logging::detail {

/// Schedule selector (mirrors the public FileLogger config vocabulary).
enum class Rotation_kind : std::uint8_t { None, Hourly, Daily, Custom };

/// Period identity for an UTC timestamp (monotonic in utc_ns).
[[nodiscard]] std::int64_t period_key(Rotation_kind kind, std::uint64_t custom_ns,
                                      std::int64_t utc_ns) noexcept;

/// Render the human-readable period label into buf (NUL-terminated).
/// Returns the label length, or 0 when buf is too small (never partial).
std::size_t period_label(Rotation_kind kind, std::uint64_t custom_ns,
                         std::int64_t utc_ns, char (&buf)[64]) noexcept;

/// Caller-owned path scratch: two fixed buffers, reused every rotation.
/// Both sized 560: base(≤511) + '/' + label(≤"epoch-"+20) fits; file adds
/// '/' + file_name(≤255) checked by put()'s bounds logic at every append.
struct Rotation_scratch {
    char dir[560]{};
    char file[560]{};
};

/// Create (if needed) and open the descriptor for the period containing
/// utc_ns:  dir  = "<base_dir>/<label>",  file = "<dir>/<file_name>".
/// mkdirat failures other than EEXIST and openat failures map to SystemError.
/// kind == None: dir = base_dir directly (single fixed directory).
/// The PREVIOUS descriptor is NOT touched here — the caller closes it after
/// this open succeeds, so a failed rotation leaves the old file live.
[[nodiscard]] core::Status open_period(Rotation_kind kind, std::uint64_t custom_ns,
                                       std::int64_t utc_ns, std::string_view base_dir,
                                       std::string_view file_name,
                                       Rotation_scratch& scratch, int& fd_out) noexcept;

} // namespace nexigen::logging::detail
