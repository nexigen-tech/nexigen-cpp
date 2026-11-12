// nexigen::tracing — identifier generation (design D6; spec
// tracing/span-context "Trace and span identifier generation").
//
// The ONLY id source for TraceId/SpanId: a non-zero thread_local
// xorshift64* state seeded once per thread on first use. splitmix64 mixes the
// cached Linux thread id, a process-wide counter (one relaxed fetch_add per
// seeding — cold), and the golden-ratio constant; the state is forced odd so
// zero is unreachable. Generation itself is one xorshift64* round: register
// work only — no lock, no allocation, no cross-thread atomic. Distinctness
// across threads follows from disjoint seeds; distinctness within a thread
// follows from the xorshift period (2^64 - 1).
//
// The all-zero value stays reserved as the "unset" sentinel: the wrappers
// default to it and the generator can never produce it.
#pragma once

#include <atomic>
#include <cstdint>

#include <sys/syscall.h>
#include <unistd.h>

namespace nexigen::tracing::detail {

/// @brief Per-thread cached Linux thread id.
///
/// The `SYS_gettid` syscall runs once per thread on first use; steady-state
/// calls only read the `thread_local`. Same contract as the logging and
/// metrics helpers (module-independence rule). TIDs are positive, so `0`
/// doubles as "unresolved" and no separate flag byte is needed.
///
/// @return The calling thread's kernel thread id, never `0` in practice.
/// @note Spec `tracing/span-context`, requirement "Trace and span identifier
///       generation". Cold path: one syscall per thread, then a `thread_local`
///       load.
[[nodiscard]] inline std::uint32_t cached_thread_id() noexcept {
    thread_local std::uint32_t tid = 0U;
    if (tid == 0U) {
        tid = static_cast<std::uint32_t>(::syscall(SYS_gettid));
    }
    return tid;
}

/// @brief splitmix64 finalizer.
///
/// Decorrelates the seed inputs (thread id, process counter, golden-ratio
/// constant) into a well-distributed xorshift state. A bijection on 64-bit
/// values, so disjoint inputs stay disjoint.
///
/// @param x Seed input to mix.
/// @return The mixed 64-bit value.
/// @note Spec `tracing/span-context`, requirement "Trace and span identifier
///       generation"; design D6.
[[nodiscard]] constexpr std::uint64_t splitmix64(std::uint64_t x) noexcept {
    x += 0x9E3779B97F4A7C15ULL;
    x = (x ^ (x >> 30U)) * 0xBF58476D1CE4E5B9ULL;
    x = (x ^ (x >> 27U)) * 0x94D049BB133111EBULL;
    return x ^ (x >> 31U);
}

/// @brief Process-wide seeding counter.
///
/// Distinct threads seeding concurrently get distinct mixes. Relaxed only: it
/// exists to diversify seeds and never to synchronize; the single
/// `fetch_add` per thread seeding is a cold path.
///
/// @note Spec `tracing/span-context`, requirement "Trace and span identifier
///       generation"; design D6.
inline std::atomic<std::uint64_t> seed_counter{0};

/// @brief Build a distinct seed input for one thread's first use.
///
/// Combines `cached_thread_id()` and the seeding counter injectively before
/// mixing: the tid occupies the high 32 bits (a positive `uint32`) and the
/// counter the low 32 (a process would need `2^32` thread creations to wrap).
/// A plain XOR of tid and counter would collide whenever `tid1 ^ tid2 ==
/// c1 ^ c2`, which happens constantly with consecutive tids; the packed
/// pairing guarantees disjoint inputs. splitmix64 then keeps mixed seeds
/// disjoint, so distinctness across threads follows from disjoint seeds.
///
/// @return Mixed, per-use-distinct 64-bit seed input.
/// @note Spec `tracing/span-context`, requirement "Trace and span identifier
///       generation"; design D6. Contains the only relaxed atomic on the
///       seeding path.
[[nodiscard]] inline std::uint64_t seed_input() noexcept {
    const std::uint64_t tid = static_cast<std::uint64_t>(cached_thread_id()) << 32U;
    const std::uint64_t count = seed_counter.fetch_add(1U, std::memory_order_relaxed);
    return (tid | (count & 0xFFFF'FFFFULL)) ^ 0x9E3779B97F4A7C15ULL;
}

/// @brief Non-zero wait-free identifier source.
///
/// Holds one `thread_local` xorshift64* state, seeded once per thread on first
/// use. Generation is a single xorshift64* round: register work only, with no
/// lock, allocation, or cross-thread atomic. Distinctness within a thread
/// follows from the xorshift period (`2^64 - 1`); the all-zero value stays
/// reserved as the "unset" sentinel and is unreachable because the forced-odd
/// state and the invertible transform never produce it.
///
/// @note Spec `tracing/span-context`, requirement "Trace and span identifier
///       generation"; design D6.
class IdGenerator {
public:
    /// @brief Seed this thread's generator.
    ///
    /// Runs on first use only (cold): packs the injective `(tid, counter)`
    /// pair through splitmix64 and forces the state odd, which also makes it
    /// non-zero.
    IdGenerator() noexcept {
        // Seed once per thread on first use (cold): the injective (tid,
        // counter) pairing through splitmix64, forced odd (nonzero).
        state_ = splitmix64(seed_input()) | 1ULL;
    }

    /// @brief Produce the next identifier.
    ///
    /// One round of xorshift64*: no lock, no allocation, no cross-thread
    /// atomic, bounded steps.
    ///
    /// @return A non-zero 64-bit value; the all-zero "unset" sentinel is never
    ///         produced.
    /// @note Spec `tracing/span-context`, requirement "Trace and span
    ///       identifier generation"; design D6.
    [[nodiscard]] std::uint64_t next() noexcept {
        std::uint64_t x = state_;
        x ^= x >> 12U;
        x ^= x << 25U;
        x ^= x >> 27U;
        state_ = x;
        return x * 0x2545F4914F6CDD1DULL;
    }

private:
    std::uint64_t state_{1}; ///< xorshift64* state; forced odd and non-zero.
};

/// @brief The calling thread's own generator.
///
/// Created on first use and alive for the thread; the seed is never shared
/// with another thread.
///
/// @return A fresh non-zero identifier from the calling thread's generator.
/// @note Spec `tracing/span-context`, requirement "Trace and span identifier
///       generation"; design D6.
[[nodiscard]] inline std::uint64_t next_id() noexcept {
    thread_local IdGenerator generator{};
    return generator.next();
}

} // namespace nexigen::tracing::detail
