// nexigen::chrono — seqlock over the published time-base block (change
// add-time-base-service; spec chrono/time-base "Shared-memory publication
// contract"; design D-C).
//
// One writer, many readers, no reader lock: the writer bumps the block's
// generation to an odd value with release ordering, rewrites the body, bumps it
// back to even, then publishes the heartbeat. A reader samples the generation,
// copies the block, and accepts the copy only when the generation did not move.
// A reader never blocks the writer and never observes a half-written table.
//
// A crashed writer can leave the generation odd forever. The reader therefore
// retries a bounded number of times and reports failure; the segment layer
// (and the stale-heartbeat rule) treats that as a lost base rather than
// spinning.
//
// Memory orders: generation writes are `release`, generation reads are
// `acquire`, and an acquire fence separates the body copy from the validating
// read. The heartbeat is a separate `relaxed` atomic on its own cache line, so
// freshness can be tested without copying the table.
#pragma once

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <cstring>

#include <nexigen/chrono/time_base_block.hpp>

namespace nexigen::chrono::detail {

#if defined(__SANITIZE_THREAD__)
extern "C" {
/// @brief ThreadSanitizer acquire annotation.
/// @param address Address the reader acquires.
void __tsan_acquire(void* address);
/// @brief ThreadSanitizer release annotation.
/// @param address Address the writer releases.
void __tsan_release(void* address);
} // extern "C"
#endif

/// @brief Tell TSan that the following reads happen after the writer's release.
/// @param address Synchronization address (the block).
inline void seqlock_acquire(const void* address) noexcept {
#if defined(__SANITIZE_THREAD__)
    __tsan_acquire(const_cast<void*>(address));
#else
    static_cast<void>(address);
#endif
}

/// @brief Tell TSan that the preceding writes are published.
/// @param address Synchronization address (the block).
inline void seqlock_release(const void* address) noexcept {
#if defined(__SANITIZE_THREAD__)
    __tsan_release(const_cast<void*>(address));
#else
    static_cast<void>(address);
#endif
}

#if defined(__SANITIZE_THREAD__)
/// @brief ThreadSanitizer cannot model a seqlock's mixed atomic/plain access.
#define NEXIGEN_SEQLOCK_NO_TSAN __attribute__((no_sanitize_thread))
#else
/// @brief No-op outside a ThreadSanitizer build.
#define NEXIGEN_SEQLOCK_NO_TSAN
#endif

/// @brief Bounded reader retry budget before the block is reported unstable.
inline constexpr unsigned time_base_read_attempts = 64U;

static_assert(std::atomic_ref<std::uint64_t>::is_always_lock_free,
              "the time-base seqlock requires a lock-free 64-bit atomic");

/// @brief The single writer of a mapped time-base block.
///
/// Owns no storage: it borrows the block the daemon mapped and serializes every
/// publish through the generation word. `publish()` must come from one thread.
///
/// @note Spec `chrono/time-base`, requirement "Shared-memory publication
///       contract".
class TimeBaseWriter {
public:
    /// @brief Borrow the block this writer publishes into.
    ///
    /// @param block Mapped block storage; it must outlive the writer.
    explicit TimeBaseWriter(TimeBaseBlock& block) noexcept : block_(block) {}

    /// @brief Publish a complete block atomically from a reader's point of view.
    ///
    /// Copies every field except `generation` from `staged`, brackets the copy
    /// with the odd/even generation pair, then stores the heartbeat.
    ///
    /// @param staged Complete block to publish; its `generation` is ignored.
    /// @param heartbeat_ns Canonical instant of this publication.
    /// @note Cold path only (the daemon publish loop); no allocation.
    NEXIGEN_SEQLOCK_NO_TSAN void publish(const TimeBaseBlock& staged,
                                         std::uint64_t heartbeat_ns) noexcept {
        std::atomic_ref<std::uint64_t> generation(block_.generation);
        std::atomic_ref<std::uint64_t> heartbeat(block_.heartbeat_ns);

        generation.fetch_add(1U, std::memory_order_release); // odd: writing

        block_.magic = staged.magic;
        block_.version = staged.version;
        block_.capacity = staged.capacity;
        block_.status = staged.status;
        block_.sync_mode = staged.sync_mode;
        block_.service_flags = staged.service_flags;
        block_.domain_count = staged.domain_count;
        block_.boot_token = staged.boot_token;
        block_.tai_offset_ns = staged.tai_offset_ns;
        block_.utc_offset_ns = staged.utc_offset_ns;
        block_.utc_unc_ns = staged.utc_unc_ns;
        block_.rows = staged.rows;
        block_.last_snapshot = staged.last_snapshot;

        generation.fetch_add(1U, std::memory_order_release); // even: stable
        seqlock_release(&block_);
        heartbeat.store(heartbeat_ns, std::memory_order_relaxed);
    }

private:
    TimeBaseBlock& block_; ///< Borrowed mapped block.
};

/// @brief A read-only view of a mapped time-base block.
///
/// Owns no storage; holds a pointer to the mapping. `read()` is safe to call
/// concurrently with `publish()` and performs no allocation; `generation()` and
/// `heartbeat_ns()` are single relaxed loads for cheap staleness checks.
///
/// @note Spec `chrono/time-base`, requirement "Shared-memory publication
///       contract".
class TimeBaseReader {
public:
    /// @brief Borrow the block this reader samples.
    ///
    /// @param block Mapped block storage; it must outlive the reader.
    explicit TimeBaseReader(const TimeBaseBlock& block) noexcept : block_(&block) {}

    /// @brief Copy a complete block, retrying while a write is in flight.
    ///
    /// @param out Destination for the copy; untouched when the read fails.
    /// @return `true` when a stable copy was taken, `false` when the writer held
    ///         the generation odd for the whole retry budget or the block
    ///         identity is wrong.
    /// @note Hot path safe: a handful of loads plus one 4224-byte copy.
    [[nodiscard]] NEXIGEN_SEQLOCK_NO_TSAN bool read(TimeBaseBlock& out) const noexcept {
        for (unsigned attempt = 0; attempt < time_base_read_attempts; ++attempt) {
            const std::uint64_t before = generation();
            if ((before & 1U) != 0U) {
                continue; // writer in flight
            }
            seqlock_acquire(block_);
            // Copy around the two atomic words so no plain access touches them.
            char* const destination = reinterpret_cast<char*>(&out);
            const char* const source = reinterpret_cast<const char*>(block_);
            std::memcpy(destination, source, offsetof(TimeBaseBlock, generation));
            out.generation = before;
            std::memcpy(destination + offsetof(TimeBaseBlock, generation) + sizeof(std::uint64_t),
                        source + offsetof(TimeBaseBlock, generation) + sizeof(std::uint64_t),
                        offsetof(TimeBaseBlock, heartbeat_ns) -
                            offsetof(TimeBaseBlock, generation) - sizeof(std::uint64_t));
            out.heartbeat_ns = heartbeat_ns();
            std::memcpy(destination + offsetof(TimeBaseBlock, reserved1),
                        source + offsetof(TimeBaseBlock, reserved1),
                        sizeof(TimeBaseBlock) - offsetof(TimeBaseBlock, reserved1));
#if !defined(__SANITIZE_THREAD__)
            // ThreadSanitizer does not model standalone fences; the generation
            // load below still gives the runtime its acquire edge.
            std::atomic_thread_fence(std::memory_order_acquire);
#endif
            const std::uint64_t after = generation();
            if (before == after) {
                return out.magic == time_base_magic && out.version == time_base_version;
            }
        }
        return false;
    }

    /// @brief The current generation word (one relaxed load).
    ///
    /// @return Generation value; odd means a write is in flight.
    /// @note Hot path safe.
    [[nodiscard]] std::uint64_t generation() const noexcept {
        return std::atomic_ref<std::uint64_t>(const_cast<std::uint64_t&>(block_->generation))
            .load(std::memory_order_acquire);
    }

    /// @brief The last published heartbeat (one relaxed load).
    ///
    /// @return Canonical nanoseconds of the last successful publish.
    /// @note Hot path safe.
    [[nodiscard]] std::uint64_t heartbeat_ns() const noexcept {
        return std::atomic_ref<std::uint64_t>(const_cast<std::uint64_t&>(block_->heartbeat_ns))
            .load(std::memory_order_relaxed);
    }

private:
    const TimeBaseBlock* block_; ///< Borrowed mapped block.
};

} // namespace nexigen::chrono::detail
