// nexigen::synchro — generator configuration
// (change add-synchro-event-sync; spec synchro/generator).
#pragma once

#include <cstdint>
#include <format>
#include <string>
#include <string_view>

#include <nlohmann/json.hpp>

#include <nexigen/core/result.hpp>
#include <nexigen/core/thread_management.hpp>
#include <nexigen/synchro/start_policy.hpp>
#include <nexigen/synchro/sync_channel_config.hpp>

namespace nexigen::synchro {

/// @brief Everything a `SyncGenerator` needs to publish on a grid.
///
/// @note Spec `synchro/generator`, requirement "Generator lifecycle and
///       ownership".
struct SyncConfig {
    /// @brief Default final-spin window before a phase deadline (30 us).
    static constexpr std::uint64_t default_spin_threshold_ns = 30'000U;
    /// @brief Canonical event code for synchronization events (`'SYNC'`).
    static constexpr std::uint32_t default_event_code = 0x53594E43U;

    SyncChannelConfig channel{}; ///< Service, transport limits, and phase grid.
    StartPolicy start{};         ///< How the first cycle is resolved.
    std::uint64_t spin_threshold_ns{default_spin_threshold_ns}; ///< Final busy-wait window.
    std::uint32_t event_code{default_event_code};               ///< Header event code.
    core::ThreadSpec thread{}; ///< Attributes applied to the producer worker.

    /// @brief Validate the channel, start policy, spin window, and thread spec.
    ///
    /// The spin window must stay strictly below the phase period so the coarse
    /// wait always leaves room for a sleep.
    ///
    /// @return Empty status, or `InvalidArgument` for an invalid configuration.
    /// @note Spec `synchro/generator`, requirement "Generator operational
    ///       limits".
    [[nodiscard]] core::Status validate() const noexcept;

    /// @brief Render the cold-path diagnostic string.
    /// @return Human-readable configuration summary.
    /// @note Mandate 9; cold path only.
    [[nodiscard]] std::string to_string() const;
};

/// @brief Serialize a generator configuration to diagnostic JSON.
/// @param j Destination JSON object.
/// @param config Configuration to serialize.
/// @note Mandate 10; cold path only.
void to_json(nlohmann::json& j, const SyncConfig& config);

/// @brief Parse a generator configuration from JSON, strictly non-throwing.
/// @param j Source JSON object.
/// @param config Destination, fields updated independently.
/// @note Mandate 10; cold path only.
void from_json(const nlohmann::json& j, SyncConfig& config) noexcept;

} // namespace nexigen::synchro
