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

/// Provider configuration (value type, full serialization trio; bodies in
/// src/tracing/record_format.cpp): CPU affinity of the background consumer,
/// the effective tracer pool size, and the optional span-rate budget (two
/// zeros = unlimited).
struct TracerProviderConfig {
    core::ThreadSpec worker_thread{};      ///< empty = inherited worker attributes.
    std::uint32_t max_tracers{16};         ///< effective pool limit (≤ 64).
    std::uint64_t max_spans_per_window{0}; ///< 0 = no rate limiting.
    std::uint64_t window_ns{0};            ///< 0 = no rate limiting.
    /// Ceiling of the consumer's idle sleep (backoff grows up to it); clamped
    /// to [50, 100000] us. Larger = fewer wake-ups on a shared pinned core,
    /// but the shard rings must absorb bursts for that long.
    std::uint32_t worker_poll_interval_us{core::WorkerTuning::default_poll_interval_us};

    /// Cold-path diagnostic (mandate 9).
    [[nodiscard]] std::string to_string() const;
};

/// Free helper (mandate 9): mirrors the member.
[[nodiscard]] std::string to_string(const TracerProviderConfig& config);

/// ADL JSON (cold path, full round-trip under JSON_NOEXCEPTION=1).
void to_json(nlohmann::json& j, const TracerProviderConfig& config);
void from_json(const nlohmann::json& j, TracerProviderConfig& out) noexcept;

} // namespace nexigen::tracing

/// The configuration formats as its diagnostic string (mandate 11).
template <>
struct std::formatter<nexigen::tracing::TracerProviderConfig> : std::formatter<std::string_view> {
    template <class Ctx>
    auto format(const nexigen::tracing::TracerProviderConfig& config, Ctx& ctx) const {
        const std::string text = config.to_string();
        return std::formatter<std::string_view>::format(text, ctx);
    }
};
