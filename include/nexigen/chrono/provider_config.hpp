// nexigen::chrono — provider configuration (change add-chrono-clock-service;
// spec chrono/chrono-provider; extended with `kind` by change
// add-clock-authority-core).
//
// Cold-path diagnostics only: to_string allocates; to_json/from_json bodies
// live in src/chrono/provider_config.cpp so consumer TUs stay lean.
#pragma once

#include <chrono>
#include <format>
#include <string>

#include <nlohmann/json.hpp>

#include <nexigen/chrono/provider_kind.hpp>
#include <nexigen/core/thread_management.hpp>

namespace nexigen::chrono {

/// Provider configuration: snapshot period, worker thread spec and mode. A
/// zero collect_interval disables the background thread; `kind` selects the
/// daemon/manual drive (consumed by app::Infrastructure in a later phase).
struct ProviderConfig {
    std::chrono::nanoseconds collect_interval{0};
    core::ThreadSpec worker_thread{};
    ProviderKind kind{ProviderKind::daemon};

    /// Cold-path diagnostic (mandate 9).
    [[nodiscard]] std::string to_string() const;
};

/// Free helper (mandate 9): mirrors the member.
[[nodiscard]] std::string to_string(const ProviderConfig& config);

/// Provider config JSON (cold path). Definitions live in
/// src/chrono/provider_config.cpp.
void to_json(nlohmann::json& j, const ProviderConfig& config);
void from_json(const nlohmann::json& j, ProviderConfig& out) noexcept;

} // namespace nexigen::chrono

/// The configuration formats as its diagnostic string (mandate 11).
template <>
struct std::formatter<nexigen::chrono::ProviderConfig> : std::formatter<std::string_view> {
    template <class Ctx>
    auto format(const nexigen::chrono::ProviderConfig& config, Ctx& ctx) const {
        const std::string text = config.to_string();
        return std::formatter<std::string_view>::format(text, ctx);
    }
};
