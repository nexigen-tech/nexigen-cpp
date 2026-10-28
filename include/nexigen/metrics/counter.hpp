// nexigen::metrics — the counter instrument (design D8;
// spec metrics/counter-gauge).
//
// Counter<Concurrency, Access, Reset> counts events. Two storage shapes:
//
//   SingleThreaded — one plain 64-bit cell on its own cache line. inc() is a
//       plain increment (sub-nanosecond class, no atomic instruction). The
//       contract is single-writer: reads and harvests must not run
//       concurrently with recording unless the caller establishes
//       happens-before (use collect_now() on the recording thread).
//
//   MultiThreaded  — 32 exclusive per-thread shards plus one shared fallback
//       cell. Recording is a relaxed fetch_add on the caller's own shard:
//       wait-free, no cross-core CAS contention, no cache line shared
//       between threads.
//
// ResetPolicy::ResetOnRead reports the delta since the previous observation
// and clears the window; every increment is a single atomic RMW, so it lands
// in exactly one window.
#pragma once

#include <atomic>
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
class Counter;

/// Construction guard: only a Meter may create instruments.
class MeterPasskey;

namespace detail {

/// Single-threaded counter cell: plain storage, one cache line.
struct alignas(64) Counter_cell {
    std::uint64_t value{0};
};

/// Multi-threaded counter shard: 64-byte aligned atomic cell.
struct alignas(64) Counter_shard {
    std::atomic<std::uint64_t> value{0};
};
static_assert(sizeof(Counter_shard) == 64, "a counter shard owns exactly one cache line");

/// Counter storage selected by the concurrency policy.
template <ConcurrencyPolicy Concurrency, ResetPolicy Reset>
class Counter_storage;

template <ResetPolicy Reset>
class Counter_storage<ConcurrencyPolicy::SingleThreaded, Reset> {
public:
    void add(std::uint64_t delta) noexcept { cell_.value += delta; }

    [[nodiscard]] std::uint64_t observe() const noexcept { return cell_.value; }

    [[nodiscard]] std::uint64_t take() const noexcept {
        if constexpr (Reset == ResetPolicy::ResetOnRead) {
            const std::uint64_t value = cell_.value;
            cell_.value = 0;
            return value;
        } else {
            return cell_.value;
        }
    }

private:
    mutable Counter_cell cell_{};
};

template <ResetPolicy Reset>
class Counter_storage<ConcurrencyPolicy::MultiThreaded, Reset> {
public:
    void add(std::uint64_t delta) noexcept {
        shards_[detail::thread_slot(MetricKind::Counter)].value.fetch_add(delta, std::memory_order_relaxed);
    }

    [[nodiscard]] std::uint64_t observe() const noexcept {
        std::uint64_t total = 0;
        for (const auto& shard : shards_) {
            total += shard.value.load(std::memory_order_relaxed);
        }
        return total;
    }

    [[nodiscard]] std::uint64_t take() const noexcept {
        std::uint64_t total = 0;
        for (auto& shard : shards_) {
            if constexpr (Reset == ResetPolicy::ResetOnRead) {
                total += shard.value.exchange(0, std::memory_order_relaxed);
            } else {
                total += shard.value.load(std::memory_order_relaxed);
            }
        }
        return total;
    }

private:
    mutable Counter_shard shards_[shard_count]{};
};

} // namespace detail

/// A counted event source. Constructible only through a Meter.
template <ConcurrencyPolicy Concurrency = ConcurrencyPolicy::SingleThreaded,
          AccessPolicy Access = AccessPolicy::Readable,
          ResetPolicy Reset = ResetPolicy::Cumulative>
class Counter {
    static_assert(Concurrency == ConcurrencyPolicy::SingleThreaded ||
                      Concurrency == ConcurrencyPolicy::MultiThreaded,
                  "unknown concurrency policy");

public:
    static constexpr MetricKind kind = MetricKind::Counter;
    static constexpr ConcurrencyPolicy concurrency = Concurrency;
    static constexpr AccessPolicy access = Access;
    static constexpr ResetPolicy reset = Reset;

    Counter(const Counter&) = delete;
    Counter& operator=(const Counter&) = delete;
    Counter(Counter&&) = delete;
    Counter& operator=(Counter&&) = delete;

    /// Record one event.
    void inc() noexcept { storage_.add(1U); }

    /// Record `delta` events.
    void add(std::uint64_t delta) noexcept { storage_.add(delta); }

    /// Current total (or the window delta for ResetOnRead — which also clears
    /// the window). Available only for Readable instruments.
    [[nodiscard]] std::uint64_t read() const requires (Access == AccessPolicy::Readable) {
        return storage_.take();
    }

    /// Full record for this instrument (identity, classification, values).
    [[nodiscard]] MetricRecord snapshot() const requires (Access == AccessPolicy::Readable) {
        MetricRecord record{};
        record.kind = kind;
        record.concurrency = concurrency;
        record.access = access;
        record.reset = reset;
        record.scalar.counter = storage_.take();
        if (identity_ != nullptr) {
            record.identity = *identity_;
        }
        return record;
    }

    /// Cold-path diagnostic (mandate 9).
    [[nodiscard]] std::string to_string() const {
        std::string out = "Counter(kind=";
        out += std::string(metrics::to_string(kind));
        out += ", concurrency=" + std::string(metrics::to_string(concurrency));
        out += ", access=" + std::string(metrics::to_string(access));
        out += ", reset=" + std::string(metrics::to_string(reset));
        out += ", value=" + std::to_string(storage_.observe());
        out += ")";
        return out;
    }

    /// Diagnostic JSON (cold path). The instrument is NOT deserializable: its
    /// construction is Meter-only.
    friend void to_json(nlohmann::json& j, const Counter& counter) {
        j = nlohmann::json{
            {"kind", std::string(metrics::to_string(Counter::kind))},
            {"concurrency", std::string(metrics::to_string(Counter::concurrency))},
            {"access", std::string(metrics::to_string(Counter::access))},
            {"reset", std::string(metrics::to_string(Counter::reset))},
            {"value", counter.storage_.observe()},
        };
    }

private:
    friend class Meter;
    friend class MetricProvider;

    explicit Counter(const MeterPasskey&) noexcept {}

    /// Type-erased harvest hook: the provider calls it through a plain
    /// function pointer, so it must live inside the class.
    static void harvest_thunk(void* object, MetricRecord& record) noexcept {
        const auto& self = *static_cast<const Counter*>(object);
        record.scalar.counter = self.storage_.take();
    }

    static void destroy_thunk(void* object) noexcept {
        static_cast<Counter*>(object)->~Counter();
    }

    /// Identity of the owning registry slot (set by the Meter at creation).
    const MetricIdentity* identity_{nullptr};

    detail::Counter_storage<Concurrency, Reset> storage_{};
};

static_assert(sizeof(Counter<ConcurrencyPolicy::MultiThreaded, AccessPolicy::Readable,
                             ResetPolicy::ResetOnRead>) <= detail::max_counter_size,
              "counter must fit the registry budget");
static_assert(concepts::Metric<Counter<ConcurrencyPolicy::SingleThreaded, AccessPolicy::Readable,
                                        ResetPolicy::Cumulative>>,
              "Counter must satisfy the Metric concept");
static_assert(concepts::ReadableMetric<Counter<ConcurrencyPolicy::SingleThreaded,
                                                AccessPolicy::Readable,
                                                ResetPolicy::Cumulative>>,
              "a Readable counter must satisfy the ReadableMetric concept");
static_assert(!concepts::ReadableMetric<Counter<ConcurrencyPolicy::SingleThreaded,
                                                 AccessPolicy::WriteOnly,
                                                 ResetPolicy::Cumulative>>,
              "a WriteOnly counter must NOT satisfy the ReadableMetric concept");
static_assert(!detail::HasRead<Counter<ConcurrencyPolicy::SingleThreaded, AccessPolicy::WriteOnly,
                                         ResetPolicy::Cumulative>>,
              "a WriteOnly counter must not expose read()");
static_assert(!detail::HasSnapshot<Counter<ConcurrencyPolicy::SingleThreaded, AccessPolicy::WriteOnly,
                                            ResetPolicy::Cumulative>>,
              "a WriteOnly counter must not expose snapshot()");

/// Free helper (mandate 9): mirrors the member.
template <nexigen::metrics::ConcurrencyPolicy Concurrency, nexigen::metrics::AccessPolicy Access,
          nexigen::metrics::ResetPolicy Reset>
[[nodiscard]] std::string to_string(
    const nexigen::metrics::Counter<Concurrency, Access, Reset>& counter) {
    return counter.to_string();
}

} // namespace nexigen::metrics


/// Counters format as their diagnostic string (mandate 11).
template <nexigen::metrics::ConcurrencyPolicy Concurrency, nexigen::metrics::AccessPolicy Access,
          nexigen::metrics::ResetPolicy Reset>
struct std::formatter<nexigen::metrics::Counter<Concurrency, Access, Reset>>
    : std::formatter<std::string_view> {
    template <class Ctx>
    auto format(const nexigen::metrics::Counter<Concurrency, Access, Reset>& counter,
                Ctx& ctx) const {
        const std::string text = counter.to_string();
        return std::formatter<std::string_view>::format(text, ctx);
    }
};
