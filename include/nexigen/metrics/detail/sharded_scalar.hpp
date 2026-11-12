// nexigen::metrics — shared multi-threaded scalar storage (detail).
//
// Counter and Gauge both shard a scalar value per recording thread: 32
// exclusive per-thread cells plus one shared fallback, addressed through
// detail::thread_slot(kind). Recording is a relaxed fetch_add on the caller's
// own shard; observe()/take() aggregate over all shards, with take() clearing
// the window under ResetOnRead (exchange-on-read) so every increment lands in
// exactly one window. ShardedScalar factors that aggregate/clear logic out of
// the two instruments so it lives in one place.
#pragma once

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <type_traits>

#include <nexigen/metrics/detail/thread_slot.hpp>
#include <nexigen/metrics/policy_enums.hpp>

namespace nexigen::metrics::detail {

/// @brief One 64-byte-aligned atomic scalar cell used as a per-thread shard.
///
/// Cache-line alignment keeps threads that record into different shards off one
/// another's cache lines, so the relaxed `fetch_add` hot path never contends on
/// a shared line.
///
/// @tparam ValueType Trivially copyable arithmetic type stored in the shard.
/// @note Spec `metrics/counter-gauge`, requirement "Counter multi-threaded
///       sharding".
template <typename ValueType> struct alignas(64) ScalarShard {
    /// Atomic cell; relaxed operations only, since exactness comes from sharding.
    std::atomic<ValueType> value{0};
};

/// @brief Multi-threaded scalar storage selected by the instrument kind.
///
/// Shards one scalar per recording thread: 32 exclusive per-thread cells plus
/// one shared fallback, addressed through `detail::thread_slot(kind)`. `add` is
/// the wait-free recording path (a relaxed `fetch_add` on the caller's own
/// shard); `observe` and `take` aggregate over all shards. With
/// `ResetPolicy::ResetOnRead`, `take` clears each shard with an exchange so every
/// increment lands in exactly one window.
///
/// @tparam ValueType Stored value type: `std::uint64_t` for counters,
///         `std::int64_t` or `double` for gauges; must be trivially copyable
///         (pinned by a `static_assert`).
/// @tparam Kind Instrument kind, used to select the per-kind thread slot.
/// @tparam Reset Window policy: `Cumulative` reads without clearing,
///         `ResetOnRead` exchanges each shard on `take`.
/// @note Spec `metrics/counter-gauge`, requirement "Counter multi-threaded
///       sharding" and requirement "Counter reset windows".
template <typename ValueType, MetricKind Kind, ResetPolicy Reset> class ShardedScalar {
    static_assert(std::is_trivially_copyable_v<ValueType>, "scalar values are trivially copyable");

public:
    /// @brief Record one delta into this thread's shard.
    ///
    /// @param delta Value to add; for gauges it is the signed change.
    /// @note Wait-free: a single relaxed `fetch_add`, no lock and no allocation
    ///       (spec `metrics/counter-gauge`, requirement "Counter multi-threaded
    ///       sharding").
    void add(ValueType delta) noexcept {
        shards_[thread_slot(Kind)].value.fetch_add(delta, std::memory_order_relaxed);
    }

    /// @brief Aggregate every shard without modifying the window.
    ///
    /// @return Sum over all 33 cells at the time of the call; repeated calls
    ///         without intervening `add` return the same total.
    /// @note Spec `metrics/counter-gauge`, requirement "Counter reset windows".
    [[nodiscard]] ValueType observe() const noexcept {
        ValueType total{};
        for (const auto& shard : shards_) {
            total = static_cast<ValueType>(total + shard.value.load(std::memory_order_relaxed));
        }
        return total;
    }

    /// @brief Aggregate every shard, clearing the window under `ResetOnRead`.
    ///
    /// @return Sum over all cells; with `ResetOnRead` each cell is exchanged to
    ///         zero, so a subsequent `take` reports only later increments.
    /// @note Spec `metrics/counter-gauge`, requirement "Counter reset windows";
    ///       increments racing the exchange land in exactly one window.
    [[nodiscard]] ValueType take() const noexcept {
        ValueType total{};
        for (auto& shard : shards_) {
            if constexpr (Reset == ResetPolicy::ResetOnRead) {
                total = static_cast<ValueType>(
                    total + shard.value.exchange(ValueType{0}, std::memory_order_relaxed));
            } else {
                total = static_cast<ValueType>(total + shard.value.load(std::memory_order_relaxed));
            }
        }
        return total;
    }

    /// @brief Reassign the aggregate to one target value (gauge `set`).
    ///
    /// Shard 0 takes the whole target value and every other shard — including
    /// the shared fallback cell — is zeroed. A racing `add` on another shard
    /// lands either before that shard's store (then it is folded into the
    /// reassigned aggregate) or after (then it shows on top of the target), so
    /// there is no check-then-act delta that a concurrent writer could make
    /// stale. `set` stays a bounded control-plane pass; the hot `add` path keeps
    /// its relaxed `fetch_add` untouched.
    ///
    /// @param value Target value the observed aggregate takes at call time.
    /// @note Spec `metrics/counter-gauge`, requirement "Gauge recording surface"
    ///       (set establishes the observed aggregate).
    void assign(ValueType value) noexcept {
        shards_[0].value.store(value, std::memory_order_relaxed);
        for (std::size_t i = 1; i < shard_count; ++i) {
            shards_[i].value.store(ValueType{}, std::memory_order_relaxed);
        }
    }

private:
    /// The full shard set: `shard_count` cells, index 32 being the fallback.
    mutable ScalarShard<ValueType> shards_[shard_count]{};
};

} // namespace nexigen::metrics::detail
