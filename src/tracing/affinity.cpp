// nexigen::tracing — affinity implementation (design D2). See
// detail/affinity.hpp for the contract. Requires _GNU_SOURCE (target-level).
#include <cerrno>
#include <cstddef>
#include <cstdint>

#include <pthread.h>
#include <sched.h>

#include <nexigen/core/result.hpp>
#include <nexigen/tracing/detail/affinity.hpp>

namespace nexigen::tracing::detail {
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

core::Status pin_self(std::uint64_t core_mask) noexcept {
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

core::Status self_affinity(std::uint64_t& core_mask_out) noexcept {
    cpu_set_t set{};
    if (::pthread_getaffinity_np(::pthread_self(), sizeof(set), &set) != 0) {
        return std::unexpected(core::ErrorCode::SystemError);
    }
    core_mask_out = cpuset_to_mask(set);
    return {};
}

} // namespace nexigen::tracing::detail
