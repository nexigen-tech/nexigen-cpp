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
#include <nexigen/metrics/detail/layout.hpp>
#include <nexigen/metrics/detail/probes.hpp>
#include <nexigen/metrics/detail/thread_slot.hpp>
#include <nexigen/metrics/metric_snapshot.hpp>
#include <nexigen/metrics/policy_enums.hpp>

namespace nexigen::metrics {

template <nexigen::metrics::ConcurrencyPolicy Concurrency, nexigen::metrics::AccessPolicy Access,
          nexigen::metrics::ResetPolicy Reset>
class ExponentialHistogram;

/// Construction guard: only a Meter may create instruments.
class MeterPasskey;

namespace detail {

/// Number of buckets kept per recording slot.
inline constexpr std::size_t max_exponential_buckets = 256;

/// Highest supported scale (finest resolution).
inline constexpr std::int32_t max_exponential_scale = 20;

/// Sentinel for "the window has not been anchored yet".
inline constexpr std::int64_t unanchored_index = std::numeric_limits<std::int64_t>::min();

/// Lower octave of the shared fallback window: the fixed scale-0 bucket range
/// [fallback_window_base, fallback_window_base + 256) covers octaves
/// 2^-128 .. 2^128; more extreme values fold into the edge buckets.
inline constexpr std::int64_t fallback_window_base = -128;

/// Floor division by a power of two (portable, no implementation-defined
/// right shift of negative values).
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

/// Lower edge of bucket i at the given scale: B(i) = 2^(i / 2^scale), the
/// exact geometric boundary for base = 2^(2^-scale) (design D11). One shared
/// 2^20-entry table serves every scale: factor_s[r] = master[r << (20 - s)],
/// so boundary values agree bit-for-bit with std::pow(2.0, r / 2^s). The
/// table is generated once on the first exponential-histogram creation
/// (cold path, static storage — zero heap allocation; demand-paged, so a
/// scale-10 user touches only 8 KiB of it).
inline constexpr std::int32_t boundary_master_bits = 20;
inline constexpr std::size_t boundary_master_size = std::size_t{1} << boundary_master_bits;

[[nodiscard]] inline const double* boundary_master() noexcept {
    static const std::array<double, boundary_master_size> table = [] {
        std::array<double, boundary_master_size> out{};
        for (std::size_t r = 0; r < boundary_master_size; ++r) {
            out[r] = std::pow(2.0, static_cast<double>(r) /
                                       static_cast<double>(boundary_master_size));
        }
        return out;
    }();
    return table.data();
}

[[nodiscard]] inline double bucket_lower_edge(std::int64_t index, std::int32_t scale) noexcept {
    const std::int32_t s =
        (scale < 0) ? 0 : ((scale > boundary_master_bits) ? boundary_master_bits : scale);
    const std::int64_t quotient = floor_div_pow2(index, static_cast<std::uint32_t>(s));
    const std::int64_t remainder = index - (quotient << s); // 0 <= remainder < 2^s
    return std::ldexp(boundary_master()[static_cast<std::size_t>(remainder)
                                        << (boundary_master_bits - s)],
                      static_cast<int>(quotient));
}

/// Bucket index of a positive finite value at the given scale: the exact
/// floor(2^scale * log2(v)), so bucket i covers [B(i), B(i+1)) (design D11).
/// Pure bit extraction plus a bounded boundary correction — no std::log,
/// std::log2 or allocation per record (spec metrics/exponential-histogram
/// "Bitwise bucket index mapping").
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
    const std::int64_t exponent =
        static_cast<std::int64_t>((bits >> 52U) & 0x7FFU) - 1023;
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
    std::int64_t index = (exponent << scale) +
                         static_cast<std::int64_t>(approx < 0.0 ? approx - 1.0 : approx);

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

/// One exponential bank: buckets, window and running summary.
struct alignas(64) Exponential_bank {
    /// Plain copy of one bank's contents (harvest side).
    struct View {
        std::uint64_t buckets[max_exponential_buckets]{};
        std::uint64_t count{0};
        double sum{0.0};
        double minimum{0.0};
        double maximum{0.0};
        std::int32_t effective_scale{0};
        std::int64_t base_index{unanchored_index};
    };

    std::atomic<std::uint64_t> buckets[max_exponential_buckets]{};
    std::atomic<std::uint64_t> count{0};
    std::atomic<std::uint64_t> zero_count{0};
    std::atomic<double> sum{0.0};
    std::atomic<double> minimum{std::numeric_limits<double>::infinity()};
    std::atomic<double> maximum{-std::numeric_limits<double>::infinity()};
    std::atomic<std::int64_t> base_index{unanchored_index};
    std::atomic<std::int32_t> effective_scale{0};

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

    /// Non-consuming copy (single-writer banks, under the seq handshake).
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

    /// Consuming copy for the multi-writer fallback cell: per-field RMW
    /// exchanges land each update in exactly one window (record-level
    /// straddle across fields is documented; design D15).
    [[nodiscard]] View take_view() noexcept {
        View view{};
        for (std::size_t i = 0; i < max_exponential_buckets; ++i) {
            view.buckets[i] = buckets[i].exchange(0U, std::memory_order_relaxed);
        }
        view.count = count.exchange(0U, std::memory_order_relaxed);
        view.sum = sum.exchange(0.0, std::memory_order_relaxed);
        view.minimum = minimum.exchange(std::numeric_limits<double>::infinity(),
                                        std::memory_order_relaxed);
        view.maximum = maximum.exchange(-std::numeric_limits<double>::infinity(),
                                        std::memory_order_relaxed);
        view.effective_scale = effective_scale.load(std::memory_order_relaxed);
        view.base_index = base_index.load(std::memory_order_relaxed);
        return view;
    }

    /// Fold one bank view into the merged frame (rebase onto the merged
    /// scale, folding out-of-window buckets into the edge buckets).
    static void merge_view(const View& view, struct Exponential_merge& merged,
                           std::int32_t merged_scale) noexcept;

    /// Fold one sample in (exclusive slot — single writer).
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

    /// Fold one sample in at a fixed window position (shared fallback cell —
    /// several writers; the caller derives the position from the octave, so
    /// the window never coarsens and totals stay exact under contention).
    void apply_multi_at(std::size_t position, double value) noexcept {
        buckets[position].fetch_add(1U, std::memory_order_relaxed);
        count.fetch_add(1U, std::memory_order_relaxed);
        sum.fetch_add(value, std::memory_order_relaxed);
        double current = minimum.load(std::memory_order_relaxed);
        while (value < current &&
               !minimum.compare_exchange_weak(current, value, std::memory_order_relaxed,
                                              std::memory_order_relaxed)) {
        }
        current = maximum.load(std::memory_order_relaxed);
        while (value > current &&
               !maximum.compare_exchange_weak(current, value, std::memory_order_relaxed,
                                              std::memory_order_relaxed)) {
        }
    }

    /// Halve the resolution: buckets collapse pairwise, the window widens.
    void coarsen(std::int64_t lowest_index) noexcept {
        const std::int32_t scale = effective_scale.load(std::memory_order_relaxed);
        const std::int64_t base = base_index.load(std::memory_order_relaxed);
        const std::int64_t anchor = (base == unanchored_index) ? lowest_index : ((base < lowest_index) ? base : lowest_index);
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
                if (position >= 0 && position < static_cast<std::int64_t>(max_exponential_buckets)) {
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

/// A recording slot: double-buffered banks plus the sequence handshake.
struct alignas(64) Exponential_slot {
    Exponential_bank bank[2];
    std::atomic<std::uint32_t> seq{0};
    std::atomic<std::uint8_t> active{0};
};

inline constexpr int exponential_handshake_attempts = 64;

/// Merged view of the exponential buckets at a common scale.
struct Exponential_merge {
    std::uint64_t buckets[max_exponential_buckets]{};
    std::uint64_t total{0};
    std::uint64_t zeros{0};
    double sum{0.0};
    double minimum{0.0};
    double maximum{0.0};
    std::int32_t scale{0};
    std::int64_t base{unanchored_index};
    bool empty{true};
};

/// Fold one bank view into the merged frame: rebase the bank's index space
/// onto the merged scale (finer banks shift down, the scale-0 fallback up),
/// folding out-of-window buckets into the edge buckets.
inline void Exponential_bank::merge_view(const View& view, Exponential_merge& merged,
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
            if (view.minimum < merged.minimum) { merged.minimum = view.minimum; }
            if (view.maximum > merged.maximum) { merged.maximum = view.maximum; }
        }
    }
}

} // namespace detail

/// An exponentially bucketed histogram. Constructible only through a Meter.
template <ConcurrencyPolicy Concurrency = ConcurrencyPolicy::SingleThreaded,
          AccessPolicy Access = AccessPolicy::Readable,
          ResetPolicy Reset = ResetPolicy::Cumulative>
class ExponentialHistogram {
public:
    static constexpr MetricKind kind = MetricKind::ExponentialHistogram;
    static constexpr ConcurrencyPolicy concurrency = Concurrency;
    static constexpr AccessPolicy access = Access;
    static constexpr ResetPolicy reset = Reset;

    ExponentialHistogram(const ExponentialHistogram&) = delete;
    ExponentialHistogram& operator=(const ExponentialHistogram&) = delete;
    ExponentialHistogram(ExponentialHistogram&&) = delete;
    ExponentialHistogram& operator=(ExponentialHistogram&&) = delete;

    /// Record one sample. Positive finite values land in a bucket, zeros are
    /// counted separately (they rank below every bucket), negative and
    /// non-finite values are ignored (and counted).
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
                // Shared fallback shard (beyond Max_threads): bucketed at the
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

    /// Current snapshot (buckets, summary, percentiles). Available only for
    /// Readable instruments.
    [[nodiscard]] HistogramSnapshot read() const requires (Access == AccessPolicy::Readable) {
        // Pass 1: the coarsest scale in play — the merged frame lives there so
        // every slot's buckets land on a common axis.
        std::int32_t merged_scale = scale_;
        if constexpr (Concurrency == ConcurrencyPolicy::SingleThreaded) {
            merged_scale = cell_.effective_scale.load(std::memory_order_relaxed);
        } else {
            for (std::size_t i = 0; i < detail::shard_count; ++i) {
                const std::int32_t first = slots_[i].bank[0].effective_scale.load(std::memory_order_relaxed);
                const std::int32_t second = slots_[i].bank[1].effective_scale.load(std::memory_order_relaxed);
                const std::int32_t slot_scale = (first < second) ? first : second;
                if (slot_scale < merged_scale) {
                    merged_scale = slot_scale;
                }
            }
        }

        detail::Exponential_merge merged{};
        merged.scale = merged_scale;
        merged.base = compute_merged_base(merged_scale);
        if constexpr (Concurrency == ConcurrencyPolicy::SingleThreaded) {
            const detail::Exponential_bank::View cell_view = cell_.load_view();
            if (Reset == ResetPolicy::ResetOnRead) {
                cell_.clear();
            }
            detail::Exponential_bank::merge_view(cell_view, merged, merged_scale);
        } else {
            for (std::size_t i = 0; i < detail::shard_count; ++i) {
                merge_slot(i, merged, merged_scale);
            }
        }

        const bool consuming = (Reset == ResetPolicy::ResetOnRead);
        // The shared fallback shard lives in a fixed scale-0 window and is
        // rebased onto the merged frame by merge_bank.
        detail::Exponential_bank::merge_view(consuming ? fallback_.take_view()
                                                        : fallback_.load_view(),
                                             merged, merged_scale);
        const std::uint64_t zeros = consuming
            ? zero_count_.exchange(0U, std::memory_order_relaxed)
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

    /// Full record for this instrument (identity, classification, values).
    [[nodiscard]] MetricRecord snapshot() const requires (Access == AccessPolicy::Readable) {
        MetricRecord record{};
        record.kind = kind;
        record.concurrency = concurrency;
        record.access = access;
        record.reset = reset;
        record.histogram = read();
        if (identity_ != nullptr) {
            record.identity = *identity_;
        }
        return record;
    }

    /// Cold-path diagnostic (mandate 9). Never resets a window.
    [[nodiscard]] std::string to_string() const {
        std::string out = "ExponentialHistogram(kind=";
        out += std::string(metrics::to_string(kind));
        out += ", concurrency=" + std::string(metrics::to_string(concurrency));
        out += ", access=" + std::string(metrics::to_string(access));
        out += ", reset=" + std::string(metrics::to_string(reset));
        out += ", scale=" + std::to_string(scale_);
        out += ")";
        return out;
    }

    /// Diagnostic JSON (cold path). The instrument is NOT deserializable: its
    /// construction is Meter-only.
    friend void to_json(nlohmann::json& j, const ExponentialHistogram& histogram) {
        j = nlohmann::json{
            {"kind", std::string(metrics::to_string(ExponentialHistogram::kind))},
            {"concurrency", std::string(metrics::to_string(ExponentialHistogram::concurrency))},
            {"access", std::string(metrics::to_string(ExponentialHistogram::access))},
            {"reset", std::string(metrics::to_string(ExponentialHistogram::reset))},
            {"scale", histogram.scale_},
        };
    }

private:
    friend class Meter;
    friend class MetricProvider;

    explicit ExponentialHistogram(const MeterPasskey&, std::int32_t scale) noexcept : scale_(scale) {
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

    static void harvest_thunk(void* object, MetricRecord& record) noexcept {
        const auto& self = *static_cast<const ExponentialHistogram*>(object);
        record.histogram = self.read();
    }

    static void destroy_thunk(void* object) noexcept {
        static_cast<ExponentialHistogram*>(object)->~ExponentialHistogram();
    }

    /// Map the value into the bank's window, coarsening when it does not fit.
    void apply_to_bank(detail::Exponential_bank& bank, double value) const noexcept {
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
            if (position >= 0 && position < static_cast<std::int64_t>(detail::max_exponential_buckets)) {
                bank.apply(value, static_cast<std::size_t>(position));
                return;
            }
            if (scale <= 0) {
                // Cannot coarsen any further: fold into the nearest edge so the
                // sample still counts (bucket totals stay exact).
                const std::size_t edge = (position < 0) ? 0U : (detail::max_exponential_buckets - 1U);
                bank.apply(value, edge);
                return;
            }
            bank.coarsen(index);
        }
    }

    /// The lowest bucket index in play, expressed at the merged scale.
    [[nodiscard]] std::int64_t compute_merged_base(std::int32_t merged_scale) const noexcept {
        std::int64_t lowest = detail::unanchored_index;
        const auto consider = [&](const detail::Exponential_bank& bank) {
            const std::int64_t base = bank.base_index.load(std::memory_order_relaxed);
            if (base == detail::unanchored_index) {
                return;
            }
            const std::int32_t scale = bank.effective_scale.load(std::memory_order_relaxed);
            const std::int32_t delta = scale - merged_scale;
            const std::int64_t here = (delta > 0)
                                          ? detail::floor_div_pow2(base, static_cast<std::uint32_t>(delta))
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
        // precision degradation beyond Max_threads).
        if (lowest == detail::unanchored_index) {
            consider(fallback_);
        }
        return lowest;
    }

    /// Merge one bank without consuming it (Cumulative paths).
    void merge_bank(detail::Exponential_bank& bank, detail::Exponential_merge& merged,
                    std::int32_t merged_scale) const noexcept {
        detail::Exponential_bank::merge_view(bank.load_view(), merged, merged_scale);
    }

    /// Merge one slot under the seq handshake (design D9): the bank is copied
    /// into a plain view and — for ResetOnRead — cleared only while the
    /// sequence proves no producer is mid-record. A stalled slot is deferred
    /// to a later window with its contents untouched, so every record lands
    /// in exactly one window.
    void merge_slot(std::size_t index, detail::Exponential_merge& merged,
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
            const detail::Exponential_bank::View view = slot.bank[bank_index].load_view();
            // Keep the bank reads inside the checked window: compiler barrier
            // only (x86 TSO already orders loads; design D9).
            std::atomic_signal_fence(std::memory_order_acq_rel);
            if (slot.seq.load(std::memory_order_acquire) == before) {
                if (consuming) {
                    // Safe to clear: post-exchange producers write the other
                    // bank, and the stable check proved no straggler is live.
                    slot.bank[bank_index].clear();
                }
                detail::Exponential_bank::merge_view(view, merged, merged_scale);
                return;
            }
        }
        // Deferred to a later window: the bank is left untouched (no loss).
    }

    /// Percentiles from the bucket table (bucket midpoints, zeros first).
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

    [[nodiscard]] double percentile_of(const HistogramSnapshot& snapshot, double rank) const noexcept {
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
                const double upper = (i + 1U < snapshot.bucket_count)
                                         ? snapshot.buckets_lower[i + 1U]
                                         : lower;
                return (lower + upper) * 0.5;
            }
            cumulative += in_bucket;
        }
        return snapshot.max;
    }

    /// Identity of the owning registry slot (set by the Meter at creation).
    const MetricIdentity* identity_{nullptr};

    std::int32_t scale_{0};

    mutable detail::Exponential_bank cell_{};                    // SingleThreaded
    mutable detail::Exponential_slot slots_[detail::shard_count]{}; // MultiThreaded
    mutable detail::Exponential_bank fallback_{};               // shared >32-thread cell
    mutable std::atomic<std::uint64_t> ignored_{0};
    mutable std::atomic<std::uint64_t> zero_count_{0};
};

static_assert(sizeof(ExponentialHistogram<ConcurrencyPolicy::MultiThreaded, AccessPolicy::Readable,
                                           ResetPolicy::ResetOnRead>) <=
                  detail::max_exponential_histogram_size,
              "exponential histogram must fit the registry budget");
static_assert(concepts::Metric<ExponentialHistogram<ConcurrencyPolicy::SingleThreaded,
                                                     AccessPolicy::Readable,
                                                     ResetPolicy::Cumulative>>,
              "ExponentialHistogram must satisfy the Metric concept");
static_assert(concepts::ReadableMetric<ExponentialHistogram<ConcurrencyPolicy::SingleThreaded,
                                                             AccessPolicy::Readable,
                                                             ResetPolicy::Cumulative>>,
              "a Readable exponential histogram must satisfy the ReadableMetric concept");
static_assert(!concepts::ReadableMetric<ExponentialHistogram<ConcurrencyPolicy::SingleThreaded,
                                                              AccessPolicy::WriteOnly,
                                                              ResetPolicy::Cumulative>>,
              "a WriteOnly exponential histogram must NOT satisfy the ReadableMetric concept");
static_assert(!detail::HasRead<ExponentialHistogram<ConcurrencyPolicy::SingleThreaded,
                                                    AccessPolicy::WriteOnly,
                                                    ResetPolicy::Cumulative>>,
              "a WriteOnly exponential histogram must not expose read()");
static_assert(!detail::HasSnapshot<ExponentialHistogram<ConcurrencyPolicy::SingleThreaded,
                                                        AccessPolicy::WriteOnly,
                                                        ResetPolicy::Cumulative>>,
              "a WriteOnly exponential histogram must not expose snapshot()");

/// Free helper (mandate 9): mirrors the member.
template <nexigen::metrics::ConcurrencyPolicy Concurrency, nexigen::metrics::AccessPolicy Access,
          nexigen::metrics::ResetPolicy Reset>
[[nodiscard]] std::string to_string(
    const nexigen::metrics::ExponentialHistogram<Concurrency, Access, Reset>& histogram) {
    return histogram.to_string();
}

} // namespace nexigen::metrics


/// Exponential histograms format as their diagnostic string (mandate 11).
template <nexigen::metrics::ConcurrencyPolicy Concurrency, nexigen::metrics::AccessPolicy Access,
          nexigen::metrics::ResetPolicy Reset>
struct std::formatter<nexigen::metrics::ExponentialHistogram<Concurrency, Access, Reset>>
    : std::formatter<std::string_view> {
    template <class Ctx>
    auto format(const nexigen::metrics::ExponentialHistogram<Concurrency, Access, Reset>& histogram,
                Ctx& ctx) const {
        const std::string text = histogram.to_string();
        return std::formatter<std::string_view>::format(text, ctx);
    }
};
