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

/// @brief Construction-time configuration for the shared-memory `Iceoryx2Logger` sink.
///
/// Cold-path aggregate read once by `initialize()`: the service name and app code
/// are copied into fixed buffers, and the publisher, receiver pool, and worker
/// thread are created from these values. Producers never touch the struct; the
/// worker alone publishes `LogFrame`s after initialization.
///
/// @note Spec `logging/iceoryx2-logger`, requirement "Iceoryx2 logger configuration".
struct Iceoryx2LoggerConfig {
    /// @brief Pub/sub service name the daemon subscribes to (at most 511 bytes).
    std::string service_name{"Nexigen/Log/nexigen"};
    /// @brief Four-byte application code copied into `LogFrame::app_id`.
    std::string app_id{"NEXG"};
    /// @brief Maximum subscriber/receiver pool size (requirement `REQ_COM_009`).
    std::uint32_t max_subscribers{16};
    /// @brief Per-subscriber receiver queue depth in frames.
    std::uint32_t history_size{4};
    /// @brief Severity threshold applied at initialization.
    core::LogLevel initial_level{core::LogLevel::Info};
    /// @brief Window throttle policy; over-limit frames are dropped, never queued.
    ThrottleConfig throttle{};
    /// @brief Worker thread attributes; an empty CPU set inherits the creator's affinity.
    core::ThreadSpec worker_thread{};
    /// @brief Ceiling of the worker's idle sleep (backoff grows up to it); clamped to
    ///        `[50, 100000]` us.
    ///
    /// Bounds the worker's idle CPU consumption.
    std::uint32_t worker_poll_interval_us{core::WorkerTuning::default_poll_interval_us};

    /// @brief Cold-path diagnostic rendering of every configuration field.
    ///
    /// @return Text containing the service, app code, pool sizes, level, throttle, worker
    ///         spec, and poll ceiling.
    /// @note Project mandate 9 (`to_string` on every public value type).
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

/// @brief Free `to_string` entry point for the iceoryx2 config (mandate 9).
///
/// @param cfg Config to render.
/// @return Exactly `cfg.to_string()`.
/// @note Project mandate 9; ADL-friendly mirror of the member function.
[[nodiscard]] inline std::string to_string(const Iceoryx2LoggerConfig& cfg) {
    return cfg.to_string();
}

/// @brief Cold-path JSON writer for the iceoryx2 logger configuration.
///
/// @param j Destination JSON object; overwritten with the config's field map.
/// @param cfg Config to serialize.
/// @note Project mandate 10; the body lives in `iceoryx2_logger_config.cpp`.
/// @note Spec `logging/iceoryx2-logger`, requirement "Iceoryx2 logger configuration".
void to_json(nlohmann::json& j, const Iceoryx2LoggerConfig& cfg);
/// @brief Cold-path JSON reader that is hostile-input safe.
///
/// Absent or ill-typed fields leave the corresponding member untouched, so the
/// call never throws under `JSON_NOEXCEPTION=1`.
///
/// @param j Source JSON object.
/// @param out Destination config, updated field by field.
/// @note Project mandate 10; the body lives in `iceoryx2_logger_config.cpp`.
void from_json(const nlohmann::json& j, Iceoryx2LoggerConfig& out) noexcept;

} // namespace nexigen::logging

/// @brief `std::formatter` specialization: the config formats as its diagnostic string.
///
/// Forwards to `std::formatter<std::string_view>` after materializing
/// `to_string()`, so every standard format spec accepted for strings applies.
///
/// @note Project mandate 11 (`std::formatter` for every public value type).
template <>
struct std::formatter<nexigen::logging::Iceoryx2LoggerConfig> : std::formatter<std::string_view> {
    /// @brief Write the config's diagnostic text into the format output.
    ///
    /// @tparam Ctx Format context type supplied by `std::format`.
    /// @param cfg Config to render.
    /// @param ctx Format context receiving the text.
    /// @return Iterator past the last written character, as returned by the string formatter.
    /// @note Allocates the temporary diagnostic string; cold path only.
    template <class Ctx>
    auto format(const nexigen::logging::Iceoryx2LoggerConfig& cfg, Ctx& ctx) const {
        const std::string text = cfg.to_string();
        return std::formatter<std::string_view>::format(text, ctx);
    }
};
