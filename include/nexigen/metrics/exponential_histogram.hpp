// nexigen::metrics — the exponential histogram (design D11;
// spec metrics/exponential-histogram).
//
// ExponentialHistogram<Concurrency, Access, Reset> maps a positive value to a
// bucket index in O(1) bounded work using the IEEE-754 exponent and mantissa
// bits alone (std::bit_cast) — no std::log, no std::pow, no allocation:
//
//   index(v) = (exponent << scale) + (mantissa >> (52 - scale))
//
// Each power-of-two octave is subdivided into 2^scale equal significand
// intervals, so bucket k of octave e covers [2^e * (1 + k/S), 2^e * (1 +
// (k+1)/S)) with S = 2^scale. Both edges are exactly representable doubles,
// so a value is assigned to a bucket exactly — no value can fall on the wrong
// side of an edge. The nominal geometric ratio of the bucket series is
// base = 2^(2^-scale) (the geometric mean of the per-bucket ratios).
//
// The inline bucket table is bounded (256 buckets). When the observed value
// range does not fit, the instrument COARSENS: the effective scale drops by
// one, the buckets collapse pairwise and the window widens. Coarsening never
// loses a sample: the bucket totals plus the zero count always equal the
// accepted sample count.
//
// Scale 0..20 is supported; the configured scale is the finest resolution.
#pragma once

#include <array>
#include <atomic>
#include <bit>
#include <cmath>
#include <cstdint>
#include <limits>
#include <string>

#include <nexigen/metrics/concepts/metric_concept.hpp>
#include <nexigen/metrics/detail/instrument_helpers.hpp>
#include <nexigen/metrics/detail/layout.hpp>
#include <nexigen/metrics/detail/probes.hpp>
#include <nexigen/metrics/detail/thread_slot.hpp>
#include <nexigen/metrics/metric_snapshot.hpp>
#include <nexigen/metrics/policy_enums.hpp>

namespace nexigen::metrics {

template <nexigen::metrics::ConcurrencyPolicy Concurrency, nexigen::metrics::AccessPolicy Access,
          nexigen::metrics::ResetPolicy Reset>
class ExponentialHistogram;

/// @brief Construction guard: only a Meter may create instruments.
///
/// Forward-declared so instrument constructors can take it by reference; the
/// complete definition lives in `meter.hpp`.
class MeterPasskey;

namespace detail {

/// @brief Number of inline bucket counters kept per recording slot.
///
/// A slot that cannot hold the observed value range coarsens until it fits
/// within this fixed window, so recording never allocates.
inline constexpr std::size_t max_exponential_buckets = 256;

/// @brief Highest supported scale, i.e. the finest bucket resolution.
///
/// At scale 20 each power-of-two octave is split into `2^20` intervals, one per
/// entry of the shared boundary table.
inline constexpr std::int32_t max_exponential_scale = 20;

/// @brief Sentinel for "the window has not been anchored yet".
inline constexpr std::int64_t unanchored_index = std::numeric_limits<std::int64_t>::min();

/// @brief Lower octave of the shared fallback window.
///
/// The fixed scale-0 bucket range `[fallback_window_base, fallback_window_base +
/// 256)` covers octaves 2^-128 .. 2^128; more extreme values fold into the edge
/// buckets. The window never coarsens, so the fallback shard stays exact under
/// contention.
inline constexpr std::int64_t fallback_window_base = -128;

/// @brief Floor division by a power of two, portable for negative values.
///
/// Avoids the implementation-defined right shift of negative integers, so index
/// rebasing is well-defined on every platform.
///
/// @param value Dividend (may be negative).
/// @param shift Base-2 logarithm of the divisor; `0` returns `value` unchanged.
/// @return `floor(value / 2^shift)`.
[[nodiscard]] inline std::int64_t floor_div_pow2(std::int64_t value, std::uint32_t shift) noexcept {
    if (shift == 0U) {
        return value;
    }
    if (value >= 0) {
        return value >> shift;
    }
    const std::int64_t magnitude = -value;
    return -((magnitude + ((1LL << shift) - 1LL)) >> shift);
}

/// @brief Bit width of the shared boundary table: `2^20` entries.
///
/// Bucket `i`'s exact geometric lower edge at scale `s` is
/// `B(i) = 2^(i / 2^s)` for base `2^(2^-s)` (design D11). One table serves every
/// scale: `factor_s[r] = master[r << (20 - s)]`, so the edges agree bit-for-bit
/// with `std::pow(2.0, r / 2^s)`.
inline constexpr std::int32_t boundary_master_bits = 20;
/// @brief Number of entries in the shared boundary table.
inline constexpr std::size_t boundary_master_size = std::size_t{1} << boundary_master_bits;

/// @brief Lazily generated, process-wide table of exact bucket lower edges.
///
/// Entry `r` holds `2^(r / 2^20)`; a scale `s` bucket edge reads
/// `master[r << (20 - s)]`. Generated once on the first exponential-histogram
/// creation, in static storage: zero heap allocation and demand-paged, so a
/// scale-10 user touches only 8 KiB of it.
///
/// @return Pointer to the first of `boundary_master_size` doubles, valid for
///         the lifetime of the process.
/// @note Cold path; thread-safe initialization is guaranteed by the language.
[[nodiscard]] inline const double* boundary_master() noexcept {
    static const std::array<double, boundary_master_size> table = [] {
        std::array<double, boundary_master_size> out{};
        for (std::size_t r = 0; r < boundary_master_size; ++r) {
            out[r] =
                std::pow(2.0, static_cast<double>(r) / static_cast<double>(boundary_master_size));
        }
        return out;
    }();
    return table.data();
}

/// @brief Exact lower edge `B(i) = 2^(i / 2^scale)` of bucket `i`.
///
/// @param index Bucket index; may be negative.
/// @param scale Effective scale, clamped to `[0, boundary_master_bits]` here.
/// @return The exact, representable double lower bound of the bucket.
/// @note Spec `metrics/exponential-histogram`, requirement "Bitwise bucket index
///       mapping".
/// @note Cold path: only snapshots and the bounded index correction call it.
[[nodiscard]] inline double bucket_lower_edge(std::int64_t index, std::int32_t scale) noexcept {
    const std::int32_t s =
        (scale < 0) ? 0 : ((scale > boundary_master_bits) ? boundary_master_bits : scale);
    const std::int64_t quotient = floor_div_pow2(index, static_cast<std::uint32_t>(s));
    const std::int64_t remainder = index - (quotient << s); // 0 <= remainder < 2^s
    return std::ldexp(
        boundary_master()[static_cast<std::size_t>(remainder) << (boundary_master_bits - s)],
        static_cast<int>(quotient));
}

/// @brief Bucket index of a positive finite value at the given scale.
///
/// Computes the exact `floor(2^scale * log2(v))`, so bucket `i` covers
/// `[B(i), B(i+1))` (design D11). Work is O(1) and bounded: IEEE-754 exponent
/// and mantissa extraction via `std::bit_cast`, a small log2 lookup, and at most
/// four boundary corrections — no `std::log`, `std::log2` or allocation per
/// record.
///
/// @param value Positive, finite sample value.
/// @param scale Effective scale in `[0, max_exponential_scale]`.
/// @return The bucket index; monotonic in `value`.
/// @note Spec `metrics/exponential-histogram`, requirement "Bitwise bucket index
///       mapping".
[[nodiscard]] inline std::int64_t exponential_index(double value, std::int32_t scale) noexcept {
    // Normalize subnormals exactly (v * 2^64 is normal) and work entirely in
    // the scaled domain: there the boundary doubles cannot collapse onto the
    // subnormal grid, so the correction comparisons stay decisive. The index
    // is compensated by shift * 2^scale at the end.
    double probe = value;
    std::int64_t shift = 0;
    std::uint64_t bits = std::bit_cast<std::uint64_t>(probe);
    if (((bits >> 52U) & 0x7FFU) == 0U) {
        probe = value * 0x1p64;
        shift = 64;
        bits = std::bit_cast<std::uint64_t>(probe);
    }
    const std::int64_t exponent = static_cast<std::int64_t>((bits >> 52U) & 0x7FFU) - 1023;
    const double m = static_cast<double>(bits & ((1ULL << 52U) - 1ULL)) * 0x1p-52; // [0, 1)

    // Candidate for floor(2^scale * log2(1 + m)): log2 lookup on the top 8
    // fraction bits plus a quadratic fit on the residual — error far below one
    // index unit at every supported scale, so the correction rarely moves.
    static const std::array<double, 256> log2_lookup = [] {
        std::array<double, 256> out{};
        for (std::size_t i = 0; i < out.size(); ++i) {
            out[i] = std::log2(1.0 + static_cast<double>(i) / 256.0);
        }
        return out;
    }();
    const std::size_t hi_index = static_cast<std::size_t>(m * 256.0);
    const double hi = static_cast<double>(hi_index) / 256.0;
    const double z = (m - hi) / (1.0 + hi); // [0, 2^-8)
    constexpr double log2e = 1.4426950408889634;
    const double approx = (log2_lookup[hi_index] + (z - 0.5 * z * z) * log2e) *
                          static_cast<double>(std::size_t{1} << scale);
    std::int64_t index =
        (exponent << scale) + static_cast<std::int64_t>(approx < 0.0 ? approx - 1.0 : approx);

    // Bounded correction against the exact boundaries (design D11).
    for (int step = 0; step < 4; ++step) {
        if (probe < bucket_lower_edge(index, scale)) {
            --index;
            continue;
        }
        if (probe >= bucket_lower_edge(index + 1, scale)) {
            ++index;
            continue;
        }
        break;
    }
    return index - (shift << scale);
}

/// @brief One exponential bank: buckets, window anchor and running summary.
///
/// `base_index` anchors the 256-entry bucket window in index space and
/// `effective_scale` records the resolution in force after any coarsening. A
/// bank is the consistency unit of the instrument, copied out under the
/// sequence handshake. `alignas(64)` keeps neighbouring slots' banks apart.
///
/// @note Spec `metrics/exponential-histogram`, requirement "Bounded bucket
///       storage and coarsening".
struct alignas(64) ExponentialBank {
    /// @brief Plain, non-atomic copy of one bank's contents (harvest side).
    ///
    /// `base_index == unanchored_index` means the bank has not seen a sample
    /// yet; `count` excludes zeros, which the instrument tracks separately.
    struct View {
        /// @brief Per-bucket sample counts; index `j` is `base_index + j`.
        std::uint64_t buckets[max_exponential_buckets]{};
        /// @brief Total positive samples folded into the bank.
        std::uint64_t count{0};
        /// @brief Sum of the recorded values.
        double sum{0.0};
        /// @brief Smallest recorded value; meaningful only when `count > 0`.
        double minimum{0.0};
        /// @brief Largest recorded value; meaningful only when `count > 0`.
        double maximum{0.0};
        /// @brief Resolution in force after any coarsening.
        std::int32_t effective_scale{0};
        /// @brief Bucket index of `buckets[0]`, or `unanchored_index`.
        std::int64_t base_index{unanchored_index};
    };

    /// @brief Bucket counters, indexed relative to `base_index`.
    std::atomic<std::uint64_t> buckets[max_exponential_buckets]{};
    /// @brief Total positive samples in the bank.
    std::atomic<std::uint64_t> count{0};
    /// @brief Samples recorded at exactly zero (tracked per bank).
    std::atomic<std::uint64_t> zero_count{0};
    /// @brief Sum of the recorded values.
    std::atomic<double> sum{0.0};
    /// @brief Smallest recorded value, seeded at +infinity.
    std::atomic<double> minimum{std::numeric_limits<double>::infinity()};
    /// @brief Largest recorded value, seeded at -infinity.
    std::atomic<double> maximum{-std::numeric_limits<double>::infinity()};
    /// @brief First bucket index of the window, or `unanchored_index`.
    std::atomic<std::int64_t> base_index{unanchored_index};
    /// @brief Current resolution of the window.
    std::atomic<std::int32_t> effective_scale{0};

    /// @brief Reset the bank to empty, unanchored and at scale 0.
    ///
    /// Cold path; only a harvest holding the sequence handshake may call it.
    void clear() noexcept {
        for (std::size_t i = 0; i < max_exponential_buckets; ++i) {
            buckets[i].store(0, std::memory_order_relaxed);
        }
        count.store(0, std::memory_order_relaxed);
        zero_count.store(0, std::memory_order_relaxed);
        sum.store(0.0, std::memory_order_relaxed);
        minimum.store(std::numeric_limits<double>::infinity(), std::memory_order_relaxed);
        maximum.store(-std::numeric_limits<double>::infinity(), std::memory_order_relaxed);
        base_index.store(unanchored_index, std::memory_order_relaxed);
    }

    /// @brief Copy the bank without consuming it (Cumulative harvests).
    ///
    /// @return A plain `View` of the current buckets, window and summary.
    /// @note Callers must hold the sequence handshake while copying a
    ///       multi-writer bank; the loads themselves are relaxed.
    [[nodiscard]] View load_view() const noexcept {
        View view{};
        for (std::size_t i = 0; i < max_exponential_buckets; ++i) {
            view.buckets[i] = buckets[i].load(std::memory_order_relaxed);
        }
        view.count = count.load(std::memory_order_relaxed);
        view.sum = sum.load(std::memory_order_relaxed);
        view.minimum = minimum.load(std::memory_order_relaxed);
        view.maximum = maximum.load(std::memory_order_relaxed);
        view.effective_scale = effective_scale.load(std::memory_order_relaxed);
        view.base_index = base_index.load(std::memory_order_relaxed);
        return view;
    }

    /// @brief Consuming copy for the multi-writer fallback bank.
    ///
    /// Each counter and summary field is exchanged to its empty value, so every
    /// update lands in exactly one window. A single record can straddle two
    /// fields because the exchanges are not one atomic operation; that
    /// record-level straddle is documented in design D15.
    ///
    /// @return A plain `View` of the harvested buckets and summary.
    /// @note Spec `metrics/exponential-histogram`, requirement "Exponential
    ///       histogram reset windows".
    [[nodiscard]] View take_view() noexcept {
        View view{};
        for (std::size_t i = 0; i < max_exponential_buckets; ++i) {
            view.buckets[i] = buckets[i].exchange(0U, std::memory_order_relaxed);
        }
        view.count = count.exchange(0U, std::memory_order_relaxed);
        view.sum = sum.exchange(0.0, std::memory_order_relaxed);
        view.minimum =
            minimum.exchange(std::numeric_limits<double>::infinity(), std::memory_order_relaxed);
        view.maximum =
            maximum.exchange(-std::numeric_limits<double>::infinity(), std::memory_order_relaxed);
        view.effective_scale = effective_scale.load(std::memory_order_relaxed);
        view.base_index = base_index.load(std::memory_order_relaxed);
        return view;
    }

    /// @brief Fold one bank view into a merged frame at a common scale.
    ///
    /// Rebases the bank's index space onto `merged_scale` (finer banks shift
    /// down, the scale-0 fallback up) and folds out-of-window buckets into the
    /// edge buckets, so no sample is dropped.
    ///
    /// @param view Bank copy to absorb.
    /// @param merged Accumulator receiving the rebased buckets and summary.
    /// @param merged_scale Scale of the merged frame (the coarsest in play).
    /// @note Spec `metrics/exponential-histogram`, requirement "Bounded bucket
    ///       storage and coarsening".
    static void merge_view(const View& view, struct ExponentialMerge& merged,
                           std::int32_t merged_scale) noexcept;

    /// @brief Fold one sample into an exclusive bank (single writer).
    ///
    /// Plain load/store pairs at the caller-computed window position; the first
    /// sample seeds `minimum` and `maximum`.
    ///
    /// @param value Positive finite sample value.
    /// @param position Window position in `[0, max_exponential_buckets)`.
    /// @warning Exclusive-slot use only: the caller must be the sole writer of
    ///          this bank, guarded by the sequence handshake.
    void apply(double value, std::size_t position) noexcept {
        const std::uint64_t seen = count.load(std::memory_order_relaxed);
        buckets[position].store(buckets[position].load(std::memory_order_relaxed) + 1U,
                                std::memory_order_relaxed);
        count.store(seen + 1U, std::memory_order_relaxed);
        sum.store(sum.load(std::memory_order_relaxed) + value, std::memory_order_relaxed);
        if (seen == 0U) {
            minimum.store(value, std::memory_order_relaxed);
            maximum.store(value, std::memory_order_relaxed);
        } else {
            if (value < minimum.load(std::memory_order_relaxed)) {
                minimum.store(value, std::memory_order_relaxed);
            }
            if (value > maximum.load(std::memory_order_relaxed)) {
                maximum.store(value, std::memory_order_relaxed);
            }
        }
    }

    /// @brief Fold one sample into the fixed-window shared fallback bank.
    ///
    /// Every update is an atomic read-modify-write: `fetch_add` for counters and
    /// sum, a compare-exchange loop for the extrema. The caller derives `position`
    /// from the octave, so the window never coarsens and totals stay exact under
    /// contention.
    ///
    /// @param position Fixed scale-0 window position in
    ///        `[0, max_exponential_buckets)`.
    /// @param value Positive finite sample value.
    /// @note Spec `metrics/exponential-histogram`, requirement "Multi-threaded
    ///       exponential histogram recording".
    void apply_multi_at(std::size_t position, double value) noexcept {
        buckets[position].fetch_add(1U, std::memory_order_relaxed);
        count.fetch_add(1U, std::memory_order_relaxed);
        sum.fetch_add(value, std::memory_order_relaxed);
        double current = minimum.load(std::memory_order_relaxed);
        while (value < current &&
               !minimum.compare_exchange_weak(current, value, std::memory_order_relaxed,
                                              std::memory_order_relaxed)) {}
        current = maximum.load(std::memory_order_relaxed);
        while (value > current &&
               !maximum.compare_exchange_weak(current, value, std::memory_order_relaxed,
                                              std::memory_order_relaxed)) {}
    }

    /// @brief Halve the resolution: buckets collapse pairwise, the window widens.
    ///
    /// Re-anchors `base_index` to the new coarser window and sums each pair of
    /// adjacent buckets into one. Buckets that fall outside the new window fold
    /// into its edge buckets, so the bucket totals still equal the sample count.
    ///
    /// @param lowest_index Lowest bucket index that must remain representable
    ///        after coarsening.
    /// @note Spec `metrics/exponential-histogram`, requirement "Bounded bucket
    ///       storage and coarsening".
    /// @note Scales down to 0; `apply_to_bank` folds into an edge bucket instead
    ///       of coarsening further.
    void coarsen(std::int64_t lowest_index) noexcept {
        const std::int32_t scale = effective_scale.load(std::memory_order_relaxed);
        const std::int64_t base = base_index.load(std::memory_order_relaxed);
        const std::int64_t anchor = (base == unanchored_index)
                                        ? lowest_index
                                        : ((base < lowest_index) ? base : lowest_index);
        const std::int64_t new_base = floor_div_pow2(anchor, 1U);
        std::uint64_t collapsed[max_exponential_buckets]{};
        if (base != unanchored_index) {
            for (std::size_t j = 0; j < max_exponential_buckets; ++j) {
                const std::uint64_t value = buckets[j].load(std::memory_order_relaxed);
                if (value == 0U) {
                    continue;
                }
                const std::int64_t index = base + static_cast<std::int64_t>(j);
                const std::int64_t new_index = floor_div_pow2(index, 1U);
                const std::int64_t position = new_index - new_base;
                // Loss-free collapse: buckets falling outside the new window
                // fold into the edge buckets (spec "coarsen without losing
                // samples"), matching the merge and scale-0 paths.
                if (position >= 0 &&
                    position < static_cast<std::int64_t>(max_exponential_buckets)) {
                    collapsed[static_cast<std::size_t>(position)] += value;
                } else if (position < 0) {
                    collapsed[0] += value;
                } else {
                    collapsed[max_exponential_buckets - 1U] += value;
                }
            }
        }
        for (std::size_t j = 0; j < max_exponential_buckets; ++j) {
            buckets[j].store(collapsed[j], std::memory_order_relaxed);
        }
        base_index.store(new_base, std::memory_order_relaxed);
        effective_scale.store(scale - 1, std::memory_order_relaxed);
    }
};

/// @brief A recording slot: double-buffered banks plus the sequence handshake.
///
/// `active` selects the bank receiving new samples; `seq` is odd while a
/// producer is mid-record. `alignas(64)` keeps each thread's slot on its own
/// cache line.
struct alignas(64) ExponentialSlot {
    /// @brief The two banks a reset-on-read harvest swaps between.
    ExponentialBank bank[2];
    /// @brief Sequence handshake: odd during a record, even when stable.
    std::atomic<std::uint32_t> seq{0};
    /// @brief Index of the bank currently receiving samples.
    std::atomic<std::uint8_t> active{0};
};

/// @brief Bounded retry budget for the harvest sequence handshake.
///
/// A slot that stays busy for this many attempts is deferred to the next window
/// with its contents untouched, so no sample is lost.
inline constexpr int exponential_handshake_attempts = 64;

/// @brief Merged view of exponential buckets at a common scale.
///
/// Harvests rebase every slot and the fallback bank into this frame, then render
/// a `HistogramSnapshot` from it. `zeros` counts samples at exactly zero, which
/// rank below every bucket; `empty` keeps min/max meaningful for an empty window.
struct ExponentialMerge {
    /// @brief Accumulated per-bucket counts in the merged window.
    std::uint64_t buckets[max_exponential_buckets]{};
    /// @brief Accumulated positive sample count.
    std::uint64_t total{0};
    /// @brief Accumulated count of exact-zero samples in the frame.
    ///
    /// Zeros rank below every positive bucket; the instrument tracks them in its
    /// own zero counter, so harvests may leave this at zero.
    std::uint64_t zeros{0};
    /// @brief Accumulated sum of recorded values.
    double sum{0.0};
    /// @brief Smallest recorded value across banks; valid when `!empty`.
    double minimum{0.0};
    /// @brief Largest recorded value across banks; valid when `!empty`.
    double maximum{0.0};
    /// @brief Scale all buckets were rebased onto.
    std::int32_t scale{0};
    /// @brief Bucket index of `buckets[0]`, or `unanchored_index`.
    std::int64_t base{unanchored_index};
    /// @brief True until a bank with a non-zero count has been absorbed.
    bool empty{true};
};

/// @brief Fold one bank view into the merged frame at `merged_scale`.
///
/// Definition of the in-class declaration; see it for the full rebasing and
/// edge-folding contract.
///
/// @note Spec `metrics/exponential-histogram`, requirement "Bounded bucket
///       storage and coarsening".
inline void ExponentialBank::merge_view(const View& view, ExponentialMerge& merged,
                                        std::int32_t merged_scale) noexcept {
    if (view.base_index == unanchored_index) {
        return;
    }
    const std::int32_t delta = view.effective_scale - merged_scale;
    const auto rebase = [delta](std::int64_t index) noexcept {
        return (delta > 0) ? floor_div_pow2(index, static_cast<std::uint32_t>(delta))
                           : (index << (-delta));
    };
    // The merged base is precomputed before the fold; lowering it here would
    // desynchronize buckets placed by earlier banks from the rendered edges.
    // A bank outside the window folds into the edge buckets instead.
    if (merged.base == unanchored_index) {
        merged.base = rebase(view.base_index);
    }
    for (std::size_t j = 0; j < max_exponential_buckets; ++j) {
        const std::uint64_t value = view.buckets[j];
        if (value == 0U) {
            continue;
        }
        const std::int64_t index_here = rebase(view.base_index + static_cast<std::int64_t>(j));
        const std::int64_t position = index_here - merged.base;
        if (position < 0) {
            merged.buckets[0] += value;
        } else if (position >= static_cast<std::int64_t>(max_exponential_buckets)) {
            merged.buckets[max_exponential_buckets - 1U] += value;
        } else {
            merged.buckets[static_cast<std::size_t>(position)] += value;
        }
    }
    merged.total += view.count;
    merged.sum += view.sum;
    if (view.count > 0U) {
        if (merged.empty) {
            merged.minimum = view.minimum;
            merged.maximum = view.maximum;
            merged.empty = false;
        } else {
            if (view.minimum < merged.minimum) {
                merged.minimum = view.minimum;
            }
            if (view.maximum > merged.maximum) {
                merged.maximum = view.maximum;
            }
        }
    }
}

} // namespace detail

/// @brief A histogram with automatic base-2 exponential buckets.
///
/// Maps each positive finite sample to a bucket index in O(1) bounded work from
/// the IEEE-754 exponent and mantissa, so recording needs no logarithm and no
/// allocation. The configured `scale` sets the finest resolution: octave `e` is
/// split into `2^scale` intervals of ratio `base = 2^(2^-scale)`. When the
/// observed range cannot fit the 256 inline buckets, the instrument coarsens its
/// effective scale (halving resolution) while keeping every sample counted.
///
/// `SingleThreaded` recording folds into one plain bank. `MultiThreaded`
/// recording uses a `thread_local`-assigned exclusive slot per thread plus a
/// shared, fixed-scale-0 fallback bank beyond the shard pool. Harvests are
/// cold-path and merge the banks under a sequence handshake; `ResetOnRead` swaps
/// the double-buffered bank so a racing record still lands in exactly one
/// window.
///
/// @tparam Concurrency Recording policy: `SingleThreaded` (single writer) or
///         `MultiThreaded` (per-thread shards plus a shared fallback).
/// @tparam Access `Readable` exposes `read()` and `snapshot()`; `WriteOnly`
///         removes both at compile time.
/// @tparam Reset `Cumulative` keeps totals across reads; `ResetOnRead` reports
///         and clears one window per observation.
///
/// @note Spec `metrics/exponential-histogram`, requirement "Exponential scale
///       configuration".
/// @note Construction is Meter-only: spec `metrics/metric-provider`,
///       requirement "Factory hierarchy and passkey construction".
/// @warning A `SingleThreaded` instrument is single-writer: reads and harvests
///          must not run concurrently with recording unless the caller
///          establishes happens-before.
template <ConcurrencyPolicy Concurrency = ConcurrencyPolicy::SingleThreaded,
          AccessPolicy Access = AccessPolicy::Readable, ResetPolicy Reset = ResetPolicy::Cumulative>
class ExponentialHistogram {
public:
    /// @brief `MetricKind` tag consumed by the concept and registry.
    static constexpr MetricKind kind = MetricKind::ExponentialHistogram;
    /// @brief Concurrency policy this instantiation was created with.
    static constexpr ConcurrencyPolicy concurrency = Concurrency;
    /// @brief Access policy this instantiation was created with.
    static constexpr AccessPolicy access = Access;
    /// @brief Reset policy this instantiation was created with.
    static constexpr ResetPolicy reset = Reset;

    /// @brief Deleted: instruments are non-copyable registry-owned handles.
    ExponentialHistogram(const ExponentialHistogram&) = delete;
    /// @brief Deleted: instruments are non-copyable registry-owned handles.
    ExponentialHistogram& operator=(const ExponentialHistogram&) = delete;
    /// @brief Deleted: instruments live at a fixed registry address.
    ExponentialHistogram(ExponentialHistogram&&) = delete;
    /// @brief Deleted: instruments live at a fixed registry address.
    ExponentialHistogram& operator=(ExponentialHistogram&&) = delete;

    /// @brief Record one sample into its bucket and the running summary.
    ///
    /// Positive finite values land in a bucket, coarsening the window if needed.
    /// Positive and negative zeros increment the zero counter (they rank below
    /// every bucket in percentiles). Negative and non-finite values leave the
    /// buckets and summary untouched and increment the ignored-sample counter.
    ///
    /// @param value Sample to record; sign and finiteness select its disposition.
    /// @note Spec `metrics/exponential-histogram`, requirement "Value domain
    ///       handling".
    /// @warning Under `ConcurrencyPolicy::SingleThreaded` the caller must be the
    ///          only writer.
    void record(double value) noexcept {
        if (!std::isfinite(value)) {
            ignored_.fetch_add(1U, std::memory_order_relaxed);
            return;
        }
        if (value == 0.0) {
            zero_count_.fetch_add(1U, std::memory_order_relaxed);
            return;
        }
        if (value < 0.0) {
            ignored_.fetch_add(1U, std::memory_order_relaxed);
            return;
        }
        if constexpr (Concurrency == ConcurrencyPolicy::SingleThreaded) {
            apply_to_bank(cell_, value);
        } else {
            const std::uint8_t slot = detail::thread_slot(MetricKind::ExponentialHistogram);
            if (slot == detail::fallback_slot) {
                // Shared fallback shard (beyond MaxThreads): bucketed at the
                // fixed scale-0 window [base, base + 256) octaves, with
                // extreme octaves folded into the edge buckets.
                const std::int64_t position =
                    detail::exponential_index(value, 0) + detail::fallback_window_base;
                const std::size_t clamped =
                    (position < 0)
                        ? 0U
                        : ((position >= static_cast<std::int64_t>(detail::max_exponential_buckets))
                               ? detail::max_exponential_buckets - 1U
                               : static_cast<std::size_t>(position));
                fallback_.apply_multi_at(clamped, value);
                return;
            }
            auto& target = slots_[slot];
            const std::uint32_t marker = target.seq.load(std::memory_order_relaxed);
            // Announce as a full barrier (xchg on x86): the update must not
            // become visible before the announcement (design D9/D15).
            target.seq.store(marker + 1U, std::memory_order_seq_cst);
            apply_to_bank(target.bank[target.active.load(std::memory_order_relaxed)], value);
            target.seq.store(marker + 2U, std::memory_order_release);
        }
    }

    /// @brief Take a snapshot of the current (or current window's) values.
    ///
    /// Finds the coarsest effective scale in play so every slot can be rebased
    /// onto one axis, merges the slots and the fallback bank, then renders bucket
    /// edges and computes `p50`/`p90`/`p99`/`p99.9` from bucket representatives
    /// with zeros ranked first. Under `ResetOnRead` the merged banks and the zero
    /// counter are consumed as they are read.
    ///
    /// @return `HistogramSnapshot` with effective scale, bucket edges and counts,
    ///         summary, zero count, percentiles and ignored samples.
    /// @note Spec `metrics/exponential-histogram`, requirement "Exponential
    ///       histogram read surface".
    /// @note Spec `metrics/exponential-histogram`, requirement "Exponential
    ///       histogram reset windows".
    [[nodiscard]] HistogramSnapshot read() const
        requires(Access == AccessPolicy::Readable)
    {
        // Pass 1: the coarsest scale in play — the merged frame lives there so
        // every slot's buckets land on a common axis.
        std::int32_t merged_scale = scale_;
        if constexpr (Concurrency == ConcurrencyPolicy::SingleThreaded) {
            merged_scale = cell_.effective_scale.load(std::memory_order_relaxed);
        } else {
            for (std::size_t i = 0; i < detail::shard_count; ++i) {
                const std::int32_t first =
                    slots_[i].bank[0].effective_scale.load(std::memory_order_relaxed);
                const std::int32_t second =
                    slots_[i].bank[1].effective_scale.load(std::memory_order_relaxed);
                const std::int32_t slot_scale = (first < second) ? first : second;
                if (slot_scale < merged_scale) {
                    merged_scale = slot_scale;
                }
            }
        }

        detail::ExponentialMerge merged{};
        merged.scale = merged_scale;
        merged.base = compute_merged_base(merged_scale);
        if constexpr (Concurrency == ConcurrencyPolicy::SingleThreaded) {
            const detail::ExponentialBank::View cell_view = cell_.load_view();
            if (Reset == ResetPolicy::ResetOnRead) {
                cell_.clear();
            }
            detail::ExponentialBank::merge_view(cell_view, merged, merged_scale);
        } else {
            for (std::size_t i = 0; i < detail::shard_count; ++i) {
                merge_slot(i, merged, merged_scale);
            }
        }

        const bool consuming = (Reset == ResetPolicy::ResetOnRead);
        // The shared fallback shard lives in a fixed scale-0 window and is
        // rebased onto the merged frame by merge_bank.
        detail::ExponentialBank::merge_view(
            consuming ? fallback_.take_view() : fallback_.load_view(), merged, merged_scale);
        const std::uint64_t zeros = consuming ? zero_count_.exchange(0U, std::memory_order_relaxed)
                                              : zero_count_.load(std::memory_order_relaxed);

        HistogramSnapshot snapshot{};
        snapshot.ignored_samples = ignored_.load(std::memory_order_relaxed);
        snapshot.zero_count = zeros;
        snapshot.count = merged.total + zeros;
        snapshot.sum = merged.sum;
        snapshot.effective_scale = merged_scale;

        bool have_value = false;
        if (!merged.empty) {
            snapshot.min = merged.minimum;
            snapshot.max = merged.maximum;
            have_value = true;
        }
        if (zeros > 0U) {
            snapshot.min = 0.0; // zeros rank below every positive bucket
            if (!have_value) {
                snapshot.max = 0.0;
            }
        }

        snapshot.bucket_count = detail::max_exponential_buckets;
        const std::int64_t base = (merged.base == detail::unanchored_index) ? 0 : merged.base;
        for (std::size_t i = 0; i < snapshot.bucket_count; ++i) {
            snapshot.buckets_lower[i] =
                detail::bucket_lower_edge(base + static_cast<std::int64_t>(i), merged_scale);
            snapshot.buckets_count[i] = merged.buckets[i];
        }
        compute_percentiles(snapshot);
        return snapshot;
    }

    /// @brief Build the full `MetricRecord` for this instrument.
    ///
    /// Combines the identity installed by the Meter, the policy classification
    /// and the current histogram values from `read()`.
    ///
    /// @return `MetricRecord` carrying `MetricKind::ExponentialHistogram` and the
    ///         histogram snapshot from `read()`.
    /// @note Spec `metrics/exponential-histogram`, requirement "Exponential
    ///       histogram read surface".
    [[nodiscard]] MetricRecord snapshot() const
        requires(Access == AccessPolicy::Readable)
    {
        MetricRecord record = detail::make_record(kind, concurrency, access, reset, identity_);
        record.histogram = read();
        return record;
    }

    /// @brief Cold-path diagnostic string; never resets a window.
    ///
    /// @return Implementation-defined text naming the kind, policies and
    ///         configured scale; the free `to_string` and `std::formatter`
    ///         mirror it.
    /// @note Project mandate 9: every public type renders `to_string()`.
    /// @note Spec `metrics/exponential-histogram`, requirement "Exponential
    ///       histogram serialization contract".
    [[nodiscard]] std::string to_string() const {
        std::string out = "ExponentialHistogram(kind=";
        detail::append_classification(out, kind, concurrency, access, reset);
        out += ", scale=" + std::to_string(scale_);
        out += ")";
        return out;
    }

    /// @brief Diagnostic JSON for the instrument (cold path, write-only).
    ///
    /// Emits the policy classification and the configured scale. The instrument
    /// is deliberately NOT deserializable: its construction is Meter-only.
    ///
    /// @param j JSON value overwritten with the instrument diagnostics.
    /// @param histogram Instrument to describe.
    /// @note Project mandate 10: JSON is the structured diagnostic surface.
    /// @note Spec `metrics/exponential-histogram`, requirement "Exponential
    ///       histogram serialization contract".
    friend void to_json(nlohmann::json& j, const ExponentialHistogram& histogram) {
        j = detail::classification_json(ExponentialHistogram::kind,
                                        ExponentialHistogram::concurrency,
                                        ExponentialHistogram::access, ExponentialHistogram::reset);
        j["scale"] = histogram.scale_;
    }

private:
    friend class Meter;
    friend class MetricProvider;

    /// @brief Meter-only constructor; the passkey proves factory authority.
    ///
    /// Seeds the configured scale into the single-threaded bank, both banks of
    /// every slot, and the fallback bank, then warms the boundary table while
    /// still on the cold path. The fallback bank keeps its fixed scale-0 window.
    /// The unnamed `MeterPasskey` argument proves factory authority and carries
    /// no state.
    ///
    /// @param scale Configured scale in `[0, max_exponential_scale]`, already
    ///        validated by the Meter.
    explicit ExponentialHistogram(const MeterPasskey&, std::int32_t scale) noexcept
        : scale_(scale) {
        cell_.effective_scale.store(scale, std::memory_order_relaxed);
        for (auto& slot : slots_) {
            slot.bank[0].effective_scale.store(scale, std::memory_order_relaxed);
            slot.bank[1].effective_scale.store(scale, std::memory_order_relaxed);
        }
        // The shared fallback shard uses a fixed scale-0 window that never
        // coarsens; warm the boundary table while we are on the cold path.
        fallback_.effective_scale.store(0, std::memory_order_relaxed);
        fallback_.base_index.store(detail::fallback_window_base, std::memory_order_relaxed);
        static_cast<void>(detail::bucket_lower_edge(0, scale));
    }

    /// @brief Registry harvest hook: fill `record.histogram` from the instrument.
    ///
    /// @param object Type-erased pointer to this instrument.
    /// @param record Record to populate in place.
    /// @note Spec `metrics/metric-provider`, requirement "Background harvesting
    ///       and collection period".
    static void harvest_thunk(void* object, MetricRecord& record) noexcept {
        const auto& self = *static_cast<const ExponentialHistogram*>(object);
        record.histogram = self.read();
    }

    /// @brief Map a value into the bank's window, coarsening when it does not fit.
    ///
    /// Anchors the window on the first sample. A position inside the 256 inline
    /// buckets is recorded directly; otherwise the bank coarsens and retries, and
    /// at scale 0 the sample folds into the nearest edge bucket so totals stay
    /// exact.
    ///
    /// @param bank Bank to receive the sample.
    /// @param value Positive finite sample value.
    /// @note Spec `metrics/exponential-histogram`, requirement "Bounded bucket
    ///       storage and coarsening".
    void apply_to_bank(detail::ExponentialBank& bank, double value) const noexcept {
        for (int guard = 0; guard < 64; ++guard) {
            const std::int32_t scale = bank.effective_scale.load(std::memory_order_relaxed);
            const std::int64_t base = bank.base_index.load(std::memory_order_relaxed);
            const std::int64_t index = detail::exponential_index(value, scale);
            if (base == detail::unanchored_index) {
                bank.base_index.store(index, std::memory_order_relaxed);
                bank.apply(value, 0U);
                return;
            }
            const std::int64_t position = index - base;
            if (position >= 0 &&
                position < static_cast<std::int64_t>(detail::max_exponential_buckets)) {
                bank.apply(value, static_cast<std::size_t>(position));
                return;
            }
            if (scale <= 0) {
                // Cannot coarsen any further: fold into the nearest edge so the
                // sample still counts (bucket totals stay exact).
                const std::size_t edge =
                    (position < 0) ? 0U : (detail::max_exponential_buckets - 1U);
                bank.apply(value, edge);
                return;
            }
            bank.coarsen(index);
        }
    }

    /// @brief Lowest bucket index in play, expressed at the merged scale.
    ///
    /// Scans the live bank(s) and, when no recorded slot is anchored, the
    /// fallback bank's fixed scale-0 frame, so all banks share one rendered axis.
    ///
    /// @param merged_scale Coarsest scale among the contributing banks.
    /// @return The lowest rebased index, or `detail::unanchored_index` when
    ///         nothing has been recorded.
    [[nodiscard]] std::int64_t compute_merged_base(std::int32_t merged_scale) const noexcept {
        std::int64_t lowest = detail::unanchored_index;
        const auto consider = [&](const detail::ExponentialBank& bank) {
            const std::int64_t base = bank.base_index.load(std::memory_order_relaxed);
            if (base == detail::unanchored_index) {
                return;
            }
            const std::int32_t scale = bank.effective_scale.load(std::memory_order_relaxed);
            const std::int32_t delta = scale - merged_scale;
            const std::int64_t here =
                (delta > 0) ? detail::floor_div_pow2(base, static_cast<std::uint32_t>(delta))
                            : (base << (-delta));
            if (lowest == detail::unanchored_index || here < lowest) {
                lowest = here;
            }
        };
        if constexpr (Concurrency == ConcurrencyPolicy::SingleThreaded) {
            consider(cell_);
        } else {
            for (std::size_t i = 0; i < detail::shard_count; ++i) {
                consider(slots_[i].bank[0]);
                consider(slots_[i].bank[1]);
            }
        }
        // The fallback cell's fixed scale-0 frame only anchors the merged
        // window when no recorded slot provides a base; otherwise its counts
        // fold into the edge buckets of the slots' window (documented
        // precision degradation beyond MaxThreads).
        if (lowest == detail::unanchored_index) {
            consider(fallback_);
        }
        return lowest;
    }

    /// @brief Merge one bank without consuming it (Cumulative fallback path).
    ///
    /// @param bank Bank to copy and absorb.
    /// @param merged Accumulator receiving the rebased buckets and summary.
    /// @param merged_scale Scale of the merged frame.
    void merge_bank(detail::ExponentialBank& bank, detail::ExponentialMerge& merged,
                    std::int32_t merged_scale) const noexcept {
        detail::ExponentialBank::merge_view(bank.load_view(), merged, merged_scale);
    }

    /// @brief Merge one recording slot under the sequence handshake (design D9).
    ///
    /// The bank is copied into a plain view and, for `ResetOnRead`, cleared only
    /// while the sequence proves no producer is mid-record. A stalled slot is
    /// deferred to a later window with its contents untouched, so every record
    /// lands in exactly one window.
    ///
    /// @param index Recording-slot index in `[0, detail::shard_count)`.
    /// @param merged Accumulator receiving the rebased buckets and summary.
    /// @param merged_scale Scale of the merged frame.
    /// @note Spec `metrics/exponential-histogram`, requirement "Exponential
    ///       histogram reset windows".
    /// @note Spec `metrics/exponential-histogram`, requirement "Multi-threaded
    ///       exponential histogram recording".
    void merge_slot(std::size_t index, detail::ExponentialMerge& merged,
                    std::int32_t merged_scale) const noexcept {
        auto& slot = slots_[index];
        const bool consuming = (Reset == ResetPolicy::ResetOnRead);
        std::size_t bank_index = slot.active.load(std::memory_order_relaxed);
        if (consuming) {
            const std::uint8_t current = static_cast<std::uint8_t>(bank_index);
            bank_index = slot.active.exchange(static_cast<std::uint8_t>(current ^ 1U),
                                              std::memory_order_acq_rel);
        }
        for (int attempt = 0; attempt < detail::exponential_handshake_attempts; ++attempt) {
            const std::uint32_t before = slot.seq.load(std::memory_order_acquire);
            if ((before & 1U) != 0U) {
                continue; // a producer is mid-record
            }
            const detail::ExponentialBank::View view = slot.bank[bank_index].load_view();
            // Keep the bank reads inside the checked window: compiler barrier
            // only (x86 TSO already orders loads; design D9).
            std::atomic_signal_fence(std::memory_order_acq_rel);
            if (slot.seq.load(std::memory_order_acquire) == before) {
                if (consuming) {
                    // Safe to clear: post-exchange producers write the other
                    // bank, and the stable check proved no straggler is live.
                    slot.bank[bank_index].clear();
                }
                detail::ExponentialBank::merge_view(view, merged, merged_scale);
                return;
            }
        }
        // Deferred to a later window: the bank is left untouched (no loss).
    }

    /// @brief Fill the four percentiles from the rendered bucket table.
    ///
    /// Leaves them at zero when no samples were recorded.
    ///
    /// @param snapshot Snapshot whose buckets, zero count and summary are read
    ///        and whose `p50`/`p90`/`p99`/`p999` fields are written.
    /// @note Spec `metrics/exponential-histogram`, requirement "Exponential
    ///       histogram percentile snapshots".
    void compute_percentiles(HistogramSnapshot& snapshot) const noexcept {
        if (snapshot.count == 0U) {
            return;
        }
        const auto rank_of = [&snapshot](double quantile) {
            return quantile * static_cast<double>(snapshot.count);
        };
        snapshot.p50 = percentile_of(snapshot, rank_of(0.50));
        snapshot.p90 = percentile_of(snapshot, rank_of(0.90));
        snapshot.p99 = percentile_of(snapshot, rank_of(0.99));
        snapshot.p999 = percentile_of(snapshot, rank_of(0.999));
    }

    /// @brief Interpolate one quantile from the bucket table.
    ///
    /// Zeros rank first and report `0.0`; a positive sample reports the midpoint
    /// between its bucket's lower edge and the next bucket's lower edge.
    ///
    /// @param snapshot Merged snapshot supplying buckets, zero count and max.
    /// @param rank Absolute rank in `(0, count]`, not a fraction.
    /// @return Representative value, or `snapshot.max` when no bucket matches.
    /// @note Spec `metrics/exponential-histogram`, requirement "Exponential
    ///       histogram percentile snapshots".
    [[nodiscard]] double percentile_of(const HistogramSnapshot& snapshot,
                                       double rank) const noexcept {
        if (snapshot.zero_count > 0U && rank <= static_cast<double>(snapshot.zero_count)) {
            return 0.0;
        }
        std::uint64_t cumulative = snapshot.zero_count;
        for (std::uint32_t i = 0; i < snapshot.bucket_count; ++i) {
            const std::uint64_t in_bucket = snapshot.buckets_count[i];
            if (in_bucket == 0U) {
                continue;
            }
            if (static_cast<double>(cumulative + in_bucket) >= rank) {
                const double lower = snapshot.buckets_lower[i];
                const double upper =
                    (i + 1U < snapshot.bucket_count) ? snapshot.buckets_lower[i + 1U] : lower;
                return (lower + upper) * 0.5;
            }
            cumulative += in_bucket;
        }
        return snapshot.max;
    }

    /// @brief Identity of the owning registry slot (set by the Meter).
    const MetricIdentity* identity_{nullptr};

    /// @brief Configured (finest) scale reported by `to_string()` and snapshots.
    std::int32_t scale_{0};

    /// @brief SingleThreaded recording bank.
    mutable detail::ExponentialBank cell_{};
    /// @brief MultiThreaded per-thread recording slots.
    mutable detail::ExponentialSlot slots_[detail::shard_count]{};
    /// @brief Shared fixed-scale fallback bank beyond the shard pool.
    mutable detail::ExponentialBank fallback_{};
    /// @brief Count of rejected negative/non-finite samples.
    mutable std::atomic<std::uint64_t> ignored_{0};
    /// @brief Count of accepted samples at exactly zero (both signed zeros).
    mutable std::atomic<std::uint64_t> zero_count_{0};
};

static_assert(sizeof(ExponentialHistogram<ConcurrencyPolicy::MultiThreaded, AccessPolicy::Readable,
                                          ResetPolicy::ResetOnRead>) <=
                  detail::max_exponential_histogram_size,
              "exponential histogram must fit the registry budget");
static_assert(
    concepts::Metric<ExponentialHistogram<ConcurrencyPolicy::SingleThreaded, AccessPolicy::Readable,
                                          ResetPolicy::Cumulative>>,
    "ExponentialHistogram must satisfy the Metric concept");
static_assert(
    concepts::ReadableMetric<ExponentialHistogram<ConcurrencyPolicy::SingleThreaded,
                                                  AccessPolicy::Readable, ResetPolicy::Cumulative>>,
    "a Readable exponential histogram must satisfy the ReadableMetric concept");
static_assert(
    !concepts::ReadableMetric<ExponentialHistogram<
        ConcurrencyPolicy::SingleThreaded, AccessPolicy::WriteOnly, ResetPolicy::Cumulative>>,
    "a WriteOnly exponential histogram must NOT satisfy the ReadableMetric concept");
static_assert(
    !detail::HasRead<ExponentialHistogram<ConcurrencyPolicy::SingleThreaded,
                                          AccessPolicy::WriteOnly, ResetPolicy::Cumulative>>,
    "a WriteOnly exponential histogram must not expose read()");
static_assert(
    !detail::HasSnapshot<ExponentialHistogram<ConcurrencyPolicy::SingleThreaded,
                                              AccessPolicy::WriteOnly, ResetPolicy::Cumulative>>,
    "a WriteOnly exponential histogram must not expose snapshot()");

/// @brief Free `to_string` overload mirroring the member (project mandate 9).
///
/// @tparam Concurrency Recording policy of the instrument.
/// @tparam Access Access policy of the instrument.
/// @tparam Reset Reset policy of the instrument.
/// @param histogram Instrument to render.
/// @return The same diagnostic text as `histogram.to_string()`.
/// @note Spec `metrics/exponential-histogram`, requirement "Exponential
///       histogram serialization contract".
template <nexigen::metrics::ConcurrencyPolicy Concurrency, nexigen::metrics::AccessPolicy Access,
          nexigen::metrics::ResetPolicy Reset>
[[nodiscard]] std::string
to_string(const nexigen::metrics::ExponentialHistogram<Concurrency, Access, Reset>& histogram) {
    return histogram.to_string();
}

} // namespace nexigen::metrics

/// @brief `std::formatter` that renders a histogram as its diagnostic string.
///
/// Inherits the `std::string_view` formatter, so format specs apply to the
/// rendered text (project mandate 11).
///
/// @tparam Concurrency Recording policy of the instrument.
/// @tparam Access Access policy of the instrument.
/// @tparam Reset Reset policy of the instrument.
/// @note Spec `metrics/exponential-histogram`, requirement "Exponential
///       histogram serialization contract".
template <nexigen::metrics::ConcurrencyPolicy Concurrency, nexigen::metrics::AccessPolicy Access,
          nexigen::metrics::ResetPolicy Reset>
struct std::formatter<nexigen::metrics::ExponentialHistogram<Concurrency, Access, Reset>>
    : std::formatter<std::string_view> {
    /// @brief Format the instrument by delegating to its `to_string()`.
    ///
    /// @tparam Ctx Format context type supplied by `std::format`.
    /// @param histogram Instrument to render.
    /// @param ctx Output context to append the text to.
    /// @return Iterator past the last character written.
    /// @note Project mandate 11: `std::formatter` support for diagnostics.
    template <class Ctx>
    auto format(const nexigen::metrics::ExponentialHistogram<Concurrency, Access, Reset>& histogram,
                Ctx& ctx) const {
        const std::string text = histogram.to_string();
        return std::formatter<std::string_view>::format(text, ctx);
    }
};
