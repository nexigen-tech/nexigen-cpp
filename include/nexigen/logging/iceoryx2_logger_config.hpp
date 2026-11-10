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
    std::string app_id{"NEXG"};                     ///< 4-byte code into LogFrame.app_id
    std::uint32_t max_subscribers{16};              ///< receiver pool (REQ_COM_009)
    std::uint32_t history_size{4};                  ///< receiver queue depth
    core::LogLevel initial_level{core::LogLevel::Info};
    ThrottleConfig throttle{};          ///< rate limit (drop semantics)
    core::ThreadSpec worker_thread{};   ///< empty = inherited worker attributes
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

/// Cold-path JSON round-trip (mandate 10), hostile-safe under JSON_NOEXCEPTION=1.
inline void to_json(nlohmann::json& j, const Iceoryx2LoggerConfig& cfg) {
    j = nlohmann::json{
        {"service_name", cfg.service_name},
        {"app_id", cfg.app_id},
        {"max_subscribers", cfg.max_subscribers},
        {"history_size", cfg.history_size},
        {"initial_level", cfg.initial_level},
        {"throttle",
         nlohmann::json{{"enabled", cfg.throttle.enabled},
                        {"max_records_per_window", cfg.throttle.max_records_per_window},
                        {"window_ns", cfg.throttle.window_ns}}},
        {"worker_thread", cfg.worker_thread},
        {"worker_poll_interval_us", cfg.worker_poll_interval_us},
    };
}

inline void from_json(const nlohmann::json& j, Iceoryx2LoggerConfig& out) noexcept {
    if (!j.is_object()) {
        return;
    }
    Iceoryx2LoggerConfig parsed{};
    const auto read_str = [&j](const char* key, std::string& dst) {
        const auto it = j.find(key);
        if (it != j.end() && it->is_string()) {
            dst = it->get<std::string>();
        }
    };
    const auto read_u32 = [&j](const char* key, std::uint32_t& dst) {
        const auto it = j.find(key);
        if (it != j.end() && it->is_number_unsigned()) {
            dst = it->get<std::uint32_t>();
        }
    };
    read_str("service_name", parsed.service_name);
    read_str("app_id", parsed.app_id);
    read_u32("max_subscribers", parsed.max_subscribers);
    read_u32("history_size", parsed.history_size);
    read_u32("worker_poll_interval_us", parsed.worker_poll_interval_us);

    const auto lv = j.find("initial_level");
    if (lv != j.end() && lv->is_string()) {
        core::LogLevel level = core::LogLevel::Info;
        if (core::from_string(lv->get<std::string>(), level)) {
            parsed.initial_level = level;
        }
    }
    const auto th = j.find("throttle");
    if (th != j.end() && th->is_object()) {
        const auto en = th->find("enabled");
        if (en != th->end() && en->is_boolean()) {
            parsed.throttle.enabled = en->get<bool>();
        }
        const auto mr = th->find("max_records_per_window");
        if (mr != th->end() && mr->is_number_unsigned()) {
            parsed.throttle.max_records_per_window = mr->get<std::uint64_t>();
        }
        const auto wn = th->find("window_ns");
        if (wn != th->end() && wn->is_number_unsigned()) {
            parsed.throttle.window_ns = wn->get<std::uint64_t>();
        }
    }
    const auto wt = j.find("worker_thread");
    if (wt != j.end() && wt->is_object()) {
        from_json(*wt, parsed.worker_thread);
    }
    out = std::move(parsed);
}

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
