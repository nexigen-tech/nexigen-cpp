// nexigen::synchro — consumer configuration
// (change add-synchro-event-sync; spec synchro/consumer).
#pragma once

#include <cstdint>
#include <format>
#include <string>
#include <string_view>

#include <nlohmann/json.hpp>

#include <nexigen/core/result.hpp>
#include <nexigen/synchro/late_event_policy.hpp>
#include <nexigen/synchro/sync_channel_config.hpp>

namespace nexigen::synchro {

/// @brief Everything a `SyncConsumer` needs to validate and deliver a grid.
///
/// Zero latency and zero timeouts mean "derive from the payload": the maximum
/// latency becomes one cycle period, the loss window three cycle periods, and
/// the initial window the same three periods. They are resolved on the first
/// event, so a consumer never needs the producer's period in advance.
///
/// @note Spec `synchro/consumer`, requirement "Late-event classification".
struct SyncConsumerConfig {
    /// @brief Multiplier applied to the cycle period for derived watchdog windows.
    static constexpr std::uint64_t derived_timeout_cycles = 3U;

    SyncChannelConfig channel{};                               ///< Service and expected phase grid.
    std::uint64_t max_allowed_latency_ns{0};                   ///< 0 = one cycle period.
    LateEventPolicy late_policy{LateEventPolicy::DropAndWarn}; ///< Late-event delivery policy.
    std::uint64_t sync_loss_timeout_ns{0};                     ///< 0 = three cycle periods.
    std::uint64_t initial_sync_timeout_ns{0};                  ///< 0 = three cycle periods.

    /// @brief Validate the channel and the configured windows.
    /// @return Empty status, or `InvalidArgument` for an invalid configuration.
    /// @note Spec `synchro/consumer`, requirement "Late-event classification".
    [[nodiscard]] core::Status validate() const noexcept;

    /// @brief Resolve the maximum accepted latency for a known period.
    /// @param cycle_period_ns Producer period, used when the field is zero.
    /// @return Configured latency, or one cycle period for the default.
    /// @note Spec `synchro/consumer`, requirement "Late-event classification".
    [[nodiscard]] constexpr std::uint64_t
    resolve_max_latency(std::uint64_t cycle_period_ns) const noexcept {
        return max_allowed_latency_ns == 0U ? cycle_period_ns : max_allowed_latency_ns;
    }

    /// @brief Resolve the synchronization-loss window for a known period.
    /// @param cycle_period_ns Producer period, used when the field is zero.
    /// @return Configured window, or three cycle periods for the default.
    /// @note Spec `synchro/consumer`, requirement "Synchronization watchdog".
    [[nodiscard]] constexpr std::uint64_t
    resolve_loss_timeout(std::uint64_t cycle_period_ns) const noexcept {
        return sync_loss_timeout_ns == 0U ? derived_timeout_cycles * cycle_period_ns
                                          : sync_loss_timeout_ns;
    }

    /// @brief Resolve the initial-synchronization window for a known period.
    /// @param cycle_period_ns Producer period, used when the field is zero.
    /// @return Configured window, or three cycle periods for the default.
    /// @note Spec `synchro/consumer`, requirement "Synchronization watchdog".
    [[nodiscard]] constexpr std::uint64_t
    resolve_initial_timeout(std::uint64_t cycle_period_ns) const noexcept {
        return initial_sync_timeout_ns == 0U ? derived_timeout_cycles * cycle_period_ns
                                             : initial_sync_timeout_ns;
    }

    /// @brief Render the cold-path diagnostic string.
    /// @return Human-readable configuration summary.
    /// @note Mandate 9; cold path only.
    [[nodiscard]] std::string to_string() const;
};

/// @brief Serialize a consumer configuration to diagnostic JSON.
/// @param j Destination JSON object.
/// @param config Configuration to serialize.
/// @note Mandate 10; cold path only.
void to_json(nlohmann::json& j, const SyncConsumerConfig& config);

/// @brief Parse a consumer configuration from JSON, strictly non-throwing.
/// @param j Source JSON object.
/// @param config Destination, fields updated independently.
/// @note Mandate 10; cold path only.
void from_json(const nlohmann::json& j, SyncConsumerConfig& config) noexcept;

} // namespace nexigen::synchro
