// nexigen::chrono — process-wide read-only view of the published time base
// (change add-time-base-service; spec logging/file-logger "Nanosecond UTC
// timestamps via calibrated clocks", metrics/metric-sinks "JSON-lines file
// sink", tracing/trace-sinks "JsonFileTraceSink JSONL output"; design D-J).
//
// Telemetry renderers (log, metric and trace sinks) do not own a time-base
// client: the application's `Infrastructure` attaches the authoritative client
// and binds the same segment here, so every renderer derives UTC from the
// published calibration instead of sampling `CLOCK_REALTIME`.
//
// HOT PATH: one acquire load of the binding, one relaxed generation load per
// thread, and a 4224-byte copy only when the generation moved. The cached
// block is per thread, so concurrent writer threads never share mutable state.
#pragma once

#include <cstdint>
#include <optional>
#include <string_view>

#include <nexigen/core/result.hpp>

namespace nexigen::chrono {

/// @brief Process-wide UTC calibration source backed by the published block.
///
/// `attach()` maps the segment read-only and caches one complete block per
/// calling thread; `utc_from_canonical()` returns `std::nullopt` whenever the
/// block carries no calibration covering the requested instant. A renderer can
/// therefore keep `mono_ns` authoritative and omit (never fabricate) UTC.
///
/// @note Spec `chrono/time-base`, requirement "Shared-memory publication
///       contract" and "Client attach and required domains".
class TimeBaseClock {
public:
    /// @brief Bind the process to a published time-base segment.
    ///
    /// Idempotent: an already bound process keeps its current binding.
    ///
    /// @param segment_name POSIX shared-memory name of the segment.
    /// @return OK when bound (or already bound), `NotFound` when no segment
    ///         exists, `CorruptData` for an incompatible block, or
    ///         `SystemError` when mapping fails.
    /// @note Cold path only (application start).
    [[nodiscard]] static core::Status attach(std::string_view segment_name) noexcept;

    /// @brief Drop the binding; renderers fall back to their legacy path.
    ///
    /// @note Cold path only (shutdown/rollback). The mapped state stays alive
    ///       so a thread-local cache never dangles; it is not reused after a
    ///       new attach.
    static void detach() noexcept;

    /// @brief Whether a published base is bound.
    ///
    /// @return `true` after a successful `attach()` and before `detach()`.
    [[nodiscard]] static bool attached() noexcept;

    /// @brief Convert a canonical instant to UTC through the published series.
    ///
    /// @param canonical_ns Canonical `CLOCK_MONOTONIC_RAW` nanoseconds.
    /// @return UTC nanoseconds, or `std::nullopt` when unbound, uncalibrated,
    ///         or the instant lies beyond the published validity horizon.
    /// @note Hot path safe: no allocation, no syscall.
    [[nodiscard]] static std::optional<std::int64_t>
    utc_from_canonical(std::int64_t canonical_ns) noexcept;

    /// @brief The boot token published in the bound block.
    ///
    /// @return The token, or `std::nullopt` when unbound.
    /// @note Hot path safe.
    [[nodiscard]] static std::optional<std::uint64_t> boot_token() noexcept;

    /// @brief The POSIX name of the bound segment.
    ///
    /// @return Borrowed view, or an empty view when unbound.
    /// @note Cold path only.
    [[nodiscard]] static std::string_view segment_name() noexcept;
};

} // namespace nexigen::chrono
