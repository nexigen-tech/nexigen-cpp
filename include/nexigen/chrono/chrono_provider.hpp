// nexigen::chrono — the clock service: configuration and provider (change
// add-chrono-clock-service; spec chrono/chrono-provider). Two-phase lifecycle
// added by change integrate-chrono-into-infrastructure: construction provides
// snapshot_now() without starting the worker; start() (with an optional
// logger) starts the background snapshot worker.
//
// ChronoProvider samples the base clock domains (monotonic_raw + realtime)
// and delivers a UTC-offset Chrono_snapshot to a bound sink. It runs one
// background std::jthread worker that pins itself to the configured core mask
// before its first snapshot (so sampling and sink I/O never preempt the
// real-time threads) and delivers one snapshot per collect_interval; a zero
// interval disables the worker and leaves snapshot_now() as the sole trigger.
//
// The provider binds a ChronoSink type-erased through plain function pointers
// (no virtual dispatch, no RTTI). It performs no heap allocation on the
// steady-state snapshot path: all working state (worker, atomics, log hook) is
// fixed members. The optional worker logger is a module-local hook so chrono
// does not depend on the logging module.
#pragma once

#include <atomic>
#include <chrono>
#include <concepts>
#include <condition_variable>
#include <cstdint>
#include <mutex>
#include <string>
#include <string_view>
#include <thread>

#include <nlohmann/json.hpp>

#include <nexigen/chrono/chrono_snapshot.hpp>
#include <nexigen/chrono/concepts/chrono_sink_concept.hpp>
#include <nexigen/core/result.hpp>

namespace nexigen::chrono {

/// Severity of the clock service's own worker diagnostics. Module-local so
/// chrono does not depend on the logging module; the application bridges its
/// logger to the worker's log hook.
enum class Clock_log_level : std::uint8_t {
    info,    ///< normal operational milestone
    warning, ///< recoverable problem (pin failure, snapshot delivery failure)
    error,   ///< hard failure
};

/// Canonical name of a log level; allocation-free, constexpr.
[[nodiscard]] constexpr std::string_view to_string(Clock_log_level level) noexcept {
    switch (level) {
        case Clock_log_level::info:    return "info";
        case Clock_log_level::warning: return "warning";
        case Clock_log_level::error:   return "error";
        default:                       return "Unknown";
    }
}

/// Parse a canonical Clock_log_level name; false leaves the out-parameter
/// UNCHANGED on any other spelling.
[[nodiscard]] constexpr bool from_string(std::string_view name, Clock_log_level& out) noexcept {
    if (name == "info")    { out = Clock_log_level::info;    return true; }
    if (name == "warning") { out = Clock_log_level::warning; return true; }
    if (name == "error")   { out = Clock_log_level::error;   return true; }
    return false;
}

/// ADL JSON writer (cold path only): the value is exactly to_string(level).
inline void to_json(nlohmann::json& j, const Clock_log_level level) {
    j = std::string(to_string(level));
}

/// ADL JSON reader, strictly non-throwing under JSON_NOEXCEPTION=1.
inline void from_json(const nlohmann::json& j, Clock_log_level& out) noexcept {
    const auto* const name = j.get_ptr<const nlohmann::json::string_t*>();
    if (name != nullptr) {
        static_cast<void>(from_string(std::string_view(*name), out));
    }
}

/// Provider configuration: snapshot period and CPU affinity. A zero
/// collect_interval disables the background thread.
struct ChronoProvider_config {
    std::chrono::nanoseconds collect_interval{0};
    std::uint64_t core_mask{0};

    /// Cold-path diagnostic (mandate 9).
    [[nodiscard]] std::string to_string() const;
};

/// Free helper (mandate 9): mirrors the member.
[[nodiscard]] std::string to_string(const ChronoProvider_config& config);

/// Provider config JSON (cold path). Definitions live in
/// src/chrono/chrono_provider.cpp.
void to_json(nlohmann::json& j, const ChronoProvider_config& config);
void from_json(const nlohmann::json& j, ChronoProvider_config& out) noexcept;

namespace concepts {

/// A chrono-local logger: anything with a void log(Clock_log_level,
/// std::string_view) noexcept member. The application bridges a
/// nexigen::logging logger to this contract.
template <class T>
concept Clock_logger = requires(T& logger, Clock_log_level level, std::string_view message) {
    { logger.log(level, message) } noexcept -> std::same_as<void>;
};

} // namespace concepts

namespace detail {

/// Type-erased sink binding: plain function pointers, no virtual dispatch.
struct Sink_binding {
    void* self{nullptr};
    core::Status (*on_snapshot)(void*, const Chrono_snapshot&) noexcept {nullptr};
    core::Status (*flush)(void*) noexcept {nullptr};
};

/// Type-erased worker log hook: plain function pointers, no virtual dispatch.
struct Log_binding {
    void* self{nullptr};
    void (*log)(void*, Clock_log_level, std::string_view) noexcept {nullptr};
};

} // namespace detail

/// The clock service: samples base clock domains and delivers UTC-offset
/// snapshots to a bound sink.
class ChronoProvider {
public:
    /// Bind a sink. Construction does NOT start the background worker; call
    /// start() to begin periodic snapshotting. The sink MUST outlive the
    /// provider.
    template <concepts::ChronoSink Sink>
    ChronoProvider(const ChronoProvider_config& config, Sink& sink)
        : ChronoProvider(config,
                         detail::Sink_binding{
                             static_cast<void*>(&sink),
                             [](void* self, const Chrono_snapshot& snapshot) noexcept -> core::Status {
                                 return static_cast<Sink*>(self)->on_snapshot(snapshot);
                             },
                             [](void* self) noexcept -> core::Status {
                                 return static_cast<Sink*>(self)->flush();
                             }}) {}

    ChronoProvider(const ChronoProvider&) = delete;
    ChronoProvider& operator=(const ChronoProvider&) = delete;
    ChronoProvider(ChronoProvider&&) = delete;
    ChronoProvider& operator=(ChronoProvider&&) = delete;

    ~ChronoProvider();

    /// Start the background snapshot worker (no logger). Idempotent; a zero
    /// collect_interval starts nothing.
    [[nodiscard]] core::Status start() noexcept {
        if (worker_.joinable()) {
            return {}; // already started
        }
        if (config_.collect_interval > std::chrono::nanoseconds{0}) {
            worker_ = std::jthread([this](std::stop_token stop) { snapshot_loop(stop); });
        }
        return {};
    }

    /// Bind a logger and start the background snapshot worker. The logger MUST
    /// outlive the provider (the worker logs through it until it stops).
    template <concepts::Clock_logger Logger>
    [[nodiscard]] core::Status start(Logger& logger) noexcept {
        log_binding_ = detail::Log_binding{
            static_cast<void*>(&logger),
            [](void* self, Clock_log_level level, std::string_view message) noexcept {
                static_cast<Logger*>(self)->log(level, message);
            }};
        return start();
    }

    /// Sample the clocks and deliver one snapshot synchronously on the calling
    /// thread.
    [[nodiscard]] core::Status snapshot_now() noexcept;

    /// Flush the bound sink.
    [[nodiscard]] core::Status flush() noexcept;

    /// Result of the provider's startup steps (affinity pinning).
    [[nodiscard]] core::Status startup_status() const noexcept;

    /// The post-pin affinity mask as the worker observed it (0 before start).
    [[nodiscard]] std::uint64_t worker_mask() const noexcept;

    /// Cold-path diagnostic (mandate 9).
    [[nodiscard]] std::string to_string() const;

private:
    ChronoProvider(const ChronoProvider_config& config, detail::Sink_binding binding);

    /// Background snapshot worker body.
    void snapshot_loop(std::stop_token stop) noexcept;

    /// Emit a worker diagnostic through the bound logger, if any.
    void log(Clock_log_level level, std::string_view message) noexcept {
        if (log_binding_.log != nullptr) {
            log_binding_.log(log_binding_.self, level, message);
        }
    }

    ChronoProvider_config config_{};
    detail::Sink_binding binding_{};
    detail::Log_binding log_binding_{};

    std::jthread worker_{};
    std::mutex snapshot_mutex_{}; // serializes manual snapshot_now() with the worker
    std::mutex wake_mutex_{};
    std::condition_variable_any wake_cv_{};

    // Cache-line-separated so the control plane never false-shares with the
    // worker's hot cells (mandate 5).
    alignas(64) std::atomic<std::uint64_t> worker_mask_{0};
    alignas(64) std::atomic<core::ErrorCode> startup_error_{core::ErrorCode::Ok};
};

} // namespace nexigen::chrono

/// Clock_log_level formats as its canonical name (mandate 11).
template <>
struct std::formatter<nexigen::chrono::Clock_log_level> : std::formatter<std::string_view> {
    template <class Ctx>
    constexpr auto format(const nexigen::chrono::Clock_log_level level, Ctx& ctx) const {
        return std::formatter<std::string_view>::format(nexigen::chrono::to_string(level), ctx);
    }
};

/// The configuration formats as its diagnostic string (mandate 11).
template <>
struct std::formatter<nexigen::chrono::ChronoProvider_config>
    : std::formatter<std::string_view> {
    template <class Ctx>
    auto format(const nexigen::chrono::ChronoProvider_config& config, Ctx& ctx) const {
        const std::string text = config.to_string();
        return std::formatter<std::string_view>::format(text, ctx);
    }
};

/// The provider formats as its diagnostic string (mandate 11).
template <>
struct std::formatter<nexigen::chrono::ChronoProvider> : std::formatter<std::string_view> {
    template <class Ctx>
    auto format(const nexigen::chrono::ChronoProvider& provider, Ctx& ctx) const {
        const std::string text = provider.to_string();
        return std::formatter<std::string_view>::format(text, ctx);
    }
};
