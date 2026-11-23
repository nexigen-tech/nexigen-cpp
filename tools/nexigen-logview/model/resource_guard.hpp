// nexigen-logview — CPU/rate-driven ECO mode with hysteresis.
#pragma once

#include <cstdint>
#include <format>
#include <string>

#include "eco_trigger.hpp"
#include "resource_mode.hpp"

namespace nexigen::logview {

/// @brief Thresholds and windows of the resource guard.
struct ResourceGuardConfig {
    std::uint32_t cpu_limit_percent{50};       ///< Entry CPU share.
    std::uint32_t entry_windows{3};            ///< Consecutive windows above the limit.
    std::uint32_t exit_windows{2};             ///< Consecutive windows below the exit ratio.
    double exit_ratio{0.7};                    ///< Exit below this fraction of the limit.
    std::uint64_t storm_rate{100000};          ///< Entry rate in messages per second.
    std::uint64_t window_ns{1'000'000'000ULL}; ///< Sampling window.

    /// @brief Cold-path diagnostic rendering.
    /// @return Text with every threshold.
    [[nodiscard]] std::string to_string() const;
};

/// @brief Pure state machine turning CPU/rate samples into NORMAL or ECO.
///
/// The guard takes absolute sample values so tests can drive it with a
/// simulated clock. Sampling itself lives in the UI loop
/// (`CLOCK_PROCESS_CPUTIME_ID`), not here.
class ResourceGuard {
public:
    /// @brief Construct a guard with thresholds.
    /// @param config Thresholds and windows.
    explicit ResourceGuard(ResourceGuardConfig config = {}) noexcept : config_(config) {}

    /// @brief Feed one CPU/rate sample.
    ///
    /// CPU time is a monotonically increasing process counter; the guard
    /// computes the share over each full window. The rate EWMA is updated on
    /// every call and a storm enters ECO immediately.
    ///
    /// @param now_ns Monotonic time of the sample.
    /// @param process_cpu_ns Process CPU time (`CLOCK_PROCESS_CPUTIME_ID`).
    /// @param rate Current incoming rate in messages per second.
    /// @return The mode after this sample.
    ResourceMode observe(std::uint64_t now_ns, std::uint64_t process_cpu_ns,
                         std::uint64_t rate) noexcept;

    /// @brief Current mode.
    /// @return Mode value.
    [[nodiscard]] ResourceMode mode() const noexcept { return mode_; }
    /// @brief Current trigger.
    /// @return Trigger value.
    [[nodiscard]] EcoTrigger trigger() const noexcept { return trigger_; }
    /// @brief Last measured CPU share in percent.
    /// @return Percent in `0..100+`.
    [[nodiscard]] double cpu_percent() const noexcept { return last_cpu_percent_; }
    /// @brief Current rate EWMA.
    /// @return Messages per second.
    [[nodiscard]] double rate_ewma() const noexcept { return rate_ewma_; }
    /// @brief Target frame interval in seconds.
    /// @return `1/15` in ECO, `0` (event-driven vsync) in NORMAL.
    [[nodiscard]] double frame_interval_seconds() const noexcept {
        return mode_ == ResourceMode::Eco ? (1.0 / 15.0) : 0.0;
    }
    /// @brief Whether filters/statistics may be recomputed now.
    /// @param now_ns Current monotonic time.
    /// @return `true` in NORMAL or when 250 ms elapsed in ECO.
    [[nodiscard]] bool recompute_due(std::uint64_t now_ns) noexcept;
    /// @brief Reset the state machine and counters.
    void reset() noexcept;

    /// @brief Cold-path diagnostic rendering.
    /// @return Text with mode, trigger, CPU share, and rate.
    [[nodiscard]] std::string to_string() const;

private:
    /// @brief Recompute mode from the current counters.
    void evaluate() noexcept;

    ResourceGuardConfig config_{};            ///< Thresholds.
    ResourceMode mode_{ResourceMode::Normal}; ///< Current mode.
    EcoTrigger trigger_{EcoTrigger::None};    ///< Current trigger.
    std::uint64_t window_start_ns_{0};        ///< Current window start.
    std::uint64_t window_cpu_ns_{0};          ///< CPU time at window start.
    std::uint64_t above_windows_{0};          ///< Consecutive CPU-above windows.
    std::uint64_t below_windows_{0};          ///< Consecutive below-exit windows.
    double last_cpu_percent_{0.0};            ///< Last window CPU share.
    double rate_ewma_{0.0};                   ///< Rate EWMA.
    std::uint64_t last_rate_{0};              ///< Last raw rate sample.
    bool initialized_{false};                 ///< First sample seen.
    std::uint64_t last_recompute_ns_{0};      ///< Last filter/statistics recomputation.
};

/// @brief Free diagnostic mirror of `ResourceGuard::to_string`.
/// @param guard Guard to render.
/// @return The same text.
[[nodiscard]] std::string to_string(const ResourceGuard& guard);

} // namespace nexigen::logview
