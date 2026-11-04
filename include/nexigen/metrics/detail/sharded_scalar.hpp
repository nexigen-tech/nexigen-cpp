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

/// One 64-byte-aligned atomic scalar shard (own cache line, no false sharing).
template <typename ValueType>
struct alignas(64) ScalarShard {
    std::atomic<ValueType> value{0};
};

/// Multi-threaded scalar storage selected by the instrument kind. ValueType is
/// std::uint64_t (counter) or std::int64_t / double (gauge); `add` is the
/// wait-free recording path, `observe`/`take` are the aggregation paths.
template <typename ValueType, MetricKind Kind, ResetPolicy Reset>
class ShardedScalar {
    static_assert(std::is_trivially_copyable_v<ValueType>,
                  "scalar values are trivially copyable");

public:
    void add(ValueType delta) noexcept {
        shards_[thread_slot(Kind)].value.fetch_add(delta, std::memory_order_relaxed);
    }

    [[nodiscard]] ValueType observe() const noexcept {
        ValueType total{};
        for (const auto& shard : shards_) {
            total = static_cast<ValueType>(
                total + shard.value.load(std::memory_order_relaxed));
        }
        return total;
    }

    [[nodiscard]] ValueType take() const noexcept {
        ValueType total{};
        for (auto& shard : shards_) {
            if constexpr (Reset == ResetPolicy::ResetOnRead) {
                total = static_cast<ValueType>(
                    total + shard.value.exchange(ValueType{0}, std::memory_order_relaxed));
            } else {
                total = static_cast<ValueType>(
                    total + shard.value.load(std::memory_order_relaxed));
            }
        }
        return total;
    }

    /// Control-plane reassignment (gauge `set`, spec counter-gauge "set
    /// moving the aggregate to the target value as observed at call
    /// time"): shard 0 takes the whole target value, every other shard —
    /// including the shared fallback cell — is zeroed. A racing `add` on
    /// another shard lands either before that shard's store (then it is
    /// folded into the reassigned aggregate) or after (then it shows on
    /// top of the target) — no check-then-act delta that a concurrent
    /// writer could make stale. `set` stays a bounded control-plane pass;
    /// the hot `add` path keeps its relaxed `fetch_add` untouched.
    void assign(ValueType value) noexcept {
        shards_[0].value.store(value, std::memory_order_relaxed);
        for (std::size_t i = 1; i < shard_count; ++i) {
            shards_[i].value.store(ValueType{}, std::memory_order_relaxed);
        }
    }

private:
    mutable ScalarShard<ValueType> shards_[shard_count]{};
};

} // namespace nexigen::metrics::detail
