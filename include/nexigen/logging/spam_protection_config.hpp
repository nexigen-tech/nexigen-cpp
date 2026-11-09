// nexigen::logging — anti-spam quotas and suppression policy (spec
// logging/logger-options "SpamProtectionConfig defines anti-spam quotas").
#pragma once

#include <cstdint>
#include <string>

#include <nlohmann/json.hpp>

#include <nexigen/core/log_level.hpp>

namespace nexigen::logging {

/// Anti-spam quotas and suppression policy (nested inside LoggerOptions).
/// Carries only the quotas the hot-path engine consumes: the former
/// byte-budget / max-count fields were removed (spec logger-options
/// "SpamProtectionConfig defines anti-spam quotas").
struct SpamProtectionConfig {
    bool enable_repeat_suppression{true};                 ///< Level 1 on.
    std::uint32_t repeat_suppression_window_ms{1000};     ///< Collapse window.
    std::uint32_t max_messages_per_sec_per_shard{100000}; ///< 0 = no limit.
    core::LogLevel bypass_min_level{
        core::LogLevel::Fatal}; ///< Always-pass floor (Fatal = highest before Off).

    [[nodiscard]] std::string to_string() const;
};
[[nodiscard]] inline std::string to_string(const SpamProtectionConfig& cfg) {
    return cfg.to_string();
}

// --- ADL JSON (cold path; bodies in src/logging/spam_protection_config.cpp).
void to_json(nlohmann::json& j, const SpamProtectionConfig& cfg);
void from_json(const nlohmann::json& j, SpamProtectionConfig& out) noexcept;

} // namespace nexigen::logging
