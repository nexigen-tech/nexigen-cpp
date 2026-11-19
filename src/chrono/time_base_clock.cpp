// nexigen::chrono — process-wide published time-base view implementation
// (change add-time-base-service).
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <new>
#include <optional>
#include <string>
#include <string_view>
#include <utility>

#include <nexigen/chrono/detail/time_base_segment.hpp>
#include <nexigen/chrono/detail/time_base_seqlock.hpp>
#include <nexigen/chrono/time_base_block.hpp>
#include <nexigen/chrono/time_base_clock.hpp>
#include <nexigen/core/result.hpp>

namespace nexigen::chrono {
namespace {

/// @brief One bound mapping plus the seqlock reader over it.
struct State {
    /// @brief Adopt a mapped segment.
    ///
    /// @param mapped Attached read-only segment.
    explicit State(detail::TimeBaseSegment mapped)
        : segment(std::move(mapped)), reader(*segment.block()), name(segment.name()) {}

    detail::TimeBaseSegment segment; ///< Owns the read-only mapping.
    detail::TimeBaseReader reader;   ///< Seqlock reader over the mapping.
    std::string name;                ///< POSIX name for diagnostics.
};

/// @brief Per-thread copy of the last complete block seen.
struct Cache {
    const State* state{nullptr}; ///< Binding the cache belongs to.
    std::uint64_t epoch{0};      ///< Binding epoch (guards against pointer reuse).
    std::uint64_t generation{0}; ///< Generation of `block`.
    bool valid{false};           ///< `block` holds a complete block.
    TimeBaseBlock block{};       ///< Cached block.
};

/// @brief Inline storage for the one bound state; no heap on any path.
alignas(State) std::byte g_storage[sizeof(State)];
std::atomic<State*> g_state{nullptr};
std::atomic<std::uint64_t> g_binding_epoch{0};

[[nodiscard]] Cache& cache() noexcept {
    static thread_local Cache value;
    return value;
}

/// @brief Refresh the thread cache when the binding or generation moved.
///
/// @param state Bound state.
/// @param epoch Current binding epoch.
/// @return The cached block, or `nullptr` when no complete read succeeded.
[[nodiscard]] const TimeBaseBlock* refreshed(const State* state, std::uint64_t epoch) noexcept {
    Cache& entry = cache();
    if (entry.state != state || entry.epoch != epoch) {
        entry.state = state;
        entry.epoch = epoch;
        entry.valid = false;
    }
    if (!entry.valid || entry.generation != state->reader.generation()) {
        TimeBaseBlock fresh{};
        if (state->reader.read(fresh)) {
            entry.block = fresh;
            entry.generation = fresh.generation;
            entry.valid = true;
        }
    }
    return entry.valid ? &entry.block : nullptr;
}

} // namespace

core::Status TimeBaseClock::attach(std::string_view segment_name) noexcept {
    if (g_state.load(std::memory_order_acquire) != nullptr) {
        return {};
    }
    auto attached = detail::TimeBaseSegment::attach(segment_name);
    if (!attached) {
        return std::unexpected(attached.error());
    }
    auto* const state = new (g_storage) State(std::move(*attached));
    TimeBaseBlock probe{};
    if (!state->reader.read(probe)) {
        state->~State();
        return std::unexpected(core::ErrorCode::CorruptData);
    }
    g_binding_epoch.fetch_add(1U, std::memory_order_acq_rel);
    g_state.store(state, std::memory_order_release);
    return {};
}

void TimeBaseClock::detach() noexcept {
    // Detach is a shutdown/rollback action: callers stop their workers first.
    // The epoch bump makes every per-thread cache drop its pointer before the
    // state is freed.
    g_binding_epoch.fetch_add(1U, std::memory_order_acq_rel);
    State* const state = g_state.exchange(nullptr, std::memory_order_acq_rel);
    if (state != nullptr) {
        state->~State();
    }
}

bool TimeBaseClock::attached() noexcept {
    return g_state.load(std::memory_order_acquire) != nullptr;
}

std::optional<std::int64_t> TimeBaseClock::utc_from_canonical(std::int64_t canonical_ns) noexcept {
    const State* const state = g_state.load(std::memory_order_acquire);
    if (state == nullptr || canonical_ns < 0) {
        return std::nullopt;
    }
    const TimeBaseBlock* const block =
        refreshed(state, g_binding_epoch.load(std::memory_order_acquire));
    if (block == nullptr) {
        return std::nullopt;
    }
    const DomainCoefficient* const realtime = block->find(k_realtime_domain_id);
    if (realtime == nullptr || !realtime->has_reverse() ||
        realtime->lock == LockState::Unsupported ||
        !contains_canonical(*realtime, static_cast<std::uint64_t>(canonical_ns))) {
        return std::nullopt;
    }
    return static_cast<std::int64_t>(native_from_canonical(*realtime, canonical_ns));
}

std::optional<std::uint64_t> TimeBaseClock::boot_token() noexcept {
    const State* const state = g_state.load(std::memory_order_acquire);
    if (state == nullptr) {
        return std::nullopt;
    }
    const TimeBaseBlock* const block =
        refreshed(state, g_binding_epoch.load(std::memory_order_acquire));
    if (block == nullptr) {
        return std::nullopt;
    }
    return block->boot_token;
}

std::string_view TimeBaseClock::segment_name() noexcept {
    const State* const state = g_state.load(std::memory_order_acquire);
    return state != nullptr ? std::string_view{state->name} : std::string_view{};
}

} // namespace nexigen::chrono
