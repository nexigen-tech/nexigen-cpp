// nexigen::exec — deterministic cycle clock and seeded random
// (change p2-exec-platform-controls; spec exec/deterministic-execution).
//
// Allocation-free, single-threaded by contract. One client belongs to one
// application execution thread; no atomics or locks are needed.
#pragma once

#include <cstdint>
#include <format>
#include <string>
#include <string_view>

#include <nlohmann/json.hpp>

namespace nexigen::exec {

/// @brief Deterministic cycle configuration.
struct DeterministicConfig {
    std::uint64_t cycle_period_ns{1'000'000}; ///< Fixed cycle period; must be non-zero.
    std::uint64_t seed{1};                    ///< PRNG seed; 0 is accepted.

    /// @brief Cold-path diagnostic rendering.
    [[nodiscard]] std::string to_string() const;
};

/// @brief One deterministic application execution clock.
class DeterministicClient {
public:
    /// @brief Create a client whose first cycle starts now.
    ///
    /// @param config Cycle period and seed; a zero period is replaced by 1 ms.
    explicit DeterministicClient(DeterministicConfig config) noexcept;

    /// @brief Advance one cycle and sleep until its scheduled time.
    ///
    /// A late wake-up still advances exactly one cycle.
    ///
    /// @return The new cycle index.
    std::uint64_t wait_for_next_cycle() noexcept;

    /// @brief Number of cycles consumed so far.
    [[nodiscard]] std::uint64_t cycle_index() const noexcept { return cycle_index_; }

    /// @brief Scheduled monotonic time of the current cycle.
    [[nodiscard]] std::uint64_t cycle_time_ns() const noexcept;

    /// @brief Draw the next splitmix64 value.
    [[nodiscard]] std::uint64_t random_u64() noexcept;

    /// @brief Cold-path diagnostic rendering.
    [[nodiscard]] std::string to_string() const;

private:
    DeterministicConfig config_{};
    std::uint64_t start_ns_{0};
    std::uint64_t cycle_index_{0};
    std::uint64_t rng_state_{0};
};

/// @brief Free helper (project mandate 9).
[[nodiscard]] inline std::string to_string(const DeterministicConfig& value) {
    return value.to_string();
}
/// @brief Free helper (project mandate 9).
[[nodiscard]] inline std::string to_string(const DeterministicClient& value) {
    return value.to_string();
}

void to_json(nlohmann::json& j, const DeterministicConfig& value);
void from_json(const nlohmann::json& j, DeterministicConfig& value) noexcept;

} // namespace nexigen::exec

template <>
struct std::formatter<nexigen::exec::DeterministicConfig> : std::formatter<std::string_view> {
    template <class Ctx>
    auto format(const nexigen::exec::DeterministicConfig& value, Ctx& ctx) const {
        return std::formatter<std::string_view>::format(value.to_string(), ctx);
    }
};

template <>
struct std::formatter<nexigen::exec::DeterministicClient> : std::formatter<std::string_view> {
    template <class Ctx>
    auto format(const nexigen::exec::DeterministicClient& value, Ctx& ctx) const {
        return std::formatter<std::string_view>::format(value.to_string(), ctx);
    }
};
