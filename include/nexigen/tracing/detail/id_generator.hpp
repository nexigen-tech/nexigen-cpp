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

/// Per-thread cached Linux thread id (same contract as the logging/metrics
/// modules' helpers — module-independence rule): the SYS_gettid syscall runs
/// once per thread on first use; steady-state calls read the thread_local.
/// TIDs are positive, so 0 doubles as "unresolved" — no separate flag byte.
[[nodiscard]] inline std::uint32_t cached_thread_id() noexcept {
    thread_local std::uint32_t tid = 0U;
    if (tid == 0U) {
        tid = static_cast<std::uint32_t>(::syscall(SYS_gettid));
    }
    return tid;
}

/// splitmix64 finalizer: decorrelates the seed inputs (thread id, process
/// counter, constant) into a well-distributed xorshift state.
[[nodiscard]] constexpr std::uint64_t splitmix64(std::uint64_t x) noexcept {
    x += 0x9E3779B97F4A7C15ULL;
    x = (x ^ (x >> 30U)) * 0xBF58476D1CE4E5B9ULL;
    x = (x ^ (x >> 27U)) * 0x94D049BB133111EBULL;
    return x ^ (x >> 31U);
}

/// Process-wide seeding counter: distinct threads seeding concurrently get
/// distinct mixes. Relaxed only — it exists to diversify seeds, never to
/// synchronize.
inline std::atomic<std::uint64_t> seed_counter{0};

/// Combine the thread id and the seeding counter INJECTIVELY before mixing:
/// tid occupies the high 32 bits (SYS_gettid is a positive uint32), the
/// counter the low 32 (a process would need 2^32 thread creations to wrap).
/// A plain `tid ^ counter` would collide whenever tid1^tid2 == c1^c2 — with
/// consecutive tids and small counters that happens constantly; the packed
/// pairing guarantees disjoint seed inputs, and splitmix64 (a bijection)
/// keeps the seeds themselves disjoint (design D6: "distinctness across
/// threads follows from disjoint seeds").
[[nodiscard]] inline std::uint64_t seed_input() noexcept {
    const std::uint64_t tid = static_cast<std::uint64_t>(cached_thread_id()) << 32U;
    const std::uint64_t count = seed_counter.fetch_add(1U, std::memory_order_relaxed);
    return (tid | (count & 0xFFFF'FFFFULL)) ^ 0x9E3779B97F4A7C15ULL;
}

/// Non-zero wait-free id source (one xorshift64* round per call).
class Id_generator {
public:
    Id_generator() noexcept {
        // Seed once per thread on first use (cold): the injective (tid,
        // counter) pairing through splitmix64, forced odd (nonzero).
        state_ = splitmix64(seed_input()) | 1ULL;
    }

    /// One round of xorshift64* — the generated value is never zero (state is
    /// odd and the transform is a bijection on nonzero states).
    [[nodiscard]] std::uint64_t next() noexcept {
        std::uint64_t x = state_;
        x ^= x >> 12U;
        x ^= x << 25U;
        x ^= x >> 27U;
        state_ = x;
        return x * 0x2545F4914F6CDD1DULL;
    }

private:
    std::uint64_t state_{1};
};

/// The thread's own generator (created on first use, alive for the thread).
[[nodiscard]] inline std::uint64_t next_id() noexcept {
    thread_local Id_generator generator{};
    return generator.next();
}

} // namespace nexigen::tracing::detail
