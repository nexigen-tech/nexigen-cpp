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
#include <nexigen/metrics/detail/instrument_helpers.hpp>
#include <nexigen/metrics/detail/layout.hpp>
#include <nexigen/metrics/detail/probes.hpp>
#include <nexigen/metrics/detail/thread_slot.hpp>
#include <nexigen/metrics/metric_snapshot.hpp>
#include <nexigen/metrics/policy_enums.hpp>

namespace nexigen::metrics {

template <nexigen::metrics::ConcurrencyPolicy Concurrency, nexigen::metrics::AccessPolicy Access,
          nexigen::metrics::ResetPolicy Reset>
class ExplicitBucketHistogram;

/// @brief Construction guard: only a Meter may create instruments.
///
/// Forward-declared so instrument constructors can take it by reference; the
/// complete definition lives in `meter.hpp`.
class MeterPasskey;

namespace detail {

/// @brief Maximum number of explicit bucket upper bounds an instrument stores.
///
/// The inline array adds one overflow bucket on top of these, so a bank owns at
/// most `max_explicit_bounds + 1` bucket counters.
inline constexpr std::size_t max_explicit_bounds = 64;

/// @brief One histogram bank: per-bucket counters plus the running summary.
///
/// A bank is the consistency unit of the instrument: recording folds a sample
/// into one bank, and a harvest copies the whole bank out under the sequence
/// handshake. The `alignas(64)` keeps a slot's banks off neighbouring threads'
/// cache lines.
///
/// @note Spec `metrics/explicit-histogram`, requirement "Explicit histogram
///       recording and aggregation".
struct alignas(64) HistogramBank {
    /// @brief Plain, non-atomic copy of one bank's contents (harvest side).
    ///
    /// `buckets` holds `max_explicit_bounds + 1` counters, the last one being
    /// the overflow bucket. A bank with `count == 0` yields `minimum` at
    /// +infinity and `maximum` at -infinity.
    struct View {
        /// @brief Per-bucket sample counts, overflow bucket last.
        std::uint64_t buckets[max_explicit_bounds + 1]{};
        /// @brief Total samples folded into the bank.
        std::uint64_t count{0};
        /// @brief Sum of the recorded values.
        double sum{0.0};
        /// @brief Smallest recorded value; meaningful only when `count > 0`.
        double minimum{0.0};
        /// @brief Largest recorded value; meaningful only when `count > 0`.
        double maximum{0.0};
    };

    /// @brief Bucket counters, indexed as in `View::buckets`.
    std::atomic<std::uint64_t> buckets[max_explicit_bounds + 1]{};
    /// @brief Total samples folded into the bank.
    std::atomic<std::uint64_t> count{0};
    /// @brief Sum of the recorded values.
    std::atomic<double> sum{0.0};
    /// @brief Smallest recorded value, seeded at +infinity.
    std::atomic<double> minimum{std::numeric_limits<double>::infinity()};
    /// @brief Largest recorded value, seeded at -infinity.
    std::atomic<double> maximum{-std::numeric_limits<double>::infinity()};

    /// @brief Reset the bank to empty: zero counts and both extrema at infinity.
    ///
    /// Cold path; only a harvest holding the sequence handshake may call it.
    void clear() noexcept {
        for (std::size_t i = 0; i < max_explicit_bounds + 1U; ++i) {
            buckets[i].store(0, std::memory_order_relaxed);
        }
        count.store(0, std::memory_order_relaxed);
        sum.store(0.0, std::memory_order_relaxed);
        minimum.store(std::numeric_limits<double>::infinity(), std::memory_order_relaxed);
        maximum.store(-std::numeric_limits<double>::infinity(), std::memory_order_relaxed);
    }

    /// @brief Copy the bank without consuming it (Cumulative harvests).
    ///
    /// @return A plain `View` of the current counts and summary.
    /// @note Callers must hold the sequence handshake while copying a
    ///       multi-writer bank; the loads themselves are relaxed.
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

    /// @brief Consuming copy for the multi-writer fallback bank.
    ///
    /// Each field is exchanged to its empty value, so every update lands in
    /// exactly one window even when several threads record concurrently.
    ///
    /// @return A plain `View` of the harvested counts and summary.
    /// @note Spec `metrics/explicit-histogram`, requirement "Explicit histogram
    ///       reset windows"; per-field straddle is documented in design D15.
    [[nodiscard]] View take_view() noexcept {
        View view{};
        for (std::size_t i = 0; i < max_explicit_bounds + 1U; ++i) {
            view.buckets[i] = buckets[i].exchange(0U, std::memory_order_relaxed);
        }
        view.count = count.exchange(0U, std::memory_order_relaxed);
        view.sum = sum.exchange(0.0, std::memory_order_relaxed);
        view.minimum =
            minimum.exchange(std::numeric_limits<double>::infinity(), std::memory_order_relaxed);
        view.maximum =
            maximum.exchange(-std::numeric_limits<double>::infinity(), std::memory_order_relaxed);
        return view;
    }

    /// @brief Fold one sample into an exclusive bank (single writer).
    ///
    /// Plain load/store pairs: no atomic RMW and no cross-core contention.
    /// The first sample seeds `minimum` and `maximum`.
    ///
    /// @param value Sample to account for; must be finite.
    /// @param bucket Bucket index in `[0, max_explicit_bounds]`.
    /// @warning Exclusive-slot use only: the caller must be the sole writer of
    ///          this bank, guarded by the sequence handshake.
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

    /// @brief Fold one sample into the shared fallback bank (several writers).
    ///
    /// Every update is an atomic read-modify-write: `fetch_add` for counters and
    /// sum, a compare-exchange loop for the extrema.
    ///
    /// @param value Sample to account for; must be finite.
    /// @param bucket Bucket index in `[0, max_explicit_bounds]`.
    /// @note Spec `metrics/explicit-histogram`, requirement "Multi-threaded
    ///       explicit histogram recording".
    void apply_multi(double value, std::size_t bucket) noexcept {
        buckets[bucket].fetch_add(1U, std::memory_order_relaxed);
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
};

/// @brief A recording slot: double-buffered banks plus the sequence handshake.
///
/// `active` selects the bank receiving new samples; `seq` is odd while a
/// producer is mid-record. Harvests read the inactive bank (ResetOnRead) or the
/// active one (Cumulative) only while `seq` is stable. `alignas(64)` keeps each
/// thread's slot on its own cache line.
struct alignas(64) HistogramSlot {
    /// @brief The two banks a reset-on-read harvest swaps between.
    HistogramBank bank[2];
    /// @brief Sequence handshake: odd during a record, even when stable.
    std::atomic<std::uint32_t> seq{0};
    /// @brief Index of the bank currently receiving samples.
    std::atomic<std::uint8_t> active{0};
};

/// @brief Bounded retry budget for the harvest sequence handshake.
///
/// A slot that stays busy for this many attempts is deferred to the next
/// window with its contents untouched, so no sample is lost.
inline constexpr int histogram_handshake_attempts = 64;

/// @brief Merged view of one or more banks in a common bucket layout.
///
/// Harvests accumulate every slot and the fallback bank into one `HistogramMerge`,
/// then render a `HistogramSnapshot` from it. `empty` records whether any bank
/// contributed a sample, keeping min/max meaningful for an empty window.
struct HistogramMerge {
    /// @brief Accumulated per-bucket counts, overflow bucket last.
    std::uint64_t buckets[max_explicit_bounds + 1]{};
    /// @brief Accumulated sample count across all contributing banks.
    std::uint64_t total{0};
    /// @brief Accumulated sum of recorded values.
    double sum{0.0};
    /// @brief Smallest recorded value across banks; valid when `!empty`.
    double minimum{0.0};
    /// @brief Largest recorded value across banks; valid when `!empty`.
    double maximum{0.0};
    /// @brief True until a bank with a non-zero count has been absorbed.
    bool empty{true};

    /// @brief Fold one bank view into the merged totals.
    ///
    /// Adds bucket counts, count and sum; min/max widen only for a view that
    /// carries samples, so an empty bank cannot poison the extrema.
    ///
    /// @param view Bank copy produced by `HistogramBank::load_view` or
    ///        `HistogramBank::take_view`.
    /// @note Spec `metrics/explicit-histogram`, requirement "Explicit histogram
    ///       percentile snapshots".
    void absorb_view(const detail::HistogramBank::View& view) noexcept {
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
            if (bank_min < minimum) {
                minimum = bank_min;
            }
            if (bank_max > maximum) {
                maximum = bank_max;
            }
        }
    }
};

} // namespace detail

/// @brief A histogram with caller-defined bucket upper bounds.
///
/// Counts samples into one bucket per caller-supplied bound plus a final
/// overflow bucket. The bounds are installed once by the owning Meter and are
/// immutable afterwards, so the bucket layout of every snapshot is stable.
///
/// `SingleThreaded` recording folds into one plain bank. `MultiThreaded`
/// recording uses a `thread_local`-assigned exclusive slot per thread and a
/// shared fallback bank beyond the shard pool, so `record()` is allocation-free
/// and needs no lock in either mode. Harvests are cold-path and merge the banks
/// under a sequence handshake; `ResetOnRead` swaps the double-buffered bank so a
/// racing record still lands in exactly one window.
///
/// @tparam Concurrency Recording policy: `SingleThreaded` (single writer) or
///         `MultiThreaded` (per-thread shards plus a shared fallback).
/// @tparam Access `Readable` exposes `read()` and `snapshot()`; `WriteOnly`
///         removes both at compile time.
/// @tparam Reset `Cumulative` keeps totals across reads; `ResetOnRead` reports
///         and clears one window per observation.
///
/// @note Spec `metrics/explicit-histogram`, requirement "Explicit bucket
///       configuration".
/// @note Construction is Meter-only: spec `metrics/metric-provider`,
///       requirement "Factory hierarchy and passkey construction".
/// @warning A `SingleThreaded` instrument is single-writer: reads and harvests
///          must not run concurrently with recording unless the caller
///          establishes happens-before.
template <ConcurrencyPolicy Concurrency = ConcurrencyPolicy::SingleThreaded,
          AccessPolicy Access = AccessPolicy::Readable, ResetPolicy Reset = ResetPolicy::Cumulative>
class ExplicitBucketHistogram {
public:
    /// @brief `MetricKind` tag consumed by the concept and registry.
    static constexpr MetricKind kind = MetricKind::ExplicitHistogram;
    /// @brief Concurrency policy this instantiation was created with.
    static constexpr ConcurrencyPolicy concurrency = Concurrency;
    /// @brief Access policy this instantiation was created with.
    static constexpr AccessPolicy access = Access;
    /// @brief Reset policy this instantiation was created with.
    static constexpr ResetPolicy reset = Reset;

    /// @brief Deleted: instruments are non-copyable registry-owned handles.
    ExplicitBucketHistogram(const ExplicitBucketHistogram&) = delete;
    /// @brief Deleted: instruments are non-copyable registry-owned handles.
    ExplicitBucketHistogram& operator=(const ExplicitBucketHistogram&) = delete;
    /// @brief Deleted: instruments live at a fixed registry address.
    ExplicitBucketHistogram(ExplicitBucketHistogram&&) = delete;
    /// @brief Deleted: instruments live at a fixed registry address.
    ExplicitBucketHistogram& operator=(ExplicitBucketHistogram&&) = delete;

    /// @brief Record one sample into its bucket and the running summary.
    ///
    /// Non-finite samples leave the buckets and summary untouched and increment
    /// the ignored-sample counter instead. Finite samples increment exactly one
    /// bucket, the count, the sum, and widen min/max as needed, with bounded
    /// comparison work over the inline bounds and no heap allocation.
    ///
    /// @param value Sample to record; NaN and infinities are ignored visibly.
    /// @note Spec `metrics/explicit-histogram`, requirement "Explicit histogram
    ///       recording and aggregation".
    /// @warning Under `ConcurrencyPolicy::SingleThreaded` the caller must be the
    ///          only writer.
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

    /// @brief Take a snapshot of the current (or current window's) values.
    ///
    /// Merges every recording slot plus the shared fallback bank, renders the
    /// bounds and bucket counts, and computes `p50`/`p90`/`p99`/`p99.9` by
    /// linear interpolation inside the containing bucket. Under `ResetOnRead`
    /// the merged bank is cleared as it is consumed, so each call reports only
    /// samples recorded since the previous one.
    ///
    /// @return `HistogramSnapshot` with bounds, counts, summary, percentiles and
    ///         the ignored-sample count.
    /// @note Spec `metrics/explicit-histogram`, requirement "Explicit histogram
    ///       read surface".
    /// @note Spec `metrics/explicit-histogram`, requirement "Explicit histogram
    ///       reset windows".
    [[nodiscard]] HistogramSnapshot read() const
        requires(Access == AccessPolicy::Readable)
    {
        detail::HistogramMerge merged{};
        if constexpr (Concurrency == ConcurrencyPolicy::SingleThreaded) {
            const detail::HistogramBank::View cell_view = cell_.load_view();
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

    /// @brief Build the full `MetricRecord` for this instrument.
    ///
    /// Combines the identity installed by the Meter, the policy classification
    /// and the current histogram values from `read()`.
    ///
    /// @return `MetricRecord` carrying `MetricKind::ExplicitHistogram` and the
    ///         histogram snapshot from `read()`.
    /// @note Spec `metrics/explicit-histogram`, requirement "Explicit histogram
    ///       read surface".
    [[nodiscard]] MetricRecord snapshot() const
        requires(Access == AccessPolicy::Readable)
    {
        MetricRecord record = detail::make_record(kind, concurrency, access, reset, identity_);
        record.histogram = read();
        return record;
    }

    /// @brief Cold-path diagnostic string; never resets a window.
    ///
    /// @return Implementation-defined text naming the kind, policies and bound
    ///         count; the free `to_string` and `std::formatter` mirror it.
    /// @note Project mandate 9: every public type renders `to_string()`.
    /// @note Spec `metrics/explicit-histogram`, requirement "Explicit histogram
    ///       serialization contract".
    [[nodiscard]] std::string to_string() const {
        std::string out = "ExplicitBucketHistogram(kind=";
        detail::append_classification(out, kind, concurrency, access, reset);
        out += ", bounds=" + std::to_string(bound_count_);
        out += ")";
        return out;
    }

    /// @brief Diagnostic JSON for the instrument (cold path, write-only).
    ///
    /// Emits the policy classification and the bound count. The instrument is
    /// deliberately NOT deserializable: its construction is Meter-only.
    ///
    /// @param j JSON value overwritten with the instrument diagnostics.
    /// @param histogram Instrument to describe.
    /// @note Project mandate 10: JSON is the structured diagnostic surface.
    /// @note Spec `metrics/explicit-histogram`, requirement "Explicit histogram
    ///       serialization contract".
    friend void to_json(nlohmann::json& j, const ExplicitBucketHistogram& histogram) {
        j = detail::classification_json(
            ExplicitBucketHistogram::kind, ExplicitBucketHistogram::concurrency,
            ExplicitBucketHistogram::access, ExplicitBucketHistogram::reset);
        j["bounds"] = histogram.bound_count_;
    }

private:
    friend class Meter;
    friend class MetricProvider;

    /// @brief Meter-only constructor; the passkey proves factory authority.
    ///
    /// Creates an empty instrument with no bounds installed yet; the Meter
    /// follows up with `install_bounds` before publishing the slot. The
    /// unnamed `MeterPasskey` argument proves factory authority and carries no
    /// state.
    explicit ExplicitBucketHistogram(const MeterPasskey&) noexcept {}

    /// @brief Install the validated bucket bounds (Meter cold path).
    ///
    /// Copies up to `max_explicit_bounds` finite, strictly increasing upper
    /// bounds into the inline array and records their count. Called once,
    /// before the instrument is published to the harvester.
    ///
    /// @param bounds Caller-owned bounds array; must be validated by the caller.
    /// @param count Number of bounds, at most `detail::max_explicit_bounds`.
    /// @warning Must not race recording: only the Meter calls this, on the
    ///          freshly constructed object.
    void install_bounds(const double* bounds, std::uint32_t count) noexcept {
        for (std::uint32_t i = 0; i < count; ++i) {
            bounds_[i] = bounds[i];
        }
        bound_count_ = count;
    }

    /// @brief Registry harvest hook: fill `record.histogram` from the instrument.
    ///
    /// @param object Type-erased pointer to this instrument.
    /// @param record Record to populate in place.
    /// @note Spec `metrics/metric-provider`, requirement "Background harvesting
    ///       and collection period".
    static void harvest_thunk(void* object, MetricRecord& record) noexcept {
        const auto& self = *static_cast<const ExplicitBucketHistogram*>(object);
        record.histogram = self.read();
    }

    /// @brief Locate the bucket for a value with a binary search over the bounds.
    ///
    /// Returns the first bucket whose upper bound is `>= value`, or the overflow
    /// bucket when the value is above the last bound.
    ///
    /// @param value Finite sample value.
    /// @return Bucket index in `[0, bound_count_]`.
    /// @note Spec `metrics/explicit-histogram`, requirement "Explicit bucket
    ///       configuration".
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

    /// @brief Lower edge of a bucket, used for percentile interpolation.
    ///
    /// @param bucket Bucket index.
    /// @return Previous bound, or `0.0` for bucket 0 and out-of-range indices.
    [[nodiscard]] double lower_edge(std::size_t bucket) const noexcept {
        if (bucket == 0U || bucket > bound_count_) {
            return 0.0;
        }
        return bounds_[bucket - 1U];
    }

    /// @brief Upper edge of a bucket.
    ///
    /// @param bucket Bucket index.
    /// @return The bucket's own bound, or `0.0` for the unbounded overflow bucket.
    [[nodiscard]] double upper_edge(std::size_t bucket) const noexcept {
        if (bucket < bound_count_) {
            return bounds_[bucket];
        }
        return 0.0;
    }

    /// @brief Fill the four percentiles of a merged snapshot.
    ///
    /// Leaves them at zero when no samples were recorded.
    ///
    /// @param snapshot Snapshot whose bucket counts and summary are read and
    ///        whose `p50`/`p90`/`p99`/`p999` fields are written.
    /// @note Spec `metrics/explicit-histogram`, requirement "Explicit histogram
    ///       percentile snapshots".
    void compute_percentiles(HistogramSnapshot& snapshot) const noexcept {
        if (snapshot.count == 0U) {
            return;
        }
        snapshot.p50 = percentile(snapshot, 0.50);
        snapshot.p90 = percentile(snapshot, 0.90);
        snapshot.p99 = percentile(snapshot, 0.99);
        snapshot.p999 = percentile(snapshot, 0.999);
    }

    /// @brief Interpolate one quantile from the merged bucket counts.
    ///
    /// Finds the containing bucket and linearly interpolates between its edges;
    /// the first bucket uses the observed minimum, the overflow bucket the
    /// observed maximum.
    ///
    /// @param snapshot Merged snapshot supplying buckets, count and extrema.
    /// @param quantile Rank in `(0, 1]`, e.g. `0.99` for `p99`.
    /// @return Interpolated value, or `snapshot.max` when no bucket matches.
    /// @note Spec `metrics/explicit-histogram`, requirement "Explicit histogram
    ///       percentile snapshots".
    [[nodiscard]] double percentile(const HistogramSnapshot& snapshot,
                                    double quantile) const noexcept {
        const double rank = quantile * static_cast<double>(snapshot.count);
        std::uint64_t cumulative = 0;
        for (std::uint32_t i = 0; i < snapshot.bucket_count; ++i) {
            const std::uint64_t in_bucket = snapshot.buckets_count[i];
            if (in_bucket == 0U) {
                continue;
            }
            if (static_cast<double>(cumulative + in_bucket) >= rank) {
                const double lower = (i == 0U) ? snapshot.min : lower_edge(i);
                const double upper =
                    (i + 1U == snapshot.bucket_count) ? snapshot.max : upper_edge(i);
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

    /// @brief Merge one recording slot under the sequence handshake (design D9).
    ///
    /// The bank is copied into a plain view and, for `ResetOnRead`, cleared only
    /// while the sequence proves no producer is mid-record. A stalled slot is
    /// deferred to a later window with its contents untouched, so every record
    /// lands in exactly one window.
    ///
    /// @param index Recording-slot index in `[0, detail::shard_count)`.
    /// @param merged Accumulator that absorbs the bank view.
    /// @note Spec `metrics/explicit-histogram`, requirement "Explicit histogram
    ///       reset windows".
    /// @note Spec `metrics/explicit-histogram`, requirement "Multi-threaded
    ///       explicit histogram recording".
    void merge_slot(std::size_t index, detail::HistogramMerge& merged) const noexcept {
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
            const detail::HistogramBank::View view = slot.bank[bank_index].load_view();
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

    /// @brief Identity of the owning registry slot (set by the Meter).
    const MetricIdentity* identity_{nullptr};

    /// @brief Inline bucket upper bounds, installed once by the Meter.
    double bounds_[detail::max_explicit_bounds]{};
    /// @brief Number of live entries in `bounds_`.
    std::uint32_t bound_count_{0};

    /// @brief SingleThreaded recording bank.
    mutable detail::HistogramBank cell_{};
    /// @brief MultiThreaded per-thread recording slots.
    mutable detail::HistogramSlot slots_[detail::shard_count]{};
    /// @brief Shared bank for threads beyond the shard pool.
    mutable detail::HistogramBank fallback_{};
    /// @brief Count of non-finite samples rejected by `record()`.
    std::atomic<std::uint64_t> ignored_{0};
};

static_assert(sizeof(ExplicitBucketHistogram<ConcurrencyPolicy::MultiThreaded,
                                             AccessPolicy::Readable, ResetPolicy::ResetOnRead>) <=
                  detail::max_explicit_histogram_size,
              "explicit histogram must fit the registry budget");
static_assert(
    concepts::Metric<ExplicitBucketHistogram<ConcurrencyPolicy::SingleThreaded,
                                             AccessPolicy::Readable, ResetPolicy::Cumulative>>,
    "ExplicitBucketHistogram must satisfy the Metric concept");
static_assert(
    concepts::ReadableMetric<ExplicitBucketHistogram<
        ConcurrencyPolicy::SingleThreaded, AccessPolicy::Readable, ResetPolicy::Cumulative>>,
    "a Readable histogram must satisfy the ReadableMetric concept");
static_assert(
    !concepts::ReadableMetric<ExplicitBucketHistogram<
        ConcurrencyPolicy::SingleThreaded, AccessPolicy::WriteOnly, ResetPolicy::Cumulative>>,
    "a WriteOnly histogram must NOT satisfy the ReadableMetric concept");
static_assert(
    !detail::HasRead<ExplicitBucketHistogram<ConcurrencyPolicy::SingleThreaded,
                                             AccessPolicy::WriteOnly, ResetPolicy::Cumulative>>,
    "a WriteOnly histogram must not expose read()");
static_assert(
    !detail::HasSnapshot<ExplicitBucketHistogram<ConcurrencyPolicy::SingleThreaded,
                                                 AccessPolicy::WriteOnly, ResetPolicy::Cumulative>>,
    "a WriteOnly histogram must not expose snapshot()");

/// @brief Free `to_string` overload mirroring the member (project mandate 9).
///
/// @tparam Concurrency Recording policy of the instrument.
/// @tparam Access Access policy of the instrument.
/// @tparam Reset Reset policy of the instrument.
/// @param histogram Instrument to render.
/// @return The same diagnostic text as `histogram.to_string()`.
/// @note Spec `metrics/explicit-histogram`, requirement "Explicit histogram
///       serialization contract".
template <nexigen::metrics::ConcurrencyPolicy Concurrency, nexigen::metrics::AccessPolicy Access,
          nexigen::metrics::ResetPolicy Reset>
[[nodiscard]] std::string
to_string(const nexigen::metrics::ExplicitBucketHistogram<Concurrency, Access, Reset>& histogram) {
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
/// @note Spec `metrics/explicit-histogram`, requirement "Explicit histogram
///       serialization contract".
template <nexigen::metrics::ConcurrencyPolicy Concurrency, nexigen::metrics::AccessPolicy Access,
          nexigen::metrics::ResetPolicy Reset>
struct std::formatter<nexigen::metrics::ExplicitBucketHistogram<Concurrency, Access, Reset>>
    : std::formatter<std::string_view> {
    /// @brief Format the instrument by delegating to its `to_string()`.
    ///
    /// @tparam Ctx Format context type supplied by `std::format`.
    /// @param histogram Instrument to render.
    /// @param ctx Output context to append the text to.
    /// @return Iterator past the last character written.
    /// @note Project mandate 11: `std::formatter` support for diagnostics.
    template <class Ctx>
    auto
    format(const nexigen::metrics::ExplicitBucketHistogram<Concurrency, Access, Reset>& histogram,
           Ctx& ctx) const {
        const std::string text = histogram.to_string();
        return std::formatter<std::string_view>::format(text, ctx);
    }
};
