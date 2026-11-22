// nexigen::chrono — host-domain sampling implementation (change
// add-time-base-service; spec chrono/time-base "Canonical monotonic scale").
//
// Cold path: clock reads, one sysfs read at start, and an optional cross-core
// probe. The sampling itself is inline in the header; this unit owns the
// production clock table and the probe so consumer TUs never include <sched.h>.
#include <cerrno>
#include <chrono>
#include <cstdint>
#include <fstream>
#include <optional>
#include <string>
#include <thread>

#if defined(__x86_64__)
#include <x86intrin.h>
#endif

#include <sched.h>
#include <unistd.h>

#include <nexigen/chrono/time_base_host_domain.hpp>
#include <nexigen/chrono/timestamp.hpp>
#include <nexigen/core/detail/fault_injection.hpp>

namespace nexigen::chrono {
namespace {

/// @brief Read `CLOCK_MONOTONIC_RAW` through the production path.
///
/// @param self Unused state pointer.
/// @return Canonical nanoseconds, or `0` on failure.
std::uint64_t production_canonical_ns(void* self) noexcept {
    static_cast<void>(self);
    const core::Result<Timestamp> now = Timestamp::now();
    return now ? now->nanoseconds() : 0U;
}

/// @brief Read the cycle counter through the production path.
///
/// @param self Unused state pointer.
/// @return Cycle-counter value, or `0` when the target has no counter.
std::uint64_t production_counter_ticks(void* self) noexcept {
    static_cast<void>(self);
#if defined(__x86_64__)
    return __rdtsc();
#else
    return 0U;
#endif
}

/// @brief Read one Linux system clock.
///
/// @param self Unused.
/// @param id Clock id.
/// @return Nanoseconds, or zero on failure.
std::uint64_t production_clock_ns(void* self, clockid_t id) noexcept {
    static_cast<void>(self);
    ::timespec ts{};
    if (::clock_gettime(id, &ts) != 0) {
        return 0U;
    }
    return static_cast<std::uint64_t>(ts.tv_sec) * 1'000'000'000ULL +
           static_cast<std::uint64_t>(ts.tv_nsec);
}

/// @brief Read `CLOCK_MONOTONIC`.
std::uint64_t production_monotonic_ns(void* self) noexcept {
    return production_clock_ns(self, CLOCK_MONOTONIC);
}

/// @brief Read `CLOCK_TAI`.
std::uint64_t production_tai_ns(void* self) noexcept {
    return production_clock_ns(self, CLOCK_TAI);
}

/// @brief Read `CLOCK_REALTIME`.
std::uint64_t production_realtime_ns(void* self) noexcept {
    return production_clock_ns(self, CLOCK_REALTIME);
}

/// @brief Report the kernel clocksource verdict cached in the state.
///
/// @param self `HostClockState` pointer.
/// @return `true` when the active clocksource is `tsc`.
bool production_is_tsc(void* self) noexcept {
    const auto* const state = static_cast<const HostClockState*>(self);
    return state != nullptr && state->tsc_is_clocksource;
}

/// @brief Report the cross-core probe verdict cached in the state.
///
/// @param self `HostClockState` pointer.
/// @return `true` when the probe certified a coherent counter.
bool production_synchronized(void* self) noexcept {
    const auto* const state = static_cast<const HostClockState*>(self);
    return state != nullptr && state->tsc_synchronized;
}

/// @brief Read the active clocksource name once.
///
/// @return `true` when sysfs reports `tsc`.
bool read_clocksource_is_tsc() noexcept {
    std::ifstream file{"/sys/devices/system/clocksource/clocksource0/current_clocksource"};
    if (!file.is_open()) {
        return false;
    }
    std::string name;
    file >> name;
    return name == "tsc";
}

} // namespace

HostClockSource production_host_clock(HostClockState& state) noexcept {
    state.tsc_is_clocksource = read_clocksource_is_tsc();
    state.tsc_synchronized = state.tsc_is_clocksource && probe_tsc_synchronized();
    return HostClockSource{&state,
                           &production_canonical_ns,
                           &production_counter_ticks,
                           &production_monotonic_ns,
                           &production_tai_ns,
                           &production_realtime_ns,
                           &production_is_tsc,
                           &production_synchronized};
}

#if defined(__x86_64__)
namespace {

/// @brief One counter value bracketed by two canonical reads.
///
/// `canonical_ns` is the midpoint of the two reads and `span_ns` is the
/// distance between them, so a stall inside the pair can shift the midpoint by
/// at most half the span. The sample therefore carries its own error bound.
struct ClockSample {
    std::uint64_t ticks{0};
    std::int64_t canonical_ns{0};
    std::int64_t span_ns{0};
};

/// @brief An accepted sample is ten times tighter than the certification
///        tolerance, so scheduling delay can never consume the tolerance budget.
constexpr std::int64_t k_tight_span_ns = 5'000;

/// @brief Upper bound on bracketed attempts spent looking for a tight sample.
constexpr int k_max_sample_attempts = 16;

/// @brief Maximum canonical spread that still describes one counter domain.
///
/// A 50 us spread separates one coherent domain from independent counters and
/// sits far above invariant-TSC skew.
constexpr std::int64_t k_tolerance_ns = 50'000;

/// @brief Restores the calling thread's affinity mask on scope exit.
///
/// The probe pins the thread to every CPU in turn; callers keep the mask they
/// had before it ran, including when the probe returns early.
class AffinityScope {
public:
    AffinityScope() noexcept : saved_(::sched_getaffinity(0, sizeof(mask_), &mask_) == 0) {}
    ~AffinityScope() noexcept {
        if (saved_) {
            static_cast<void>(::sched_setaffinity(0, sizeof(mask_), &mask_));
        }
    }
    AffinityScope(const AffinityScope&) = delete;
    AffinityScope& operator=(const AffinityScope&) = delete;

private:
    cpu_set_t mask_{};
    bool saved_{false};
};

/// @brief Take one counter value bracketed by two canonical reads.
///
/// @return The sample, or `std::nullopt` when a clock read fails.
[[nodiscard]] std::optional<ClockSample> take_clock_sample() noexcept {
    const core::Result<Timestamp> before = Timestamp::now();
    if (!before) {
        return std::nullopt;
    }
    // Test-only hook: a stall here models a preemption inside the pair.
    if (core::fault_injection::fault(core::fault_injection::FaultPoint::Chrono_tsc_probe_stall)) {
        std::this_thread::sleep_for(std::chrono::microseconds{200});
    }
    const std::uint64_t ticks = __rdtsc();
    const core::Result<Timestamp> after = Timestamp::now();
    if (!after) {
        return std::nullopt;
    }
    const std::int64_t start = static_cast<std::int64_t>(before->nanoseconds());
    const std::int64_t finish = static_cast<std::int64_t>(after->nanoseconds());
    if (finish < start) {
        return std::nullopt;
    }
    return ClockSample{ticks, start + (finish - start) / 2, finish - start};
}

/// @brief Keep the tightest of a bounded number of bracketed samples.
///
/// A sample disturbed by the scheduler reports a wider span, so the minimum is
/// the sample least perturbed by preemption.
///
/// @return The tightest sample, or `std::nullopt` when every clock read failed.
[[nodiscard]] std::optional<ClockSample> tightest_clock_sample() noexcept {
    std::optional<ClockSample> best{};
    for (int attempt = 0; attempt < k_max_sample_attempts; ++attempt) {
        const std::optional<ClockSample> sample = take_clock_sample();
        if (sample && (!best || sample->span_ns < best->span_ns)) {
            best = sample;
        }
        if (best && best->span_ns <= k_tight_span_ns) {
            break;
        }
    }
    return best;
}

/// @brief Nanoseconds per counter tick in Q32 between two tight samples.
///
/// @param base Earlier sample.
/// @param later Later sample, taken after a known interval.
/// @return The forward scale, or `std::nullopt` when the pair cannot describe
///         one (non-monotonic pair or a zero scale).
[[nodiscard]] std::optional<std::uint64_t> sample_scale(const ClockSample& base,
                                                        const ClockSample& later) noexcept {
    if (later.canonical_ns <= base.canonical_ns || later.ticks <= base.ticks) {
        return std::nullopt;
    }
    const core::detail::uint128 product = core::detail::multiply_u64(
        static_cast<std::uint64_t>(later.canonical_ns - base.canonical_ns), 1ULL << 32U);
    const std::uint64_t scale = core::detail::divide_u128_u64(product, later.ticks - base.ticks);
    if (scale == 0U) {
        return std::nullopt;
    }
    return scale;
}

/// @brief Pin the calling thread to one CPU.
///
/// @param cpu CPU index.
/// @return `true` when the affinity was applied.
[[nodiscard]] bool pin_to_cpu(long cpu) noexcept {
    cpu_set_t set;
    CPU_ZERO(&set);
    CPU_SET(static_cast<std::size_t>(cpu), &set);
    return ::sched_setaffinity(0, sizeof(set), &set) == 0;
}

/// @brief Compare the instant implied by the counter on every online CPU.
///
/// @param cpus Number of online CPUs to visit.
/// @param base Tighter sample that anchors the scale.
/// @param scale Forward scale in Q32 nanoseconds per tick.
/// @return `true` when every sampled CPU agrees within the tolerance.
[[nodiscard]] bool cpu_offsets_agree(long cpus, const ClockSample& base,
                                     std::uint64_t scale) noexcept {
    const AffinityScope affinity{};
    bool have_reference = false;
    std::int64_t reference_offset = 0;
    for (long cpu = 0; cpu < cpus; ++cpu) {
        if (!pin_to_cpu(cpu)) {
            continue; // restricted affinity: skip this CPU rather than fail
        }
        const std::optional<ClockSample> sample = tightest_clock_sample();
        if (!sample) {
            return false;
        }
        const std::int64_t predicted =
            detail::map_forward(sample->ticks, base.ticks, base.canonical_ns, scale);
        const std::int64_t offset = sample->canonical_ns - predicted;
        if (!have_reference) {
            reference_offset = offset;
            have_reference = true;
            continue;
        }
        const std::int64_t spread = offset - reference_offset;
        if (spread > k_tolerance_ns || spread < -k_tolerance_ns) {
            return false;
        }
    }
    return true;
}

} // namespace
#endif

bool probe_tsc_synchronized() noexcept {
#if !defined(__x86_64__)
    return true; // no counter to check; the TSC row is never published anyway
#else
    const long cpus = ::sysconf(_SC_NPROCESSORS_ONLN);
    if (cpus <= 0) {
        return true; // cannot pin; leave the kernel verdict as the evidence
    }
    const std::optional<ClockSample> base = tightest_clock_sample();
    if (!base) {
        return false;
    }
    std::this_thread::sleep_for(std::chrono::milliseconds{2});
    const std::optional<ClockSample> later = tightest_clock_sample();
    if (!later) {
        return false;
    }
    const std::optional<std::uint64_t> scale = sample_scale(*base, *later);
    if (!scale) {
        return false;
    }
    return cpu_offsets_agree(cpus, *base, *scale);
#endif
}

std::uint64_t measure_counter_scale(const HostClockSource& source,
                                    std::chrono::nanoseconds window) noexcept {
    if (source.canonical_ns == nullptr || source.counter_ticks == nullptr ||
        source.self == nullptr || window <= std::chrono::nanoseconds{0}) {
        return 0U;
    }
    const std::uint64_t c1 = source.counter_ticks(source.self);
    const std::uint64_t m1 = source.canonical_ns(source.self);
    std::this_thread::sleep_for(window);
    const std::uint64_t m2 = source.canonical_ns(source.self);
    const std::uint64_t c2 = source.counter_ticks(source.self);
    if (c1 == 0U || m1 == 0U || m2 <= m1 || c2 <= c1) {
        return 0U;
    }
    const std::uint64_t delta_ticks = c2 - c1;
    const std::uint64_t delta_ns = m2 - m1;
    const nexigen::core::detail::uint128 product =
        nexigen::core::detail::multiply_u64(delta_ns, 1ULL << 32U);
    return nexigen::core::detail::divide_u128_u64(product, delta_ticks);
}

namespace {

/// @brief Fill the TSC row when every eligibility gate passes.
[[nodiscard]] bool fill_tsc_row(TimeBaseBlock& block, const HostClockSource& source,
                                std::uint64_t epoch, std::uint64_t canonical,
                                std::uint64_t counter_scale_q32) noexcept {
    if (source.clocksource_is_tsc == nullptr || !source.clocksource_is_tsc(source.self) ||
        source.tsc_synchronized == nullptr || !source.tsc_synchronized(source.self) ||
        source.counter_ticks == nullptr || counter_scale_q32 == 0U) {
        return false;
    }
    const std::uint64_t ticks = source.counter_ticks(source.self);
    if (ticks == 0U) {
        return false;
    }
    DomainCoefficient& tsc_row = block.rows[1];
    tsc_row = DomainCoefficient{};
    tsc_row.domain_id = k_host_counter_domain_id;
    tsc_row.domain_class = TimeDomainClass::CycleCounter;
    tsc_row.lock = LockState::Locked;
    tsc_row.epoch = epoch;
    tsc_row.native_anchor = ticks;
    tsc_row.canonical_anchor_ns = static_cast<std::int64_t>(canonical);
    tsc_row.scale_fwd_q32 = counter_scale_q32;
    tsc_row.scale_rev_q32 = detail::reciprocal_scale_q32(counter_scale_q32);
    tsc_row.flags = DomainCoefficient::has_reverse_bit;
    tsc_row.unc_ns = 1U;
    tsc_row.valid_from_native = ticks;
    tsc_row.valid_to_native =
        ticks + static_cast<std::uint64_t>(nexigen::core::detail::mul_shift_i64(
                    static_cast<std::int64_t>(k_host_row_validity_ns), counter_scale_q32, 32U));
    tsc_row.valid_from_canonical = canonical;
    tsc_row.valid_to_canonical = canonical + k_host_row_validity_ns;
    block.service_flags.set(ServiceFlag::HostCounterAvailable);
    block.service_flags.set(ServiceFlag::HostCounterSynchronized);
    const char tsc_name[] = "host_counter";
    for (std::size_t i = 0; i < sizeof(tsc_name) && i < sizeof(tsc_row.name); ++i) {
        tsc_row.name[i] = tsc_name[i];
    }
    return true;
}

} // namespace

HostDomainResult sample_host_domains(const HostClockSource& source, TimeBaseBlock& block,
                                     std::uint64_t epoch, std::uint64_t now_ns,
                                     std::uint64_t counter_scale_q32, bool publish_tsc) noexcept {
    HostDomainResult result{};
    block.domain_count = 0U;
    block.rows = {};

    if (source.canonical_ns == nullptr || source.self == nullptr) {
        result.status = TimeBaseStatus::Lost;
        return result;
    }
    const std::uint64_t canonical = source.canonical_ns(source.self);
    if (canonical == 0U) {
        result.status = TimeBaseStatus::Lost;
        return result;
    }

    DomainCoefficient& canonical_row = block.rows[0];
    canonical_row.domain_id = k_monotonic_raw_domain_id;
    canonical_row.domain_class = TimeDomainClass::SystemClock;
    canonical_row.lock = LockState::Locked;
    canonical_row.epoch = epoch;
    canonical_row.native_anchor = canonical;
    canonical_row.canonical_anchor_ns = static_cast<std::int64_t>(canonical);
    canonical_row.scale_fwd_q32 = 1ULL << 32U;
    canonical_row.scale_rev_q32 = 1ULL << 32U;
    canonical_row.flags = DomainCoefficient::has_reverse_bit;
    canonical_row.unc_ns = 0U;
    const char canonical_name[] = "monotonic_raw";
    for (std::size_t i = 0; i < sizeof(canonical_name) && i < sizeof(canonical_row.name); ++i) {
        canonical_row.name[i] = canonical_name[i];
    }
    canonical_row.valid_from_native = canonical;
    canonical_row.valid_to_native = canonical + k_host_row_validity_ns;
    canonical_row.valid_from_canonical = canonical;
    canonical_row.valid_to_canonical = canonical + k_host_row_validity_ns;
    result.rows = 1U;
    result.status = TimeBaseStatus::Ready;

    if (source.counter_ticks != nullptr && source.counter_ticks(source.self) != 0U) {
        block.service_flags.set(ServiceFlag::HostCounterAvailable);
    }
    if (!publish_tsc) {
        block.domain_count = static_cast<std::uint16_t>(result.rows);
        return result;
    }

    if (!fill_tsc_row(block, source, epoch, canonical, counter_scale_q32)) {
        result.status = TimeBaseStatus::Degraded;
        block.domain_count = static_cast<std::uint16_t>(result.rows);
        return result;
    }
    result.rows = 2U;
    result.status = TimeBaseStatus::Ready;
    block.domain_count = static_cast<std::uint16_t>(result.rows);
    static_cast<void>(now_ns); // the canonical instant is read from the clock itself
    return result;
}

bool sample_system_domains(const HostClockSource& source, TimeBaseBlock& block,
                           std::uint64_t now_ns, std::uint64_t epoch) noexcept {
    if (source.monotonic_ns == nullptr || source.tai_ns == nullptr ||
        source.realtime_ns == nullptr || source.self == nullptr) {
        return false;
    }
    const std::uint64_t monotonic = source.monotonic_ns(source.self);
    const std::uint64_t tai = source.tai_ns(source.self);
    const std::uint64_t realtime = source.realtime_ns(source.self);
    if (monotonic == 0U || tai == 0U || realtime == 0U) {
        return false;
    }

    auto add = [&](std::uint16_t id, std::string_view name, std::uint64_t native) {
        if (block.domain_count >= time_base_max_domains) {
            return;
        }
        DomainCoefficient& row = block.rows[block.domain_count];
        row = DomainCoefficient{};
        row.domain_id = id;
        row.domain_class = TimeDomainClass::SystemClock;
        row.lock = LockState::Locked;
        row.epoch = epoch;
        row.native_anchor = native;
        row.canonical_anchor_ns = static_cast<std::int64_t>(now_ns);
        row.scale_fwd_q32 = 1ULL << 32U;
        row.scale_rev_q32 = 1ULL << 32U;
        row.flags = DomainCoefficient::has_reverse_bit;
        row.valid_from_native = 0U;
        row.valid_to_native = std::numeric_limits<std::uint64_t>::max();
        row.valid_from_canonical = now_ns;
        row.valid_to_canonical = now_ns + k_host_row_validity_ns;
        const std::size_t take =
            name.size() < sizeof(row.name) - 1U ? name.size() : sizeof(row.name) - 1U;
        for (std::size_t i = 0; i < take; ++i) {
            row.name[i] = name[i];
        }
        row.name[take] = '\0';
        ++block.domain_count;
    };

    add(k_monotonic_domain_id, "monotonic", monotonic);
    add(k_tai_domain_id, "tai", tai);
    add(k_realtime_domain_id, "realtime", realtime);
    block.tai_offset_ns = static_cast<std::int64_t>(tai) - static_cast<std::int64_t>(realtime);
    block.utc_offset_ns = static_cast<std::int64_t>(realtime) - static_cast<std::int64_t>(now_ns);
    block.service_flags.set(ServiceFlag::TaiAvailable);
    // A kernel that has leap-second information keeps TAI and UTC a whole
    // number of seconds apart; a zero or absurd offset means the leap table is
    // not loaded and TAI must not be advertised as synchronized.
    const std::int64_t tai_minus_utc = block.tai_offset_ns;
    if (tai_minus_utc > 0 && tai_minus_utc <= 3'600'000'000'000LL &&
        tai_minus_utc % 1'000'000'000LL == 0) {
        block.service_flags.set(ServiceFlag::TaiSynchronized);
        // The leap table is loaded and TAI is a whole second offset from UTC:
        // the external stack has done its job as far as the kernel can tell.
        block.service_flags.set(ServiceFlag::PtpStackReady);
    }
    return true;
}

} // namespace nexigen::chrono
