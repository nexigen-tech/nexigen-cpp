// nexigen::chrono — host-domain sampling for the time-base daemon (change
// add-time-base-service; spec chrono/time-base "Canonical monotonic scale",
// "Domain coefficient row").
//
// The daemon never assumes that the CPU cycle counter is the canonical clock.
// It publishes the canonical row from `CLOCK_MONOTONIC_RAW` and, only when the
// kernel reports that the TSC is the active clocksource and a cross-core probe
// agrees, an ordinary TSC row whose scale was measured at start.
//
// The clock access goes through a function table so tests can inject a fake
// without touching sysfs or the cycle counter. Production helpers live in the
// compiled unit; the sampling logic itself is header-inline and allocation-free.
#pragma once

#include <chrono>
#include <cstdint>

#include <nexigen/chrono/time_base_block.hpp>
#include <nexigen/chrono/time_base_status.hpp>

namespace nexigen::chrono {

/// @brief How long a host row stays nominally valid before the next publish.
inline constexpr std::uint64_t k_host_row_validity_ns = 10'000'000'000ULL;

/// @brief Function-table clock source for host-domain sampling.
///
/// `canonical_ns` returns `CLOCK_MONOTONIC_RAW` nanoseconds, or `0` on a clock
/// failure. `counter_ticks` returns the cycle counter, or `0` when unavailable.
/// `clocksource_is_tsc` reports the kernel's active clocksource verdict.
/// `tsc_synchronized` reports a cross-core probe verdict.
struct HostClockSource {
    void* self{nullptr}; ///< Opaque state for the callbacks.
    /// @brief Canonical clock read.
    std::uint64_t (*canonical_ns)(void*) noexcept {nullptr};
    /// @brief Cycle-counter read.
    std::uint64_t (*counter_ticks)(void*) noexcept {nullptr};
    /// @brief `CLOCK_MONOTONIC` read.
    std::uint64_t (*monotonic_ns)(void*) noexcept {nullptr};
    /// @brief `CLOCK_TAI` read.
    std::uint64_t (*tai_ns)(void*) noexcept {nullptr};
    /// @brief `CLOCK_REALTIME` read.
    std::uint64_t (*realtime_ns)(void*) noexcept {nullptr};
    /// @brief Kernel clocksource verdict.
    bool (*clocksource_is_tsc)(void*) noexcept {nullptr};
    /// @brief Cross-core probe verdict.
    bool (*tsc_synchronized)(void*) noexcept {nullptr};
};

/// @brief State backing the production clock source.
struct HostClockState {
    bool tsc_is_clocksource{false}; ///< Verdict read once from sysfs.
    bool tsc_synchronized{false};   ///< Cross-core probe verdict, cached at start.
};

/// @brief Build the production clock source over `state`.
///
/// The sysfs verdict is read once here (cold path); the callbacks themselves
/// only read the clocks (one `clock_gettime` and one `__rdtsc`).
///
/// @param state Caller-owned state; it must outlive the returned table.
/// @return A function table bound to `state`.
/// @note Cold path only (daemon start).
[[nodiscard]] HostClockSource production_host_clock(HostClockState& state) noexcept;

/// @brief Probe whether the cycle counter is coherent across online CPUs.
///
/// Pins the calling thread to each online CPU, takes a counter value bracketed
/// by two canonical reads on either side of it, and compares the implied
/// canonical instant against the first CPU. Each sample carries the span of its
/// bracket as its error bound, and the tightest of a bounded number of attempts
/// is used, so a scheduling delay inside a sample cannot be reported as counter
/// skew. A spread above the tolerance means the counter is not one time domain.
/// The calling thread's affinity mask is restored before returning.
///
/// @return `true` when the counter is coherent (or pinning is unavailable, in
///         which case the kernel's own verdict remains the only evidence).
/// @note Cold path only; takes a few milliseconds.
/// @note Spec `chrono/time-base`, requirement "Jitter-tolerant probe sampling".
[[nodiscard]] bool probe_tsc_synchronized() noexcept;

/// @brief Measure canonical nanoseconds per counter tick over a short window.
///
/// @param source Clock source to sample.
/// @param window Sleep between the two samples.
/// @return Q20 scale (canonical ns per tick), or `0` when a read fails or the
///         window is degenerate.
/// @note Cold path only; sleeps for `window`.
[[nodiscard]] std::uint64_t measure_counter_scale(const HostClockSource& source,
                                                  std::chrono::nanoseconds window) noexcept;

/// @brief Outcome of one host-domain sample.
struct HostDomainResult {
    std::uint32_t rows{0};                           ///< Rows written into the block.
    TimeBaseStatus status{TimeBaseStatus::NotReady}; ///< Aggregate status after the sample.
};

/// @brief Fill the host rows of `block` from one clock sample.
///
/// Writes the canonical identity row, then the TSC row when `publish_tsc` is
/// set, the kernel reports TSC as the clocksource, the probe agrees, and a
/// positive scale was measured. A canonical read failure yields `Lost` and no
/// rows; a missing TSC row with `publish_tsc` set yields `Degraded`.
///
/// @param source Clock source to sample.
/// @param block Destination block; its `rows` and `domain_count` are replaced.
/// @param epoch Domain epoch (the boot token for host domains).
/// @param now_ns Canonical instant of this publication.
/// @param counter_scale_q32 Measured Q32 scale for the counter row.
/// @param publish_tsc Whether a TSC row is wanted at all.
/// @return Rows written and the resulting aggregate status.
/// @note Cold path only; no allocation.
[[nodiscard]] HostDomainResult sample_host_domains(const HostClockSource& source,
                                                   TimeBaseBlock& block, std::uint64_t epoch,
                                                   std::uint64_t now_ns,
                                                   std::uint64_t counter_scale_q32,
                                                   bool publish_tsc) noexcept;

/// @brief Fill the system-scale rows from one sample.
///
/// Adds `CLOCK_MONOTONIC`, `CLOCK_TAI`, and `CLOCK_REALTIME` rows when the
/// source exposes them, and updates the TAI/UTC offsets.
///
/// @param source Clock source.
/// @param block Destination block whose existing rows are preserved.
/// @param now_ns Hub instant of the sample.
/// @param epoch Domain epoch.
/// @return `true` when at least TAI and REALTIME were sampled.
[[nodiscard]] bool sample_system_domains(const HostClockSource& source, TimeBaseBlock& block,
                                         std::uint64_t now_ns, std::uint64_t epoch) noexcept;

} // namespace nexigen::chrono
