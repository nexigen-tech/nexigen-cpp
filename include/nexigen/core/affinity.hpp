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

namespace nexigen::core::affinity {

namespace {

constexpr std::size_t mask_bits = 8U * sizeof(std::uint64_t); // 64 representable cores

void mask_to_cpuset(std::uint64_t mask, cpu_set_t& set) noexcept {
    CPU_ZERO(&set);
    for (std::size_t bit = 0; bit < mask_bits; ++bit) {
        if ((mask & (1ULL << bit)) != 0ULL) {
            CPU_SET(bit, &set);
        }
    }
}

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

/// Pin the calling thread to the cores set in core_mask (bit i == CPU i).
/// core_mask == 0: InvalidArgument without touching the affinity. pthread
/// failure (or a mask naming cores beyond the allowed set): SystemError.
[[nodiscard]] inline core::Status pin_self(std::uint64_t core_mask) noexcept {
    if (core_mask == 0ULL) {
        return std::unexpected(core::ErrorCode::InvalidArgument);
    }
    cpu_set_t set{};
    mask_to_cpuset(core_mask, set);
    if (::pthread_setaffinity_np(::pthread_self(), sizeof(set), &set) != 0) {
        return std::unexpected(core::ErrorCode::SystemError);
    }
    return {};
}

/// Read the calling thread's current affinity into core_mask_out. Cores above
/// bit 63 are unrepresentable in the uint64_t mask and silently dropped.
/// On pthread failure: SystemError.
[[nodiscard]] inline core::Status self_affinity(std::uint64_t& core_mask_out) noexcept {
    cpu_set_t set{};
    if (::pthread_getaffinity_np(::pthread_self(), sizeof(set), &set) != 0) {
        return std::unexpected(core::ErrorCode::SystemError);
    }
    core_mask_out = cpuset_to_mask(set);
    return {};
}

} // namespace nexigen::core::affinity
