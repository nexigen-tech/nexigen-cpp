// nexigen::logging — construction-time configuration for Iceoryx2Logger
// (change add-iceoryx2-logger; spec logging/iceoryx2-logger; design D6).
#pragma once

#include <cstdint>
#include <format>
#include <string>

#include <nlohmann/json.hpp>

#include <nexigen/core/log_level.hpp>
#include <nexigen/core/thread_management.hpp>
#include <nexigen/core/worker_tuning.hpp>
#include <nexigen/logging/throttle_config.hpp>

namespace nexigen::logging {

/// Construction-time configuration. All views are copied into fixed buffers
/// during initialize(); lengths beyond the buffers are rejected.
struct Iceoryx2LoggerConfig {
    std::string service_name{"Nexigen/Log/nexigen"}; ///< pub/sub service (≤ 511 B)
    std::string app_id{"NEXG"};                      ///< 4-byte code into LogFrame.app_id
    std::uint32_t max_subscribers{16};               ///< receiver pool (REQ_COM_009)
    std::uint32_t history_size{4};                   ///< receiver queue depth
    core::LogLevel initial_level{core::LogLevel::Info};
    ThrottleConfig throttle{};        ///< rate limit (drop semantics)
    core::ThreadSpec worker_thread{}; ///< empty = inherited worker attributes
    /// Ceiling of the worker's idle sleep (backoff grows up to it); clamped to
    /// [50, 100000] us — bounds the worker's idle CPU consumption.
    std::uint32_t worker_poll_interval_us{core::WorkerTuning::default_poll_interval_us};

    /// Cold-path diagnostic (mandate 9).
    [[nodiscard]] std::string to_string() const {
        std::string out = "Iceoryx2LoggerConfig(service=" + service_name;
        out += ", app=" + app_id;
        out += ", subscribers=" + std::to_string(max_subscribers);
        out += ", history=" + std::to_string(history_size);
        out += ", level=" + std::string(core::to_string(initial_level));
        out += ", throttle=" + throttle.to_string();
        out += ", worker_thread=" + worker_thread.to_string();
        out += ", worker_poll_interval_us=" + std::to_string(worker_poll_interval_us) + ")";
        return out;
    }
};

/// Free helper (mandate 9): mirrors the member.
[[nodiscard]] inline std::string to_string(const Iceoryx2LoggerConfig& cfg) {
    return cfg.to_string();
}

/// Cold-path JSON round-trip (mandate 10), hostile-safe under JSON_NOEXCEPTION=1;
/// bodies live in src/logging/iceoryx2_logger_config.cpp.
void to_json(nlohmann::json& j, const Iceoryx2LoggerConfig& cfg);
void from_json(const nlohmann::json& j, Iceoryx2LoggerConfig& out) noexcept;

} // namespace nexigen::logging

/// The config formats as its diagnostic string (mandate 11).
template <>
struct std::formatter<nexigen::logging::Iceoryx2LoggerConfig> : std::formatter<std::string_view> {
    template <class Ctx>
    auto format(const nexigen::logging::Iceoryx2LoggerConfig& cfg, Ctx& ctx) const {
        const std::string text = cfg.to_string();
        return std::formatter<std::string_view>::format(text, ctx);
    }
};
