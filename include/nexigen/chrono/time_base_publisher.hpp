// nexigen::chrono — daemon publish loop (change add-time-base-service; spec
// chrono/time-base "Coefficient history", "Time-base status"; design D-F).
//
// One call to `publish()` turns a clock sample into a complete block: it samples
// the host domains, detects which rows changed enough to matter, appends those
// changes (plus removal and gap entries) to the history, and then publishes the
// block under the seqlock. Host rows change their anchor every interval but
// represent the same affine map; only a scale, epoch, lock or class change is
// recorded for them, so the history stays small.
#pragma once

#include <array>
#include <chrono>
#include <cstdint>
#include <span>
#include <string>
#include <string_view>

#include <nexigen/chrono/adjtimex_status.hpp>
#include <nexigen/chrono/detail/kalman2.hpp>
#include <nexigen/chrono/detail/time_base_segment.hpp>
#include <nexigen/chrono/detail/time_base_seqlock.hpp>
#include <nexigen/chrono/domain_coefficient.hpp>
#include <nexigen/chrono/time_base_block.hpp>
#include <nexigen/chrono/time_base_daemon_config.hpp>
#include <nexigen/chrono/time_base_device.hpp>
#include <nexigen/chrono/time_base_estimator.hpp>
#include <nexigen/chrono/time_base_history_entry.hpp>
#include <nexigen/chrono/time_base_host_domain.hpp>
#include <nexigen/chrono/service_flags.hpp>
#include <nexigen/chrono/time_snapshot.hpp>
#include <nexigen/chrono/time_sync_monitor.hpp>
#include <nexigen/chrono/time_base_status.hpp>

namespace nexigen::chrono {

/// @brief Cold-path daemon publisher over one mapped segment.
///
/// Owns the block writer and the last published table; the history sink is a
/// function table so the daemon can stream to a file while tests record in
/// memory.
///
/// @note Spec `chrono/time-base`, requirement "Coefficient history".
class TimeBasePublisher {
public:
    /// @brief Sink receiving appended history entries.
    struct HistorySink {
        void* self{nullptr}; ///< Opaque sink state.
        /// @brief Append callback.
        bool (*append)(void*, const TimeBaseHistoryEntry&) noexcept {nullptr};
    };

    /// @brief Bind the publisher to a segment, configuration and history sink.
    ///
    /// @param segment Mapped owner segment; it must outlive the publisher.
    /// @param config Daemon configuration.
    /// @param source Host clock source (borrowed state).
    /// @param history History sink; may be empty when history is not wanted.
    /// @param boot_uuid Full kernel boot id written into history entries.
    /// @param boot_token 64-bit boot token written into the block.
    TimeBasePublisher(detail::TimeBaseSegment& segment, const TimeBaseDaemonConfig& config,
                      HostClockSource source, HistorySink history, std::string_view boot_uuid,
                      std::uint64_t boot_token) noexcept;

    /// @brief Measure the counter scale once at start.
    ///
    /// @param window Sleep between the two calibration reads.
    /// @note Cold path only.
    void measure(std::chrono::nanoseconds window) noexcept;

    /// @brief Sample, append history changes, and publish a new generation.
    ///
    /// @param now_ns Canonical instant driving the publication.
    /// @note Cold path only; no allocation.
    void publish(std::uint64_t now_ns) noexcept;

    /// @brief Bind one declared hardware domain to its backend.
    ///
    /// Every domain gets its own estimator and row. Call once per declared
    /// domain at startup, before the first publication.
    ///
    /// @param backend Backend to bind; its borrowed state must outlive the
    ///                publisher.
    /// @return `true` when the backend was bound, `false` when the table is
    ///         full, the backend is invalid, or the domain id is duplicated.
    /// @note Cold path only.
    [[nodiscard]] bool bind_device(TimeBaseDeviceBackend backend) noexcept;

    /// @brief Number of bound hardware domains.
    /// @return Bound-device count.
    /// @note Spec `chrono/time-base`, requirement "Daemon configuration".
    [[nodiscard]] std::uint32_t device_count() const noexcept { return device_count_; }

    /// @brief Number of declared domains that are not bound.
    /// @return Declared-but-missing domain count.
    /// @note Spec `chrono/time-base`, requirement "Daemon configuration".
    [[nodiscard]] std::uint32_t missing_domain_count() const noexcept;

    /// @brief The published synchronization mode (derived from the policy).
    /// @return Mode carried by the latest publication.
    /// @note Spec `chrono/time-sync`, requirement "Platform sync mode".
    [[nodiscard]] SyncMode sync_mode() const noexcept { return sync_mode_; }

    /// @brief Set the synchronization policy for the next publications.
    ///
    /// `Standalone` and `Holdover` are published verbatim. `PtpSynced` is
    /// *derived* every interval from `CLOCK_TAI`/step state: the published mode
    /// is `PtpSynced` while TAI is disciplined, `Holdover` for
    /// `holdover_window` after a loss, then `Standalone`.
    ///
    /// @param mode Deployment policy.
    void set_sync_mode(SyncMode mode) noexcept {
        policy_mode_ = mode;
        if (mode != SyncMode::PtpSynced) {
            sync_mode_ = mode;
        }
    }

    /// @brief Publish a final `Lost` status at shutdown.
    ///
    /// @param now_ns Canonical instant of the final publication.
    /// @note Cold path only.
    void mark_lost(std::uint64_t now_ns) noexcept;

    /// @brief Merge daemon runtime flags into every publication.
    ///
    /// @param flags Observed runtime flags (`rt_sched_applied`, ...).
    void set_service_flags(ServiceFlags flags) noexcept { base_flags_ = flags; }

    /// @brief Replace the hub-to-UTC calibration fields.
    ///
    /// @param offset_ns Signed `UTC - hub` offset.
    /// @param unc_ns Offset uncertainty.
    /// @param valid_to_canonical Last hub instant the offset covers.
    void set_utc_calibration(std::int64_t offset_ns, std::uint64_t unc_ns,
                             std::uint64_t valid_to_canonical) noexcept {
        utc_offset_ns_ = offset_ns;
        utc_unc_ns_ = unc_ns;
        static_cast<void>(valid_to_canonical);
    }

    /// @brief Current aggregate status.
    ///
    /// @return Last published status.
    [[nodiscard]] TimeBaseStatus status() const noexcept { return status_; }

    /// @brief Current logical generation (the block's seqlock value).
    ///
    /// @return Last published generation.
    [[nodiscard]] std::uint64_t generation() const noexcept { return generation_; }

    /// @brief Whether a row carries publishable bidirectional maps.
    ///
    /// A row with a zero scale or no reverse map is dropped from the staged
    /// block and degrades the publication instead of being published.
    ///
    /// @param row Row to validate.
    /// @return `true` when both scales are non-zero and the reverse flag is set.
    [[nodiscard]] static bool row_publishable(const DomainCoefficient& row) noexcept;

    /// @brief Measured Q32 counter scale.
    ///
    /// @return Scale, or `0` when the counter is not usable.
    [[nodiscard]] std::uint64_t counter_scale() const noexcept { return counter_scale_; }

private:
    /// @brief One bound hardware domain and its private estimator.
    struct DeviceRuntime {
        TimeBaseDeviceBackend backend{}; ///< Bound backend.
        TimeBaseEstimator estimator{};   ///< Private fit window.
        DomainCoefficient last{};        ///< Last published row.
        bool published{false};           ///< Whether `last` is meaningful.
        bool declared{false};            ///< Whether the config declares it.
    };

    /// @brief Append one entry through the sink.
    ///
    /// @param entry Entry to append.
    /// @return `true` when the sink accepted it (or no sink is bound).
    bool append_history(const TimeBaseHistoryEntry& entry) noexcept;

    /// @brief Read and fit one bound device's row for this publication.
    ///
    /// @param runtime Bound device state.
    /// @param now_ns Hub instant of the publication.
    /// @return The fitted row, or `std::nullopt` when no fresh pair or fit.
    [[nodiscard]] std::optional<DomainCoefficient> fit_device_row(DeviceRuntime& runtime,
                                                                  std::uint64_t now_ns) noexcept;

    /// @brief Derive the published sync mode from the observed TAI state.
    ///
    /// @param staged Block being built (flags already populated).
    /// @param now_ns Hub instant of the publication.
    /// @return Mode to publish.
    [[nodiscard]] SyncMode derive_sync_mode(const TimeBaseBlock& staged,
                                            std::uint64_t now_ns) noexcept;

    /// @brief Sample every bound device and append its row to the staged block.
    ///
    /// @param staged Block being built.
    /// @param now_ns Hub instant of the publication.
    /// @return Number of device rows appended.
    std::uint32_t append_device_rows(TimeBaseBlock& staged, std::uint64_t now_ns) noexcept;

    /// @brief Classify the realtime step and map it to flags and row lock state.
    ///
    /// @param staged Block being built (system rows already sampled).
    /// @param now_ns Hub instant of the publication.
    void apply_time_sync_verdict(TimeBaseBlock& staged, std::uint64_t now_ns) noexcept;

    /// @brief Feed the system-scale offsets into their fixed-point filters.
    ///
    /// Tracks `MONOTONIC - hub`, `TAI - hub`, and `REALTIME - hub`; the
    /// `REALTIME` filter's uncertainty widens `utc_unc_ns`.
    ///
    /// @param staged Block being built (system rows already sampled).
    /// @param now_ns Hub instant of the publication.
    void feed_system_filters(TimeBaseBlock& staged, std::uint64_t now_ns) noexcept;

    /// @brief Map the kernel clock discipline onto service flags.
    ///
    /// @param staged Block being built.
    void apply_adjtimex_flags(TimeBaseBlock& staged) noexcept;

    /// @brief Validate rows and fold the publication status and flags.
    ///
    /// Drops rows without publishable scales (degrading the status), records
    /// missing declared domains, and advertises PHC presence.
    ///
    /// @param staged Block being built.
    /// @param status Sampled status, updated in place.
    /// @param sampled Host-domain sampling result.
    void finalize_publication(TimeBaseBlock& staged, TimeBaseStatus& status,
                              const HostDomainResult& sampled) noexcept;

    /// @brief Fill the 1 Hz snapshot inside the staged block.
    ///
    /// @param staged Block being built (rows already present).
    /// @param now_ns Hub instant of the publication.
    /// @param generation Generation the snapshot belongs to.
    /// @param status Aggregate status of this publication.
    void fill_snapshot(TimeBaseBlock& staged, std::uint64_t now_ns, std::uint64_t generation,
                       TimeBaseStatus status) noexcept;

    /// @brief Whether a row changed enough to be recorded.
    ///
    /// @param candidate Newly sampled row.
    /// @param published Row currently published for the same domain.
    /// @return `true` when the change must be appended.
    [[nodiscard]] static bool row_changed(const DomainCoefficient& candidate,
                                          const DomainCoefficient& published) noexcept;

    /// @brief Find the last published row for a domain.
    ///
    /// @param domain_id Domain to look up.
    /// @return Pointer to the stored row, or `nullptr`.
    [[nodiscard]] const DomainCoefficient* last_row(std::uint16_t domain_id) const noexcept;

    detail::TimeBaseSegment* segment_{nullptr}; ///< Borrowed mapped segment.
    detail::TimeBaseWriter writer_;             ///< Block writer over the mapping.
    TimeBaseDaemonConfig config_{};             ///< Copied configuration.
    HostClockSource source_{};                  ///< Borrowed clock callbacks.
    HistorySink history_{};                     ///< Borrowed history sink.
    std::array<char, 37> boot_uuid_{};          ///< Full boot id for history.
    std::uint64_t boot_token_{0};               ///< Boot token for the block.
    std::array<DomainCoefficient, time_base_max_domains> last_{}; ///< Last published rows.
    std::uint32_t last_count_{0};                                 ///< Valid rows in `last_`.
    std::array<DeviceRuntime, time_base_max_domains> devices_{};  ///< Bound hardware domains.
    std::uint32_t device_count_{0};                               ///< Valid entries in `devices_`.
    std::uint64_t generation_{0};                                 ///< Last seqlock generation.
    std::uint64_t counter_scale_{0};                              ///< Measured Q32 scale.
    std::uint64_t last_publish_ns_{0};                            ///< Previous publication instant.
    std::uint64_t last_snapshot_ns_{0};               ///< Previous 1 Hz snapshot instant.
    TimeSnapshot last_snapshot_{};                    ///< Last published 1 Hz snapshot.
    TimeSyncMonitor monitor_{};                       ///< Realtime step/slew detector.
    detail::Kalman2 monotonic_filter_{};              ///< MONOTONIC offset/drift.
    detail::Kalman2 tai_filter_{};                    ///< TAI offset/drift.
    detail::Kalman2 realtime_filter_{};               ///< REALTIME offset/drift.
    std::uint64_t last_filter_ns_{0};                 ///< Previous filter update instant.
    std::int64_t utc_offset_ns_{0};                   ///< Hub -> UTC offset.
    std::uint64_t utc_unc_ns_{0};                     ///< Offset uncertainty.
    TimeBaseStatus status_{TimeBaseStatus::NotReady}; ///< Last status.
    SyncMode policy_mode_{SyncMode::Standalone};      ///< Deployment policy.
    SyncMode sync_mode_{SyncMode::Standalone};        ///< Published (derived) mode.
    std::uint64_t holdover_since_ns_{0};              ///< Start of the holdover window.
    bool was_synced_{false};                          ///< TAI was disciplined at least once.
    ServiceFlags base_flags_{}; ///< Daemon runtime flags merged into publications.
};

} // namespace nexigen::chrono
