// nexigen::chrono — device-domain map estimator (change add-time-base-service;
// spec chrono/time-base "Coefficient history"; design D-F).
//
// The daemon feeds the estimator `(native ticks, canonical ns)` pairs obtained
// from a hardware-latched correlation pair. PCIe read latency is one-sided — it
// only ever adds delay to the canonical side — so the estimate uses the sample
// with the smallest residual as the anchor and reports the residual spread as
// uncertainty. The scale comes from the window endpoints, which minimises the
// influence of any single delayed sample.
//
// The class holds a fixed sample window and allocates nothing: it is cold path
// in the daemon only, never part of a producer path.
#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>

#include <nexigen/chrono/domain_coefficient.hpp>
#include <nexigen/chrono/time_domain_class.hpp>
#include <nexigen/chrono/detail/domain_map.hpp>
#include <nexigen/chrono/lock_state.hpp>

namespace nexigen::chrono {

/// @brief Fixed-window estimator of one device domain's affine map.
///
/// @note Spec `chrono/time-base`, requirement "Domain coefficient row".
class TimeBaseEstimator {
public:
    /// @brief Maximum pairs kept in one fit window.
    static constexpr std::size_t max_samples = 64U;

    /// @brief Drop every sample and start a new window.
    void reset() noexcept {
        count_ = 0U;
        next_ = 0U;
    }

    /// @brief Add one correlation pair.
    ///
    /// When the window is full the oldest sample is overwritten, so the
    /// estimator tracks slow drift without unbounded storage.
    ///
    /// @param native Device counter value.
    /// @param canonical_ns Canonical instant measured for that counter value.
    /// @note Cold path only.
    void add_sample(std::uint64_t native, std::int64_t canonical_ns) noexcept {
        samples_[next_] = Sample{native, canonical_ns};
        next_ = (next_ + 1U) % max_samples;
        if (count_ < max_samples) {
            ++count_;
        }
    }

    /// @brief Number of pairs currently in the window.
    ///
    /// @return Sample count in `[0, max_samples]`.
    [[nodiscard]] std::size_t sample_count() const noexcept { return count_; }

    /// @brief Fit a coefficient row from the current window.
    ///
    /// Requires at least two samples with distinct counter values. The row's
    /// lock state is `Locked` when the residual spread stays within
    /// `max_residual_ns`, `Holdover` otherwise.
    ///
    /// @param domain_id Domain identity to stamp on the row.
    /// @param epoch Domain epoch to stamp on the row.
    /// @param max_residual_ns Residual spread above which the row is holdover.
    /// @return The fitted row, or `std::nullopt` when the window cannot be fit.
    /// @note Cold path only.
    [[nodiscard]] std::optional<DomainCoefficient>
    estimate(std::uint16_t domain_id, std::uint64_t epoch,
             std::uint64_t max_residual_ns) const noexcept;

    /// @brief Whether a freshly fitted row differs enough to be published.
    ///
    /// A change is significant when the epoch or lock state changes, when the
    /// scale moves by more than one part per million, or when the anchor moves
    /// by more than the published uncertainty plus `uncertainty_slack_ns`.
    ///
    /// @param candidate Newly fitted row.
    /// @param published Row currently in the block.
    /// @param uncertainty_slack_ns Extra anchor tolerance on top of `unc_ns`.
    /// @return `true` when the row must be appended to the history and published.
    /// @note Cold path only.
    [[nodiscard]] static bool is_significant_change(const DomainCoefficient& candidate,
                                                    const DomainCoefficient& published,
                                                    std::uint64_t uncertainty_slack_ns) noexcept;

    /// @brief Render the estimator window as a diagnostic string.
    ///
    /// @return Text naming the sample count and the fitted row, when available.
    /// @note Project mandate 9; allocates, cold path only.
    [[nodiscard]] std::string to_string() const;

private:
    /// @brief One stored correlation pair.
    struct Sample {
        std::uint64_t native{0};   ///< Device counter value.
        std::int64_t canonical{0}; ///< Canonical instant for that value.
    };

    /// @brief Endpoint summary of the current window.
    struct Window {
        std::uint64_t min_native{0};          ///< Smallest counter value.
        std::uint64_t max_native{0};          ///< Largest counter value.
        std::int64_t min_native_canonical{0}; ///< Canonical at `min_native`.
        std::int64_t max_native_canonical{0}; ///< Canonical at `max_native`.
        std::int64_t min_canonical{0};        ///< Smallest canonical instant.
        std::int64_t max_canonical{0};        ///< Largest canonical instant.
    };

    /// @brief Endpoint fit of the current window.
    struct Fit {
        std::uint64_t scale_fwd_q32{0}; ///< Fitted forward Q32 scale.
        std::size_t anchor{0};         ///< Sample with the smallest residual.
        std::uint64_t min_residual{0}; ///< Smallest residual.
        std::uint64_t max_residual{0}; ///< Largest residual.
    };

    /// @brief Summarise the window endpoints.
    ///
    /// @return Endpoint summary, meaningful only when `sample_count() >= 2`.
    [[nodiscard]] Window window() const noexcept;

    /// @brief Fit scale and anchor from a window summary.
    ///
    /// @param summary Endpoint summary of the current window.
    /// @return The fit, or `std::nullopt` when the window cannot be fitted.
    [[nodiscard]] std::optional<Fit> fit(const Window& summary) const noexcept;

    std::array<Sample, max_samples> samples_{}; ///< Rolling window.
    std::size_t count_{0};                      ///< Valid samples in the window.
    std::size_t next_{0};                       ///< Next write slot.
};

/// @brief Free-helper mirror of `TimeBaseEstimator::to_string`.
///
/// @param estimator Estimator to render.
/// @return The same text as the member.
/// @note Project mandate 9.
[[nodiscard]] std::string to_string(const TimeBaseEstimator& estimator);

} // namespace nexigen::chrono
