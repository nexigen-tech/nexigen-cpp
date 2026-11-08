// nexigen::chrono — provider configuration (change add-chrono-clock-service;
// spec chrono/chrono-provider).
//
// Cold-path diagnostics only: to_string allocates; to_json/from_json bodies
// live in src/chrono/chrono_provider_config.cpp so consumer TUs stay lean.
#pragma once

#include <chrono>
#include <format>
#include <string>

#include <nlohmann/json.hpp>

#include <nexigen/core/thread_management.hpp>

namespace nexigen::chrono {

/// Provider configuration: snapshot period and CPU affinity. A zero
/// collect_interval disables the background thread.
struct ChronoProviderConfig {
    std::chrono::nanoseconds collect_interval{0};
    core::ThreadSpec worker_thread{};

    /// Cold-path diagnostic (mandate 9).
    [[nodiscard]] std::string to_string() const;
};

/// Free helper (mandate 9): mirrors the member.
[[nodiscard]] std::string to_string(const ChronoProviderConfig& config);

/// Provider config JSON (cold path). Definitions live in
/// src/chrono/chrono_provider_config.cpp.
void to_json(nlohmann::json& j, const ChronoProviderConfig& config);
void from_json(const nlohmann::json& j, ChronoProviderConfig& out) noexcept;

} // namespace nexigen::chrono

/// The configuration formats as its diagnostic string (mandate 11).
template <>
struct std::formatter<nexigen::chrono::ChronoProviderConfig> : std::formatter<std::string_view> {
    template <class Ctx>
    auto format(const nexigen::chrono::ChronoProviderConfig& config, Ctx& ctx) const {
        const std::string text = config.to_string();
        return std::formatter<std::string_view>::format(text, ctx);
    }
};
