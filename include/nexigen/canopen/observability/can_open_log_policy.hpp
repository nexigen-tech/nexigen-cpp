// nexigen::canopen::observability — hot/cold logging policy (change
// add-canopen-observability; spec canopen/observability).
#pragma once

#include <cstdint>
#include <string>
#include <string_view>

#include <nlohmann/json.hpp>

#include <nexigen/core/log_level.hpp>
#include <nexigen/core/result.hpp>
#include <nexigen/core/serialization_mandate.hpp>

namespace nexigen::canopen::observability {

/// @brief Thresholds, levels, and caps for CANopen runtime observability.
///
/// The policy is a plain value aggregate copied once at runtime construction.
/// Hot-path recording reads it directly; it is never mutated from the runtime
/// thread after construction.
///
/// Defaults (design D6):
/// `repeat_interval_ms=5000`, `recovery_hold_ms=5000`,
/// `max_suppressed_per_key=10000`, `summary_interval_ms=60000`,
/// `max_hot_logs_per_second=50`, `hot_error_level=Error`,
/// `recovery_level=Info`, `summary_level=Info`, `max_cold_diagnostics=32`.
///
/// @note Spec `canopen/observability`, requirement "Suppression defaults and
///       global budget".
struct CanOpenLogPolicy {
    /// @brief Minimum accepted `repeat_interval_ms` (one millisecond).
    static constexpr std::uint32_t min_repeat_interval_ms = 1U;
    /// @brief Maximum accepted `repeat_interval_ms` (one hour).
    static constexpr std::uint32_t max_repeat_interval_ms = 3'600'000U;

    /// @brief Minimum accepted `recovery_hold_ms` (one millisecond).
    static constexpr std::uint32_t min_recovery_hold_ms = 1U;
    /// @brief Maximum accepted `recovery_hold_ms` (one hour).
    static constexpr std::uint32_t max_recovery_hold_ms = 3'600'000U;

    /// @brief Minimum accepted `max_suppressed_per_key`.
    static constexpr std::uint32_t min_suppressed_per_key = 1U;
    /// @brief Maximum accepted `max_suppressed_per_key`.
    static constexpr std::uint32_t max_suppressed_per_key_limit = 1'000'000U;

    /// @brief Minimum accepted `summary_interval_ms` (one second).
    static constexpr std::uint32_t min_summary_interval_ms = 1'000U;
    /// @brief Maximum accepted `summary_interval_ms` (one hour).
    static constexpr std::uint32_t max_summary_interval_ms = 3'600'000U;

    /// @brief Minimum accepted `max_hot_logs_per_second`.
    static constexpr std::uint32_t min_hot_logs_per_second = 1U;
    /// @brief Maximum accepted `max_hot_logs_per_second`.
    static constexpr std::uint32_t max_hot_logs_per_second_limit = 10'000U;

    /// @brief Maximum accepted `max_cold_diagnostics`.
    static constexpr std::uint32_t max_cold_diagnostics_limit = 1'024U;

    /// @brief Minimum time between two repeat records for one error key.
    std::uint32_t repeat_interval_ms{5'000U};
    /// @brief Quiet time before an active error episode is declared recovered.
    std::uint32_t recovery_hold_ms{5'000U};
    /// @brief Suppressed occurrences that force one immediate repeat record.
    std::uint32_t max_suppressed_per_key{10'000U};
    /// @brief Window between per-domain cold-path summaries.
    std::uint32_t summary_interval_ms{60'000U};
    /// @brief Repeat records allowed per second across all keys.
    std::uint32_t max_hot_logs_per_second{50U};
    /// @brief Severity of first-error, code-change, and repeat records.
    core::LogLevel hot_error_level{core::LogLevel::Error};
    /// @brief Severity of an episode-recovery record.
    core::LogLevel recovery_level{core::LogLevel::Info};
    /// @brief Severity of a windowed domain summary.
    core::LogLevel summary_level{core::LogLevel::Info};
    /// @brief EDS diagnostics emitted per load before the omitted-count trailer.
    std::uint32_t max_cold_diagnostics{32U};

    /// @brief Check every field against its documented range.
    ///
    /// Levels are accepted in the whole `Verbose`..`Off` range: `Off` is a
    /// deliberate way to disable one record class while its metric keeps
    /// counting.
    ///
    /// @return Empty status when valid; `InvalidArgument` when any field is out
    ///         of range.
    /// @note Cold path only.
    [[nodiscard]] core::Status validate() const noexcept;

    /// @brief Cold-path diagnostic string with every field and its value.
    ///
    /// @return One-line text; allocates.
    /// @note Mandate 9; never call on a hot path.
    [[nodiscard]] std::string to_string() const;
};

/// @brief Write a log policy as a JSON object (cold path).
///
/// Levels are written as their canonical lowercase names.
///
/// @param j Destination JSON value.
/// @param policy Policy to serialize.
/// @note Mandate 10; cold path only.
void to_json(nlohmann::json& j, const CanOpenLogPolicy& policy);

/// @brief Parse a log policy without throwing.
///
/// Each field is read independently. A missing, mistyped, or out-of-range field
/// leaves the corresponding member unchanged, so a partial document updates only
/// the fields it carries.
///
/// @param j Source JSON value; a non-object is ignored.
/// @param policy Destination, updated field by field.
/// @note Mandate 10; cold path only.
void from_json(const nlohmann::json& j, CanOpenLogPolicy& policy) noexcept;

} // namespace nexigen::canopen::observability
