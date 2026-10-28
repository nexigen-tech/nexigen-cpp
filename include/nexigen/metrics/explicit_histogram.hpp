// nexigen::metrics — the explicit-bucket histogram (design D10;
// spec metrics/explicit-histogram).
//
// ExplicitBucketHistogram<Concurrency, Access, Reset> counts samples into
// caller-defined buckets. The upper bounds are fixed at creation time and
// stored in an inline array (at most 64 bounds); bucket i covers
// (bounds[i-1], bounds[i]], bucket 0 covers (-inf, bounds[0]], and a final
// overflow bucket covers everything above the last bound.
//
// Storage mirrors the counter: a plain cell for SingleThreaded, 32 exclusive
// per-thread slots plus a shared fallback cell for MultiThreaded. Every
// bucket count, the total, the sum, the minimum and the maximum are relaxed
// atomics inside a bank; the sequence handshake keeps a harvest consistent
// while a producer records, and the double bank makes a ResetOnRead harvest
// swap the window without losing a concurrent sample.
//
// The shared fallback cell is written by several threads at once, so its
// updates are atomic RMWs (fetch_add / CAS loops) instead of the exclusive
// slots' plain load/store pairs.
#pragma once

#include <atomic>
#include <cmath>
#include <cstdint>
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
class ExplicitBucketHistogram;

/// Construction guard: only a Meter may create instruments.
class MeterPasskey;

namespace detail {

/// Maximum number of explicit bucket upper bounds.
inline constexpr std::size_t max_explicit_bounds = 64;

/// One histogram bank: bucket counts plus the running summary.
struct alignas(64) Histogram_bank {
    /// Plain copy of one bank's contents (harvest side).
    struct View {
        std::uint64_t buckets[max_explicit_bounds + 1]{};
        std::uint64_t count{0};
        double sum{0.0};
        double minimum{0.0};
        double maximum{0.0};
    };

    std::atomic<std::uint64_t> buckets[max_explicit_bounds + 1]{};
    std::atomic<std::uint64_t> count{0};
    std::atomic<double> sum{0.0};
    std::atomic<double> minimum{std::numeric_limits<double>::infinity()};
    std::atomic<double> maximum{-std::numeric_limits<double>::infinity()};

    void clear() noexcept {
        for (std::size_t i = 0; i < max_explicit_bounds + 1U; ++i) {
            buckets[i].store(0, std::memory_order_relaxed);
        }
        count.store(0, std::memory_order_relaxed);
        sum.store(0.0, std::memory_order_relaxed);
        minimum.store(std::numeric_limits<double>::infinity(), std::memory_order_relaxed);
        maximum.store(-std::numeric_limits<double>::infinity(), std::memory_order_relaxed);
    }

    /// Non-consuming copy (single-writer banks, under the seq handshake).
    [[nodiscard]] View load_view() const noexcept {
        View view{};
        for (std::size_t i = 0; i < max_explicit_bounds + 1U; ++i) {
            view.buckets[i] = buckets[i].load(std::memory_order_relaxed);
        }
        view.count = count.load(std::memory_order_relaxed);
        view.sum = sum.load(std::memory_order_relaxed);
        view.minimum = minimum.load(std::memory_order_relaxed);
        view.maximum = maximum.load(std::memory_order_relaxed);
        return view;
    }

    /// Consuming copy for the multi-writer fallback cell: per-field RMW
    /// exchanges land each update in exactly one window (design D15).
    [[nodiscard]] View take_view() noexcept {
        View view{};
        for (std::size_t i = 0; i < max_explicit_bounds + 1U; ++i) {
            view.buckets[i] = buckets[i].exchange(0U, std::memory_order_relaxed);
        }
        view.count = count.exchange(0U, std::memory_order_relaxed);
        view.sum = sum.exchange(0.0, std::memory_order_relaxed);
        view.minimum = minimum.exchange(std::numeric_limits<double>::infinity(),
                                        std::memory_order_relaxed);
        view.maximum = maximum.exchange(-std::numeric_limits<double>::infinity(),
                                        std::memory_order_relaxed);
        return view;
    }

    /// Fold one sample in (exclusive slot — single writer).
    void apply(double value, std::size_t bucket) noexcept {
        const std::uint64_t seen = count.load(std::memory_order_relaxed);
        buckets[bucket].store(buckets[bucket].load(std::memory_order_relaxed) + 1U,
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

    /// Fold one sample in (shared fallback cell — several writers).
    void apply_multi(double value, std::size_t bucket) noexcept {
        buckets[bucket].fetch_add(1U, std::memory_order_relaxed);
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
};

/// A recording slot: double-buffered banks plus the sequence handshake.
struct alignas(64) Histogram_slot {
    Histogram_bank bank[2];
    std::atomic<std::uint32_t> seq{0};
    std::atomic<std::uint8_t> active{0};
};

inline constexpr int histogram_handshake_attempts = 64;

/// Merged view of one or more banks.
struct Histogram_merge {
    std::uint64_t buckets[max_explicit_bounds + 1]{};
    std::uint64_t total{0};
    double sum{0.0};
    double minimum{0.0};
    double maximum{0.0};
    bool empty{true};

    void absorb_view(const detail::Histogram_bank::View& view) noexcept {
        for (std::size_t i = 0; i < max_explicit_bounds + 1U; ++i) {
            buckets[i] += view.buckets[i];
        }
        total += view.count;
        sum += view.sum;
        const std::uint64_t seen = view.count;
        if (seen == 0U) {
            return;
        }
        const double bank_min = view.minimum;
        const double bank_max = view.maximum;
        if (empty) {
            minimum = bank_min;
            maximum = bank_max;
            empty = false;
        } else {
            if (bank_min < minimum) { minimum = bank_min; }
            if (bank_max > maximum) { maximum = bank_max; }
        }
    }
};

} // namespace detail

/// A histogram with caller-defined buckets. Constructible only through a Meter.
template <ConcurrencyPolicy Concurrency = ConcurrencyPolicy::SingleThreaded,
          AccessPolicy Access = AccessPolicy::Readable,
          ResetPolicy Reset = ResetPolicy::Cumulative>
class ExplicitBucketHistogram {
public:
    static constexpr MetricKind kind = MetricKind::ExplicitHistogram;
    static constexpr ConcurrencyPolicy concurrency = Concurrency;
    static constexpr AccessPolicy access = Access;
    static constexpr ResetPolicy reset = Reset;

    ExplicitBucketHistogram(const ExplicitBucketHistogram&) = delete;
    ExplicitBucketHistogram& operator=(const ExplicitBucketHistogram&) = delete;
    ExplicitBucketHistogram(ExplicitBucketHistogram&&) = delete;
    ExplicitBucketHistogram& operator=(ExplicitBucketHistogram&&) = delete;

    /// Record one sample. Non-finite samples are ignored (and counted).
    void record(double value) noexcept {
        if (!std::isfinite(value)) {
            ignored_.fetch_add(1U, std::memory_order_relaxed);
            return;
        }
        const std::size_t bucket = bucket_of(value);
        if constexpr (Concurrency == ConcurrencyPolicy::SingleThreaded) {
            cell_.apply(value, bucket);
        } else {
            const std::uint8_t slot = detail::thread_slot(MetricKind::ExplicitHistogram);
            if (slot == detail::fallback_slot) {
                fallback_.apply_multi(value, bucket);
                return;
            }
            auto& target = slots_[slot];
            const std::uint32_t marker = target.seq.load(std::memory_order_relaxed);
            // Announce as a full barrier (xchg on x86): the update must not
            // become visible before the announcement (design D9/D15).
            target.seq.store(marker + 1U, std::memory_order_seq_cst);
            const std::uint8_t bank = target.active.load(std::memory_order_relaxed);
            target.bank[bank].apply(value, bucket);
            target.seq.store(marker + 2U, std::memory_order_release);
        }
    }

    /// Current snapshot (bucket counts, summary, percentiles). Available only
    /// for Readable instruments.
    [[nodiscard]] HistogramSnapshot read() const requires (Access == AccessPolicy::Readable) {
        detail::Histogram_merge merged{};
        if constexpr (Concurrency == ConcurrencyPolicy::SingleThreaded) {
            const detail::Histogram_bank::View cell_view = cell_.load_view();
            if (Reset == ResetPolicy::ResetOnRead) {
                cell_.clear();
            }
            merged.absorb_view(cell_view);
        } else {
            for (std::size_t i = 0; i < detail::shard_count; ++i) {
                merge_slot(i, merged);
            }
            merged.absorb_view(Reset == ResetPolicy::ResetOnRead ? fallback_.take_view()
                                                                  : fallback_.load_view());
        }

        HistogramSnapshot snapshot{};
        snapshot.ignored_samples = ignored_.load(std::memory_order_relaxed);
        snapshot.count = merged.total;
        snapshot.sum = merged.sum;
        snapshot.min = merged.minimum;
        snapshot.max = merged.maximum;
        snapshot.effective_scale = 0;
        snapshot.bucket_count = bound_count_ + 1U;
        for (std::uint32_t i = 0; i < snapshot.bucket_count; ++i) {
            snapshot.buckets_lower[i] = lower_edge(i);
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
        std::string out = "ExplicitBucketHistogram(kind=";
        out += std::string(metrics::to_string(kind));
        out += ", concurrency=" + std::string(metrics::to_string(concurrency));
        out += ", access=" + std::string(metrics::to_string(access));
        out += ", reset=" + std::string(metrics::to_string(reset));
        out += ", bounds=" + std::to_string(bound_count_);
        out += ")";
        return out;
    }

    /// Diagnostic JSON (cold path). The instrument is NOT deserializable: its
    /// construction is Meter-only.
    friend void to_json(nlohmann::json& j, const ExplicitBucketHistogram& histogram) {
        j = nlohmann::json{
            {"kind", std::string(metrics::to_string(ExplicitBucketHistogram::kind))},
            {"concurrency", std::string(metrics::to_string(ExplicitBucketHistogram::concurrency))},
            {"access", std::string(metrics::to_string(ExplicitBucketHistogram::access))},
            {"reset", std::string(metrics::to_string(ExplicitBucketHistogram::reset))},
            {"bounds", histogram.bound_count_},
        };
    }

private:
    friend class Meter;
    friend class MetricProvider;

    explicit ExplicitBucketHistogram(const MeterPasskey&) noexcept {}

    /// The Meter installs the validated bounds after construction.
    void install_bounds(const double* bounds, std::uint32_t count) noexcept {
        for (std::uint32_t i = 0; i < count; ++i) {
            bounds_[i] = bounds[i];
        }
        bound_count_ = count;
    }

    static void harvest_thunk(void* object, MetricRecord& record) noexcept {
        const auto& self = *static_cast<const ExplicitBucketHistogram*>(object);
        record.histogram = self.read();
    }

    static void destroy_thunk(void* object) noexcept {
        static_cast<ExplicitBucketHistogram*>(object)->~ExplicitBucketHistogram();
    }

    /// Bucket index: the first bucket whose upper bound is >= value, or the
    /// overflow bucket when the value is above the last bound.
    [[nodiscard]] std::size_t bucket_of(double value) const noexcept {
        std::size_t low = 0;
        std::size_t high = bound_count_;
        while (low < high) {
            const std::size_t mid = low + (high - low) / 2U;
            if (value <= bounds_[mid]) {
                high = mid;
            } else {
                low = mid + 1U;
            }
        }
        return low;
    }

    /// Lower edge of a bucket (used for percentile interpolation).
    [[nodiscard]] double lower_edge(std::size_t bucket) const noexcept {
        if (bucket == 0U || bucket > bound_count_) {
            return 0.0;
        }
        return bounds_[bucket - 1U];
    }

    /// Upper edge of a bucket.
    [[nodiscard]] double upper_edge(std::size_t bucket) const noexcept {
        if (bucket < bound_count_) {
            return bounds_[bucket];
        }
        return 0.0;
    }

    /// Interpolated percentiles over the merged bucket counts.
    void compute_percentiles(HistogramSnapshot& snapshot) const noexcept {
        if (snapshot.count == 0U) {
            return;
        }
        snapshot.p50 = percentile(snapshot, 0.50);
        snapshot.p90 = percentile(snapshot, 0.90);
        snapshot.p99 = percentile(snapshot, 0.99);
        snapshot.p999 = percentile(snapshot, 0.999);
    }

    [[nodiscard]] double percentile(const HistogramSnapshot& snapshot, double quantile) const noexcept {
        const double rank = quantile * static_cast<double>(snapshot.count);
        std::uint64_t cumulative = 0;
        for (std::uint32_t i = 0; i < snapshot.bucket_count; ++i) {
            const std::uint64_t in_bucket = snapshot.buckets_count[i];
            if (in_bucket == 0U) {
                continue;
            }
            if (static_cast<double>(cumulative + in_bucket) >= rank) {
                const double lower = (i == 0U) ? snapshot.min : lower_edge(i);
                const double upper = (i + 1U == snapshot.bucket_count) ? snapshot.max : upper_edge(i);
                const double within = static_cast<double>(cumulative + in_bucket) - rank;
                const double fraction = 1.0 - (within / static_cast<double>(in_bucket));
                if (upper > lower) {
                    return lower + (upper - lower) * fraction;
                }
                return lower;
            }
            cumulative += in_bucket;
        }
        return snapshot.max;
    }

    /// Merge one slot under the seq handshake (design D9): the bank is copied
    /// into a plain view and — for ResetOnRead — cleared only while the
    /// sequence proves no producer is mid-record. A stalled slot is deferred
    /// to a later window with its contents untouched, so every record lands
    /// in exactly one window.
    void merge_slot(std::size_t index, detail::Histogram_merge& merged) const noexcept {
        auto& slot = slots_[index];
        const bool consuming = (Reset == ResetPolicy::ResetOnRead);
        std::size_t bank_index = slot.active.load(std::memory_order_relaxed);
        if (consuming) {
            const std::uint8_t current = static_cast<std::uint8_t>(bank_index);
            bank_index = slot.active.exchange(static_cast<std::uint8_t>(current ^ 1U),
                                              std::memory_order_acq_rel);
        }
        for (int attempt = 0; attempt < detail::histogram_handshake_attempts; ++attempt) {
            const std::uint32_t before = slot.seq.load(std::memory_order_acquire);
            if ((before & 1U) != 0U) {
                continue; // a producer is mid-record
            }
            const detail::Histogram_bank::View view = slot.bank[bank_index].load_view();
            // Keep the bank reads inside the checked window: compiler barrier
            // only (x86 TSO already orders loads; design D9).
            std::atomic_signal_fence(std::memory_order_acq_rel);
            if (slot.seq.load(std::memory_order_acquire) == before) {
                if (consuming) {
                    // Safe to clear: post-exchange producers write the other
                    // bank, and the stable check proved no straggler is live.
                    slot.bank[bank_index].clear();
                }
                merged.absorb_view(view);
                return;
            }
        }
        // Deferred to a later window: the bank is left untouched (no loss).
    }

    /// Identity of the owning registry slot (set by the Meter at creation).
    const MetricIdentity* identity_{nullptr};

    double bounds_[detail::max_explicit_bounds]{};
    std::uint32_t bound_count_{0};

    mutable detail::Histogram_bank cell_{};                      // SingleThreaded
    mutable detail::Histogram_slot slots_[detail::shard_count]{}; // MultiThreaded
    mutable detail::Histogram_bank fallback_{};                   // shared overflow cell
    std::atomic<std::uint64_t> ignored_{0};
};

static_assert(sizeof(ExplicitBucketHistogram<ConcurrencyPolicy::MultiThreaded, AccessPolicy::Readable,
                                              ResetPolicy::ResetOnRead>) <=
                  detail::max_explicit_histogram_size,
              "explicit histogram must fit the registry budget");
static_assert(concepts::Metric<ExplicitBucketHistogram<ConcurrencyPolicy::SingleThreaded,
                                                        AccessPolicy::Readable,
                                                        ResetPolicy::Cumulative>>,
              "ExplicitBucketHistogram must satisfy the Metric concept");
static_assert(concepts::ReadableMetric<ExplicitBucketHistogram<ConcurrencyPolicy::SingleThreaded,
                                                                AccessPolicy::Readable,
                                                                ResetPolicy::Cumulative>>,
              "a Readable histogram must satisfy the ReadableMetric concept");
static_assert(!concepts::ReadableMetric<ExplicitBucketHistogram<ConcurrencyPolicy::SingleThreaded,
                                                                 AccessPolicy::WriteOnly,
                                                                 ResetPolicy::Cumulative>>,
              "a WriteOnly histogram must NOT satisfy the ReadableMetric concept");
static_assert(!detail::HasRead<ExplicitBucketHistogram<ConcurrencyPolicy::SingleThreaded,
                                                         AccessPolicy::WriteOnly,
                                                         ResetPolicy::Cumulative>>,
              "a WriteOnly histogram must not expose read()");
static_assert(!detail::HasSnapshot<ExplicitBucketHistogram<ConcurrencyPolicy::SingleThreaded,
                                                             AccessPolicy::WriteOnly,
                                                             ResetPolicy::Cumulative>>,
              "a WriteOnly histogram must not expose snapshot()");

/// Free helper (mandate 9): mirrors the member.
template <nexigen::metrics::ConcurrencyPolicy Concurrency, nexigen::metrics::AccessPolicy Access,
          nexigen::metrics::ResetPolicy Reset>
[[nodiscard]] std::string to_string(
    const nexigen::metrics::ExplicitBucketHistogram<Concurrency, Access, Reset>& histogram) {
    return histogram.to_string();
}

} // namespace nexigen::metrics


/// Explicit histograms format as their diagnostic string (mandate 11).
template <nexigen::metrics::ConcurrencyPolicy Concurrency, nexigen::metrics::AccessPolicy Access,
          nexigen::metrics::ResetPolicy Reset>
struct std::formatter<nexigen::metrics::ExplicitBucketHistogram<Concurrency, Access, Reset>>
    : std::formatter<std::string_view> {
    template <class Ctx>
    auto format(const nexigen::metrics::ExplicitBucketHistogram<Concurrency, Access, Reset>& histogram,
                Ctx& ctx) const {
        const std::string text = histogram.to_string();
        return std::formatter<std::string_view>::format(text, ctx);
    }
};
