// nexigen::synchro — channel configuration shared by generator and consumer
// (change add-synchro-event-sync; spec synchro/protocol).
//
// One value binds the com service, the transport limits, and the logical grid.
// The generator and the consumer must agree on it; each side validates the
// producer's payload against its own copy on the first event.
#pragma once

#include <cstdint>
#include <format>
#include <string>
#include <string_view>

#include <nlohmann/json.hpp>

#include <nexigen/com/channel_config.hpp>
#include <nexigen/core/result.hpp>

namespace nexigen::synchro {

/// @brief Service, transport limits, and phase grid of one synchronization channel.
///
/// @note Spec `synchro/protocol`, requirement "Synchronization payload and
///       channel configuration".
struct SyncChannelConfig {
    /// @brief Smallest supported phase period: 1 ms (at most 1000 phases/s).
    static constexpr std::uint64_t min_phase_period_ns = 1'000'000U;

    std::string service_name{};                 ///< com service shared by both endpoints.
    com::ChannelConfig channel{};               ///< Transport limits (consumer/queue/history).
    std::uint64_t cycle_period_ns{100'000'000}; ///< Cycle period Tc, 100 ms by default.
    std::uint32_t phases_per_cycle{10};         ///< Number of phases N inside one cycle.

    /// @brief Phase period `Tc / N`, truncated to whole nanoseconds.
    /// @return Phase duration, or zero for a zero phase count.
    /// @note Spec `synchro/protocol`, requirement "Deterministic phase
    ///       schedule".
    [[nodiscard]] constexpr std::uint64_t phase_period_ns() const noexcept {
        return phases_per_cycle == 0U ? 0U : cycle_period_ns / phases_per_cycle;
    }

    /// @brief Validate the service name, transport limits, and phase grid.
    ///
    /// Rejects an empty service name, a channel failure, a zero cycle period or
    /// phase count, and a phase period below 1 ms.
    ///
    /// @return Empty status, or `InvalidArgument` for an invalid configuration.
    /// @note Spec `synchro/protocol`, requirement "Synchronization payload and
    ///       channel configuration".
    [[nodiscard]] core::Status validate() const noexcept;

    /// @brief Render the cold-path diagnostic string.
    /// @return Human-readable configuration summary.
    /// @note Mandate 9; cold path only.
    [[nodiscard]] std::string to_string() const;
};

/// @brief Serialize a channel configuration to diagnostic JSON.
/// @param j Destination JSON object.
/// @param config Configuration to serialize.
/// @note Mandate 10; cold path only.
void to_json(nlohmann::json& j, const SyncChannelConfig& config);

/// @brief Parse a channel configuration from JSON, strictly non-throwing.
/// @param j Source JSON object.
/// @param config Destination, fields updated independently.
/// @note Mandate 10; cold path only.
void from_json(const nlohmann::json& j, SyncChannelConfig& config) noexcept;

} // namespace nexigen::synchro
