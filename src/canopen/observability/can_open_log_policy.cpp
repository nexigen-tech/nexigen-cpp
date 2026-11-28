// nexigen::canopen::observability — log policy serialization and validation
// (change add-canopen-observability; spec canopen/observability).
#include <nexigen/canopen/observability/can_open_log_policy.hpp>

#include <cstdint>
#include <format>
#include <string>

#include <nlohmann/json.hpp>

#include <nexigen/core/detail/json_field.hpp>

namespace nexigen::canopen::observability {

namespace {

/// @brief Read one bounded unsigned field, leaving it unchanged on rejection.
///
/// Accepts both JSON signed and unsigned integers within the documented range;
/// a missing, mistyped, or out-of-range value is ignored.
///
/// @tparam T Unsigned destination type.
/// @param j JSON object to probe.
/// @param key Field name.
/// @param out Destination, updated only on success.
/// @param minimum Inclusive lower bound.
/// @param maximum Inclusive upper bound.
template <class T>
void read_bounded(const nlohmann::json& j, const char* key, T& out, std::int64_t minimum,
                  std::int64_t maximum) noexcept {
    std::int64_t candidate = static_cast<std::int64_t>(out);
    if (!core::detail::read_signed(j, key, candidate)) {
        return;
    }
    if (candidate < minimum || candidate > maximum) {
        return;
    }
    out = static_cast<T>(candidate);
}

/// @brief Read one log level from its canonical lowercase name.
///
/// @param j JSON object to probe.
/// @param key Field name.
/// @param out Destination, updated only on a known level name.
void read_level(const nlohmann::json& j, const char* key, core::LogLevel& out) noexcept {
    std::string text;
    if (!core::detail::read_string(j, key, text)) {
        return;
    }
    core::LogLevel parsed = out;
    if (!core::from_string(text, parsed)) {
        return;
    }
    out = parsed;
}

/// @brief Whether a level is one of the declared `LogLevel` enumerators.
///
/// @param level Value to check.
/// @return `true` for `Verbose` through `Off`.
[[nodiscard]] bool valid_level(core::LogLevel level) noexcept {
    return static_cast<std::uint8_t>(level) <= static_cast<std::uint8_t>(core::LogLevel::Off);
}

} // namespace

core::Status CanOpenLogPolicy::validate() const noexcept {
    if (repeat_interval_ms < min_repeat_interval_ms ||
        repeat_interval_ms > max_repeat_interval_ms) {
        return std::unexpected(core::ErrorCode::InvalidArgument);
    }
    if (recovery_hold_ms < min_recovery_hold_ms || recovery_hold_ms > max_recovery_hold_ms) {
        return std::unexpected(core::ErrorCode::InvalidArgument);
    }
    if (max_suppressed_per_key < min_suppressed_per_key ||
        max_suppressed_per_key > max_suppressed_per_key_limit) {
        return std::unexpected(core::ErrorCode::InvalidArgument);
    }
    if (summary_interval_ms < min_summary_interval_ms ||
        summary_interval_ms > max_summary_interval_ms) {
        return std::unexpected(core::ErrorCode::InvalidArgument);
    }
    if (max_hot_logs_per_second < min_hot_logs_per_second ||
        max_hot_logs_per_second > max_hot_logs_per_second_limit) {
        return std::unexpected(core::ErrorCode::InvalidArgument);
    }
    if (max_cold_diagnostics > max_cold_diagnostics_limit) {
        return std::unexpected(core::ErrorCode::InvalidArgument);
    }
    if (!valid_level(hot_error_level) || !valid_level(recovery_level) ||
        !valid_level(summary_level)) {
        return std::unexpected(core::ErrorCode::InvalidArgument);
    }
    return {};
}

std::string CanOpenLogPolicy::to_string() const {
    return std::format(
        "CanOpenLogPolicy(repeat_interval_ms={}, recovery_hold_ms={}, "
        "max_suppressed_per_key={}, summary_interval_ms={}, max_hot_logs_per_second={}, "
        "hot_error_level={}, recovery_level={}, summary_level={}, max_cold_diagnostics={})",
        repeat_interval_ms, recovery_hold_ms, max_suppressed_per_key, summary_interval_ms,
        max_hot_logs_per_second, core::to_string(hot_error_level), core::to_string(recovery_level),
        core::to_string(summary_level), max_cold_diagnostics);
}

void to_json(nlohmann::json& j, const CanOpenLogPolicy& policy) {
    j = nlohmann::json{
        {"repeat_interval_ms", policy.repeat_interval_ms},
        {"recovery_hold_ms", policy.recovery_hold_ms},
        {"max_suppressed_per_key", policy.max_suppressed_per_key},
        {"summary_interval_ms", policy.summary_interval_ms},
        {"max_hot_logs_per_second", policy.max_hot_logs_per_second},
        {"hot_error_level", std::string(core::to_string(policy.hot_error_level))},
        {"recovery_level", std::string(core::to_string(policy.recovery_level))},
        {"summary_level", std::string(core::to_string(policy.summary_level))},
        {"max_cold_diagnostics", policy.max_cold_diagnostics},
    };
}

void from_json(const nlohmann::json& j, CanOpenLogPolicy& policy) noexcept {
    if (!j.is_object()) {
        return;
    }
    read_bounded(j, "repeat_interval_ms", policy.repeat_interval_ms,
                 CanOpenLogPolicy::min_repeat_interval_ms,
                 CanOpenLogPolicy::max_repeat_interval_ms);
    read_bounded(j, "recovery_hold_ms", policy.recovery_hold_ms,
                 CanOpenLogPolicy::min_recovery_hold_ms, CanOpenLogPolicy::max_recovery_hold_ms);
    read_bounded(j, "max_suppressed_per_key", policy.max_suppressed_per_key,
                 CanOpenLogPolicy::min_suppressed_per_key,
                 CanOpenLogPolicy::max_suppressed_per_key_limit);
    read_bounded(j, "summary_interval_ms", policy.summary_interval_ms,
                 CanOpenLogPolicy::min_summary_interval_ms,
                 CanOpenLogPolicy::max_summary_interval_ms);
    read_bounded(j, "max_hot_logs_per_second", policy.max_hot_logs_per_second,
                 CanOpenLogPolicy::min_hot_logs_per_second,
                 CanOpenLogPolicy::max_hot_logs_per_second_limit);
    read_bounded(j, "max_cold_diagnostics", policy.max_cold_diagnostics, 0,
                 CanOpenLogPolicy::max_cold_diagnostics_limit);
    read_level(j, "hot_error_level", policy.hot_error_level);
    read_level(j, "recovery_level", policy.recovery_level);
    read_level(j, "summary_level", policy.summary_level);
}

} // namespace nexigen::canopen::observability
