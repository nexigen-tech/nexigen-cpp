// nexigen::tracing — TracerProviderConfig (change
// refactor-hub-model-config-headers; extracted verbatim from
// tracer_provider.hpp).
#pragma once

#include <cstdint>
#include <format>
#include <string>
#include <string_view>

#include <nlohmann/json.hpp>

#include <nexigen/core/thread_management.hpp>
#include <nexigen/core/worker_tuning.hpp>

namespace nexigen::tracing {

/// @brief Configuration of `TracerProvider`.
///
/// A plain value type consumed once when a provider is constructed: the
/// background consumer's CPU-affinity/thread attributes, the effective tracer
/// pool limit, the optional span-rate budget (two zeros = unlimited), and the
/// idle-poll ceiling. Full serialization trio: member and free `to_string`,
/// non-throwing JSON round-trip, and `std::formatter`.
///
/// @note Spec `tracing/tracer-provider`, requirements "CPU-pinned background
///       consumer", "Dynamic runtime control", and "Provider serialization
///       surfaces".
struct TracerProviderConfig {
    core::ThreadSpec worker_thread{};      ///< empty = inherited worker attributes.
    std::uint32_t max_tracers{16};         ///< effective pool limit (≤ 64).
    std::uint64_t max_spans_per_window{0}; ///< 0 = no rate limiting.
    std::uint64_t window_ns{0};            ///< 0 = no rate limiting.
    /// Ceiling of the consumer's idle sleep (backoff grows up to it); clamped
    /// to [50, 100000] us. Larger = fewer wake-ups on a shared pinned core,
    /// but the shard rings must absorb bursts for that long.
    std::uint32_t worker_poll_interval_us{core::WorkerTuning::default_poll_interval_us};

    /// @brief Render the configuration as a cold-path diagnostic string.
    ///
    /// @return Text naming the worker specification, tracer limit, rate budget,
    ///         and poll interval.
    /// @note Spec `tracing/tracer-provider`, requirement "Provider serialization
    ///       surfaces"; project mandate 9 (`to_string`).
    [[nodiscard]] std::string to_string() const;
};

/// @brief Free diagnostic helper mirroring
///        `TracerProviderConfig::to_string()`.
///
/// @param config Configuration to render.
/// @return The same text the member returns.
/// @note Project mandate 9 (`to_string`); cold path only.
[[nodiscard]] std::string to_string(const TracerProviderConfig& config);

/// @brief Serialize the configuration as JSON (cold path).
///
/// @param j Destination JSON value, overwritten with every configuration field.
/// @param config Configuration to serialize.
/// @note Spec `tracing/tracer-provider`, requirement "Provider serialization
///       surfaces"; project mandate 10 (JSON).
void to_json(nlohmann::json& j, const TracerProviderConfig& config);

/// @brief Parse the configuration from JSON without throwing.
///
/// Uses the safe accessor pattern: missing or wrongly typed fields keep their
/// prior value, and a malformed document leaves the parsed values unchanged
/// under `JSON_NOEXCEPTION=1`.
///
/// @param j JSON object to read.
/// @param out Destination configuration, updated field by field.
/// @note Spec `tracing/record-format`, requirement "TracerProviderConfig
///       deserialization uses safe nlohmann accessors"; project mandate 10.
void from_json(const nlohmann::json& j, TracerProviderConfig& out) noexcept;

} // namespace nexigen::tracing

/// @brief `std::formatter` for `TracerProviderConfig`.
///
/// @note Project mandate 11 (`std::formatter`); formatting is cold path only.
template <>
struct std::formatter<nexigen::tracing::TracerProviderConfig> : std::formatter<std::string_view> {
    /// @brief Format the configuration through its `to_string()` text.
    ///
    /// @tparam Ctx Format context type supplied by `std::format`.
    /// @param config Configuration to render.
    /// @param ctx Output context receiving the text.
    /// @return Iterator past the last written character.
    template <class Ctx>
    auto format(const nexigen::tracing::TracerProviderConfig& config, Ctx& ctx) const {
        const std::string text = config.to_string();
        return std::formatter<std::string_view>::format(text, ctx);
    }
};
