// nexigen::exec — deterministic cycle clock and seeded random
// (change p2-exec-platform-controls).
#include <nexigen/exec/deterministic_client.hpp>

#include <cerrno>
#include <time.h>

#include <nexigen/core/detail/json_field.hpp>

namespace nexigen::exec {

namespace {

[[nodiscard]] std::uint64_t monotonic_now_ns() noexcept {
    timespec now{};
    static_cast<void>(::clock_gettime(CLOCK_MONOTONIC, &now));
    return static_cast<std::uint64_t>(now.tv_sec) * 1'000'000'000ULL +
           static_cast<std::uint64_t>(now.tv_nsec);
}

} // namespace

std::string DeterministicConfig::to_string() const {
    return std::format("DeterministicConfig(cycle_period_ns={}, seed={})", cycle_period_ns, seed);
}

DeterministicClient::DeterministicClient(DeterministicConfig config) noexcept
    : config_(config), start_ns_(monotonic_now_ns()), rng_state_(config.seed) {
    if (config_.cycle_period_ns == 0U) {
        config_.cycle_period_ns = 1'000'000ULL;
    }
}

std::uint64_t DeterministicClient::cycle_time_ns() const noexcept {
    return start_ns_ + cycle_index_ * config_.cycle_period_ns;
}

std::uint64_t DeterministicClient::wait_for_next_cycle() noexcept {
    ++cycle_index_;
    const std::uint64_t deadline_ns = cycle_time_ns();
    timespec deadline{};
    deadline.tv_sec = static_cast<time_t>(deadline_ns / 1'000'000'000ULL);
    deadline.tv_nsec = static_cast<long>(deadline_ns % 1'000'000'000ULL);
    while (::clock_nanosleep(CLOCK_MONOTONIC, TIMER_ABSTIME, &deadline, nullptr) == EINTR) {}
    return cycle_index_;
}

std::uint64_t DeterministicClient::random_u64() noexcept {
    rng_state_ += 0x9e3779b97f4a7c15ULL;
    std::uint64_t value = rng_state_;
    value = (value ^ (value >> 30U)) * 0xbf58476d1ce4e5b9ULL;
    value = (value ^ (value >> 27U)) * 0x94d049bb133111ebULL;
    return value ^ (value >> 31U);
}

std::string DeterministicClient::to_string() const {
    return std::format("DeterministicClient(period_ns={}, seed={}, cycle={})",
                       config_.cycle_period_ns, config_.seed, cycle_index_);
}

void to_json(nlohmann::json& j, const DeterministicConfig& value) {
    j = nlohmann::json{{"cycle_period_ns", value.cycle_period_ns}, {"seed", value.seed}};
}

void from_json(const nlohmann::json& j, DeterministicConfig& value) noexcept {
    static_cast<void>(core::detail::read_unsigned(j, "cycle_period_ns", value.cycle_period_ns));
    static_cast<void>(core::detail::read_unsigned(j, "seed", value.seed));
}

} // namespace nexigen::exec
