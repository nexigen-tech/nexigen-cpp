// nexigen::chrono — the clock service: configuration and provider (change
// add-chrono-clock-service; spec chrono/chrono-provider).
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
// steady-state snapshot path: all working state (worker, atomics) is fixed
// members.
#pragma once

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <mutex>
#include <string>
#include <thread>

#include <nlohmann/json.hpp>

#include <nexigen/chrono/chrono_snapshot.hpp>
#include <nexigen/chrono/concepts/chrono_sink_concept.hpp>
#include <nexigen/core/result.hpp>

namespace nexigen::chrono {

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

namespace detail {

/// Type-erased sink binding: plain function pointers, no virtual dispatch.
struct Sink_binding {
    void* self{nullptr};
    core::Status (*on_snapshot)(void*, const Chrono_snapshot&) noexcept {nullptr};
    core::Status (*flush)(void*) noexcept {nullptr};
};

} // namespace detail

/// The clock service: samples base clock domains and delivers UTC-offset
/// snapshots to a bound sink.
class ChronoProvider {
public:
    /// Bind a sink and start the provider. The sink MUST outlive the provider.
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

    ChronoProvider_config config_{};
    detail::Sink_binding binding_{};

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
