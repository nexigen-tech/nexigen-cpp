// nexigen::logging — anti-spam quotas and suppression policy (spec
// logging/logger-options "SpamProtectionConfig defines anti-spam quotas").
#pragma once

#include <cstdint>
#include <string>

#include <nlohmann/json.hpp>

#include <nexigen/core/log_level.hpp>

namespace nexigen::logging {

/// @brief Anti-spam quotas and suppression policy nested inside `LoggerOptions`.
///
/// Carries only the quotas the hot-path engine consumes: the repeat-suppression
/// switch and window, the per-shard per-second message quota, and the level
/// that always bypasses the quota. The former byte-budget / max-count fields
/// were removed because no engine path reads them.
///
/// The struct is copyable vocabulary: `set_spam_protection_config()` publishes
/// each consumed field with an atomic store, so a producer's hot path observes
/// whole fields (a valid old or new value) and never blocks.
///
/// @note Spec `logging/logger-options`, requirement
///       "SpamProtectionConfig defines anti-spam quotas".
/// @note Spec `logging/spam-protection`, requirement "Dynamic quota updates are race-free".
struct SpamProtectionConfig {
    /// @brief Enables Level 1 repeat suppression (collapsing identical consecutive messages).
    bool enable_repeat_suppression{true};
    /// @brief Repeat-suppression collapse window in milliseconds.
    std::uint32_t repeat_suppression_window_ms{1000};
    /// @brief Level 2 quota of messages per second per producer shard; `0` = no limit.
    std::uint32_t max_messages_per_sec_per_shard{100000};
    /// @brief Always-pass floor: records at or above this level skip the quota check
    ///        (`Fatal` is the highest severity before `Off`).
    core::LogLevel bypass_min_level{core::LogLevel::Fatal};

    /// @brief Cold-path diagnostic rendering of every anti-spam field.
    ///
    /// @return Non-empty text naming each field and its value.
    /// @note Project mandate 9 (`to_string` on every public value type); the body
    ///       lives in `spam_protection_config.cpp` to keep the header light.
    [[nodiscard]] std::string to_string() const;
};
/// @brief Free `to_string` entry point for the anti-spam policy (mandate 9).
///
/// @param cfg Policy to render.
/// @return Exactly `cfg.to_string()`.
/// @note Project mandate 9; ADL-friendly mirror of the member function.
[[nodiscard]] inline std::string to_string(const SpamProtectionConfig& cfg) {
    return cfg.to_string();
}

// --- ADL JSON (cold path; bodies in src/logging/spam_protection_config.cpp).
/// @brief ADL JSON writer: serializes every anti-spam field by name.
///
/// @param j Destination JSON object; overwritten with the config's field map.
/// @param cfg Policy to serialize.
/// @note Project mandate 10 (JSON for every configuration value type).
/// @note Spec `logging/logger-options`, requirement
///       "SpamProtectionConfig defines anti-spam quotas".
void to_json(nlohmann::json& j, const SpamProtectionConfig& cfg);
/// @brief ADL JSON reader that is hostile-input safe.
///
/// Absent or ill-typed fields leave the corresponding member untouched, so the
/// call never throws and never partially overwrites `out`.
///
/// @param j Source JSON object.
/// @param out Destination config, updated field by field.
/// @note Project mandate 10; non-throwing under `JSON_NOEXCEPTION=1`.
void from_json(const nlohmann::json& j, SpamProtectionConfig& out) noexcept;

} // namespace nexigen::logging
