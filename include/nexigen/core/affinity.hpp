// nexigen::core — thread affinity for the observability modules
// (change dedup-affinity-core; spec core/affinity).
//
// ONE inline implementation of the uint64-core-mask surface
// (`pin_self` / `self_affinity`) that the chrono/logging/metrics/tracing
// modules used to carry as four byte-identical private copies
// (`settings_io.hpp` "three near-identical copy groups collapse into one"
// precedent — affinity is the remaining group of that wave). The
// per-module `<module>/detail/affinity.hpp` headers are forward-only shims.
//
// DETAIL DISCLAIMER (mirrors settings_io.hpp and fixed_writer.hpp): the
// namespace keeps a dedicated name (`nexigen::core::affinity`) so test
// scopes that `using namespace nexigen::core` never learn a SECOND bare
// `detail` candidate name.
//
// Calling convention: callers run these ON THE THREAD TO BE PINNED — the
// worker pins itself as the first act inside its body, because
// std::jthread creation is asynchronous and pinning from the spawning
// thread could pin the wrong schedule window.
//
// Cold-path scope: pinning/querying runs only at worker/provider start
// (never inside a producer loop). `_np` syscalls are glibc extensions and
// need `_GNU_SOURCE`; the build-tuning INTERFACE definition is attached to
// the nexigen_core target for every consumer.
#pragma once

#include <cstdint>

#if !defined(_GNU_SOURCE) && defined(__linux__)
#error "nexigen::core::affinity requires _GNU_SOURCE (pthread_*affinity_np are glibc extensions)"
#endif

#include <pthread.h>
#include <sched.h>

#include <nexigen/core/result.hpp>
#include <nexigen/core/thread_management.hpp>

namespace nexigen::core::affinity {

namespace {

/// @brief Number of cores representable by the legacy uint64 mask (64).
constexpr std::size_t mask_bits = 8U * sizeof(std::uint64_t);

/// @brief Convert a native CPU set into the 64-bit legacy mask.
///
/// @param set Native `cpu_set_t` to read.
/// @return Mask with bit `i` set when CPU `i` belongs to `set`; CPUs above 63
///         are outside the uint64 domain and silently dropped.
std::uint64_t cpuset_to_mask(const cpu_set_t& set) noexcept {
    std::uint64_t mask = 0ULL;
    for (std::size_t bit = 0; bit < mask_bits; ++bit) {
        if (CPU_ISSET(bit, &set)) {
            mask |= (1ULL << bit);
        }
    }
    return mask;
}

} // namespace

/// @brief Pin the calling thread to the CPUs selected by `core_mask`.
///
/// Bit `i` selects CPU `i`. The mask is converted through the shared
/// `core/thread-management` machinery and applied with
/// `pthread_setaffinity_np` to the calling thread only; `noexcept`,
/// allocation-free, cold-path only (worker or provider start).
///
/// @param core_mask Bit mask of CPUs to pin to; bit `i` means CPU `i`.
/// @return `Status{}` on success; `InvalidArgument` for a zero mask (no syscall
///         runs and the affinity is untouched); `SystemError` when the pthread
///         call fails, including a mask naming cores the process may not use.
/// @note Spec `core/affinity`, requirement "Pinning the calling thread by core
///       mask".
/// @warning Call it on the thread to be pinned as that thread's first act;
///          never call `.value()` on the returned status (see `result.hpp`).
[[nodiscard]] inline core::Status pin_self(std::uint64_t core_mask) noexcept {
    if (core_mask == 0ULL) {
        return std::unexpected(core::ErrorCode::InvalidArgument);
    }
    cpu_set_t set{};
    thread_management_detail::fill_native_cpu_set(CpuSet::from_core_mask(core_mask), set);
    if (::pthread_setaffinity_np(::pthread_self(), sizeof(set), &set) != 0) {
        return std::unexpected(core::ErrorCode::SystemError);
    }
    return {};
}

/// @brief Read the calling thread's current affinity into a uint64 mask.
///
/// Cores above bit 63 are unrepresentable in the uint64 mask and silently
/// dropped; `noexcept`, allocation-free, cold-path only.
///
/// @param core_mask_out Receives the affinity mask; untouched when the call
///        fails.
/// @return `Status{}` on success; `SystemError` when `pthread_getaffinity_np`
///         fails.
/// @note Spec `core/affinity`, requirement "Querying the calling thread's
///       affinity".
/// @warning Call it on the thread whose mask is requested; never call
///          `.value()` on the returned status (see `result.hpp`).
[[nodiscard]] inline core::Status self_affinity(std::uint64_t& core_mask_out) noexcept {
    cpu_set_t set{};
    if (::pthread_getaffinity_np(::pthread_self(), sizeof(set), &set) != 0) {
        return std::unexpected(core::ErrorCode::SystemError);
    }
    core_mask_out = cpuset_to_mask(set);
    return {};
}

} // namespace nexigen::core::affinity
