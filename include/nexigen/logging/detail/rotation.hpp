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
#include <format>
#include <string>
#include <string_view>

#include <nlohmann/json.hpp>

#include <nexigen/core/result.hpp>

namespace nexigen::logging::detail {

/// Schedule selector (mirrors the public FileLogger config vocabulary).
enum class Rotation_kind : std::uint8_t { None, Hourly, Daily, Custom };

/// Compile-time lowercase name — the spelling used by from_string/JSON
/// (mandates 9–11; public alias: nexigen::logging::Rotation_schedule).
[[nodiscard]] constexpr std::string_view to_string(Rotation_kind kind) noexcept {
    switch (kind) {
        case Rotation_kind::None:   return "none";
        case Rotation_kind::Hourly: return "hourly";
        case Rotation_kind::Daily:  return "daily";
        case Rotation_kind::Custom: return "custom";
        default:                    return "Unknown";
    }
}

/// Parse a canonical lowercase schedule name; false leaves `out` unchanged.
[[nodiscard]] constexpr bool from_string(std::string_view name, Rotation_kind& out) noexcept {
    if (name == "none")   { out = Rotation_kind::None; return true; }
    if (name == "hourly") { out = Rotation_kind::Hourly; return true; }
    if (name == "daily")  { out = Rotation_kind::Daily; return true; }
    if (name == "custom") { out = Rotation_kind::Custom; return true; }
    return false;
}

/// ADL JSON writer (cold path only): the JSON value is exactly to_string(kind).
inline void to_json(nlohmann::json& j, const Rotation_kind kind) {
    j = std::string(to_string(kind));
}

/// ADL JSON reader, strictly non-throwing under JSON_NOEXCEPTION=1.
inline void from_json(const nlohmann::json& j, Rotation_kind& out) noexcept {
    const auto* const name = j.get_ptr<const nlohmann::json::string_t*>();
    if (name != nullptr) {
        static_cast<void>(from_string(std::string_view(*name), out));
    }
}

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

/// Same as open_period, but selects a numbered size-based segment inside the
/// period directory: segment 0 keeps `file_name`; segment N inserts `.<N>`
/// before the last extension (`app.jsonl` -> `app.1.jsonl`) or appends it when
/// the name has no usable extension. A rendered name that would overflow the
/// pre-allocated scratch maps to InvalidArgument (never truncation).
[[nodiscard]] core::Status open_segment(Rotation_kind kind, std::uint64_t custom_ns,
                                        std::int64_t utc_ns, std::string_view base_dir,
                                        std::string_view file_name, std::uint64_t segment,
                                        Rotation_scratch& scratch, int& fd_out) noexcept;

} // namespace nexigen::logging::detail

/// Rotation_schedule formats as its canonical lowercase name (mandate 11):
/// parse/fill/align via the string_view formatter; templated context for
/// libstdc++ 15's substituted-formattability evaluation.
template <>
struct std::formatter<nexigen::logging::detail::Rotation_kind>
    : std::formatter<std::string_view> {
    template <class Ctx>
    constexpr auto format(const nexigen::logging::detail::Rotation_kind kind, Ctx& ctx) const {
        return std::formatter<std::string_view>::format(
            nexigen::logging::detail::to_string(kind), ctx);
    }
};
