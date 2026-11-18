// nexigen::synchro — start policy
// (change add-synchro-event-sync; spec synchro/protocol).
#pragma once

#include <cstdint>
#include <format>
#include <string>
#include <string_view>

#include <nlohmann/json.hpp>

#include <nexigen/core/result.hpp>
#include <nexigen/synchro/past_target_policy.hpp>
#include <nexigen/synchro/start_mode.hpp>

namespace nexigen::synchro {

/// @brief How a generator resolves the instant of its first cycle.
///
/// The policy separates the mode from target alignment and from past-target
/// behaviour so `start_at()` can reuse the same value with a one-shot target.
///
/// @note Spec `synchro/protocol`, requirement "Start policy".
struct StartPolicy {
    StartMode mode{StartMode::NextCycle}; ///< Next boundary or explicit target.
    std::uint64_t target_time_ns{0};      ///< Raw-monotonic target for `AtTarget`.
    bool align_target_to_cycle{true};     ///< Snap a future target to the cycle grid.
    PastTargetPolicy past_target{PastTargetPolicy::Reject}; ///< Past-target behaviour.

    /// @brief Validate the mode and past-target vocabulary.
    /// @return Empty status, or `InvalidArgument` for an out-of-vocabulary value.
    /// @note Spec `synchro/protocol`, requirement "Start policy".
    [[nodiscard]] core::Status validate() const noexcept;

    /// @brief Render the cold-path diagnostic string.
    /// @return Human-readable policy summary.
    /// @note Mandate 9; cold path only.
    [[nodiscard]] std::string to_string() const;
};

/// @brief Serialize a start policy to diagnostic JSON.
/// @param j Destination JSON object.
/// @param policy Policy to serialize.
/// @note Mandate 10; cold path only.
void to_json(nlohmann::json& j, const StartPolicy& policy);

/// @brief Parse a start policy from JSON, strictly non-throwing.
/// @param j Source JSON object.
/// @param policy Destination, fields updated independently.
/// @note Mandate 10; cold path only.
void from_json(const nlohmann::json& j, StartPolicy& policy) noexcept;

} // namespace nexigen::synchro
