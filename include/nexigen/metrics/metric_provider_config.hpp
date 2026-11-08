// nexigen::metrics — provider configuration (design D14; spec
// metrics/metric-provider).
#pragma once

#include <chrono>
#include <cstdint>
#include <format>
#include <string>
#include <string_view>

#include <nlohmann/json.hpp>

#include <nexigen/core/thread_management.hpp>

namespace nexigen::metrics {

/// Provider configuration: collection period, CPU affinity and registry
/// capacities. A zero collect_interval disables the background thread.
struct MetricProviderConfig {
    std::chrono::nanoseconds collect_interval{0};
    core::ThreadSpec worker_thread{};
    std::uint32_t max_meters{16};
    std::uint32_t max_counters{64};
    std::uint32_t max_gauges{64};
    std::uint32_t max_states{16};
    std::uint32_t max_statistics{32};
    std::uint32_t max_explicit_histograms{16};
    std::uint32_t max_exponential_histograms{8};

    /// Cold-path diagnostic (mandate 9).
    [[nodiscard]] std::string to_string() const;
};

/// Free helper (mandate 9): mirrors the member.
[[nodiscard]] std::string to_string(const MetricProviderConfig& config);

/// Provider config JSON (cold path). Definitions live in
/// src/metrics/snapshot_format.cpp (design D2).
void to_json(nlohmann::json& j, const MetricProviderConfig& config);
void from_json(const nlohmann::json& j, MetricProviderConfig& out) noexcept;

} // namespace nexigen::metrics

/// MetricProviderConfig formats as its diagnostic string (mandate 11).
template <>
struct std::formatter<nexigen::metrics::MetricProviderConfig> : std::formatter<std::string_view> {
    template <class Ctx>
    auto format(const nexigen::metrics::MetricProviderConfig& value, Ctx& ctx) const {
        const std::string text = value.to_string();
        return std::formatter<std::string_view>::format(text, ctx);
    }
};
