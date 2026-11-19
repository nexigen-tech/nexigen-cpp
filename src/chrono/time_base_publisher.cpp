// nexigen::chrono — daemon publish loop implementation (change
// add-time-base-service; spec chrono/time-base "Coefficient history";
// design D-F).
//
// Cold path only: one clock sample, one table comparison, at most a handful of
// history appends, and one seqlock publication per interval. No allocation.
#include <chrono>
#include <cstdint>
#include <string_view>

#include <nexigen/chrono/time_base_estimator.hpp>
#include <nexigen/chrono/time_base_publisher.hpp>

namespace nexigen::chrono {
namespace {

/// @brief Copy a string view into the fixed boot-UUID storage.
///
/// @param out Destination buffer.
/// @param uuid Source text.
void set_uuid(std::array<char, 37>& out, std::string_view uuid) noexcept {
    const std::size_t count = uuid.size() < out.size() - 1U ? uuid.size() : out.size() - 1U;
    for (std::size_t i = 0; i < count; ++i) {
        out[i] = uuid[i];
    }
    out[count] = '\0';
}

/// @brief View over the fixed boot-UUID storage.
///
/// @param uuid Stored text.
/// @return View without the terminator.
[[nodiscard]] std::string_view uuid_view(const std::array<char, 37>& uuid) noexcept {
    std::size_t length = 0;
    while (length < uuid.size() && uuid[length] != '\0') {
        ++length;
    }
    return std::string_view{uuid.data(), length};
}

/// @brief Absolute difference of two unsigned scales.
///
/// @param lhs First value.
/// @param rhs Second value.
/// @return Non-negative difference.
[[nodiscard]] std::uint64_t scale_difference(std::uint64_t lhs, std::uint64_t rhs) noexcept {
    return lhs > rhs ? lhs - rhs : rhs - lhs;
}

/// @brief Copy a device name into a row's fixed-size field.
///
/// @param row Destination row.
/// @param name Borrowed name.
void set_row_name(DomainCoefficient& row, std::string_view name) noexcept {
    const std::size_t take =
        name.size() < sizeof(row.name) - 1U ? name.size() : sizeof(row.name) - 1U;
    for (std::size_t i = 0; i < take; ++i) {
        row.name[i] = name[i];
    }
    row.name[take] = '\0';
}

/// @brief Stamp a fitted device row with pair flags, uncertainty and name.
///
/// @param row Fitted row to update.
/// @param pair Pair the row was fitted from.
/// @param backend Backend that produced the pair.
/// @param max_residual_ns Residual bound distinguishing locked from holdover.
void apply_pair_metadata(DomainCoefficient& row, const TimeBaseCorrelationPair& pair,
                         const TimeBaseDeviceBackend& backend,
                         std::uint64_t max_residual_ns) noexcept {
    if (pair.uncertainty_ns > row.unc_ns) {
        row.unc_ns = pair.uncertainty_ns;
    }
    if (row.lock == LockState::Locked && row.unc_ns > max_residual_ns) {
        row.lock = LockState::Holdover;
    }
    row.flags |= DomainCoefficient::has_reverse_bit;
    if (pair.latched) {
        row.flags |= DomainCoefficient::latched_bit;
    }
    if (pair.ptm_capable) {
        row.flags |= DomainCoefficient::ptm_capable_bit;
    }
    if (backend.mode() == DeviceAccessMode::ManualPair) {
        row.flags |= DomainCoefficient::manual_pair_bit;
        row.flags &= static_cast<std::uint16_t>(~DomainCoefficient::ptm_capable_bit);
    }
    set_row_name(row, backend.name());
}

} // namespace

TimeBasePublisher::TimeBasePublisher(detail::TimeBaseSegment& segment,
                                     const TimeBaseDaemonConfig& config, HostClockSource source,
                                     HistorySink history, std::string_view boot_uuid,
                                     std::uint64_t boot_token) noexcept
    : segment_(&segment), writer_(*segment.block()), config_(config), source_(source),
      history_(history), boot_token_(boot_token), generation_(segment.block()->generation),
      monitor_(config.time_sync), policy_mode_(config.sync_mode),
      sync_mode_(config.sync_mode == SyncMode::PtpSynced ? SyncMode::Standalone
                                                         : config.sync_mode) {
    set_uuid(boot_uuid_, boot_uuid);
}

void TimeBasePublisher::measure(std::chrono::nanoseconds window) noexcept {
    counter_scale_ = measure_counter_scale(source_, window);
}

bool TimeBasePublisher::append_history(const TimeBaseHistoryEntry& entry) noexcept {
    if (history_.append == nullptr) {
        return true;
    }
    return history_.append(history_.self, entry);
}

const DomainCoefficient* TimeBasePublisher::last_row(std::uint16_t domain_id) const noexcept {
    for (std::uint32_t i = 0; i < last_count_; ++i) {
        if (last_[i].domain_id == domain_id) {
            return &last_[i];
        }
    }
    return nullptr;
}

bool TimeBasePublisher::bind_device(TimeBaseDeviceBackend backend) noexcept {
    if (!backend.valid() || device_count_ >= devices_.size()) {
        return false;
    }
    for (std::uint32_t i = 0; i < device_count_; ++i) {
        if (devices_[i].backend.domain_id() == backend.domain_id()) {
            return false;
        }
    }
    const std::string_view name = backend.name();
    bool declared = config_.domains.empty();
    for (const HardwareDomainConfig& domain : config_.domains) {
        if (domain.name == name) {
            declared = true;
            break;
        }
    }
    DeviceRuntime& runtime = devices_[device_count_];
    runtime = DeviceRuntime{};
    runtime.backend = backend;
    runtime.declared = declared;
    ++device_count_;
    return true;
}

std::uint32_t TimeBasePublisher::missing_domain_count() const noexcept {
    std::uint32_t missing = 0U;
    for (const HardwareDomainConfig& domain : config_.domains) {
        bool found = false;
        for (std::uint32_t i = 0; i < device_count_; ++i) {
            if (devices_[i].backend.name() == domain.name) {
                found = true;
                break;
            }
        }
        if (!found) {
            ++missing;
        }
    }
    return missing;
}

void TimeBasePublisher::apply_time_sync_verdict(TimeBaseBlock& staged,
                                                std::uint64_t now_ns) noexcept {
    const std::uint32_t count =
        staged.domain_count < time_base_max_domains ? staged.domain_count : time_base_max_domains;
    for (std::uint32_t i = 0; i < count; ++i) {
        DomainCoefficient& row = staged.rows[i];
        if (row.domain_id != k_realtime_domain_id) {
            continue;
        }
        const std::optional<TimeSyncSample> verdict =
            monitor_.sample(static_cast<std::int64_t>(row.native_anchor), now_ns);
        if (!verdict.has_value()) {
            return;
        }
        switch (verdict->status) {
            case TimeSyncStatus::Stepped:
            case TimeSyncStatus::GoingBackwards:
                staged.service_flags.set(ServiceFlag::StepDetected);
                row.lock = LockState::Holdover;
                break;
            case TimeSyncStatus::RateExceeded:
                staged.service_flags.set(ServiceFlag::SlewActive);
                break;
            case TimeSyncStatus::Ok:
                if (row.lock != LockState::Locked) {
                    row.lock = LockState::Holdover;
                }
                break;
        }
        return;
    }
}

void TimeBasePublisher::apply_adjtimex_flags(TimeBaseBlock& staged) noexcept {
    const core::Result<AdjtimexStatus> discipline = read_adjtimex();
    if (!discipline) {
        return;
    }
    if (!discipline->pll_engaged) {
        // Without a disciplined PLL the kernel has no opinion to report; the
        // leap-table-derived `tai_available`/`tai_synchronized` bits stand.
        return;
    }
    if (discipline->unsynchronized) {
        staged.service_flags.clear(ServiceFlag::PtpStackReady);
    }
    if (discipline->pll_engaged && discipline->freq_ppb != 0) {
        staged.service_flags.set(ServiceFlag::SlewActive);
    }
    constexpr std::int64_t step_threshold_ns = 1'000'000;
    if (discipline->offset_ns > step_threshold_ns || discipline->offset_ns < -step_threshold_ns) {
        staged.service_flags.set(ServiceFlag::StepDetected);
    }
}

void TimeBasePublisher::feed_system_filters(TimeBaseBlock& staged, std::uint64_t now_ns) noexcept {
    const std::uint64_t dt =
        last_filter_ns_ == 0U || now_ns < last_filter_ns_ ? 0U : now_ns - last_filter_ns_;
    last_filter_ns_ = now_ns;
    const std::uint32_t count =
        staged.domain_count < time_base_max_domains ? staged.domain_count : time_base_max_domains;
    for (std::uint32_t i = 0; i < count; ++i) {
        const DomainCoefficient& row = staged.rows[i];
        if (row.domain_id == k_monotonic_domain_id) {
            monotonic_filter_.update(static_cast<std::int64_t>(row.native_anchor) -
                                         static_cast<std::int64_t>(now_ns),
                                     dt);
        } else if (row.domain_id == k_tai_domain_id) {
            tai_filter_.update(static_cast<std::int64_t>(row.native_anchor) -
                                   static_cast<std::int64_t>(now_ns),
                               dt);
        } else if (row.domain_id == k_realtime_domain_id) {
            realtime_filter_.update(static_cast<std::int64_t>(row.native_anchor) -
                                        static_cast<std::int64_t>(now_ns),
                                    dt);
        }
    }
    if (realtime_filter_.initialized()) {
        const std::uint64_t variance =
            static_cast<std::uint64_t>(realtime_filter_.offset_variance_ns2());
        if (variance > staged.utc_unc_ns * staged.utc_unc_ns) {
            staged.utc_unc_ns = variance;
        }
    }
}

std::optional<DomainCoefficient> TimeBasePublisher::fit_device_row(DeviceRuntime& runtime,
                                                                   std::uint64_t now_ns) noexcept {
    TimeBaseCorrelationPair pair{};
    if (!runtime.backend.read(pair)) {
        return std::nullopt;
    }
    runtime.estimator.add_sample(pair.native_ticks, pair.canonical_ns);
    std::optional<DomainCoefficient> row = runtime.estimator.estimate(
        runtime.backend.domain_id(), runtime.backend.epoch(), config_.max_residual_ns);
    if (!row.has_value()) {
        return std::nullopt;
    }
    apply_pair_metadata(*row, pair, runtime.backend, config_.max_residual_ns);
    const std::uint64_t interval = static_cast<std::uint64_t>(config_.publish_interval.count());
    if (row->valid_to_canonical < now_ns + interval) {
        row->valid_to_canonical = now_ns + interval;
    }
    return row;
}

std::uint32_t TimeBasePublisher::append_device_rows(TimeBaseBlock& staged,
                                                    std::uint64_t now_ns) noexcept {
    const std::uint64_t interval = static_cast<std::uint64_t>(config_.publish_interval.count());
    std::uint32_t appended = 0U;
    for (std::uint32_t i = 0; i < device_count_ && staged.domain_count < time_base_max_domains;
         ++i) {
        DeviceRuntime& runtime = devices_[i];
        std::optional<DomainCoefficient> row = fit_device_row(runtime, now_ns);
        if (!row.has_value() && runtime.published) {
            // Holdover keeps the last map; the lock state carries the fact that
            // no fresh pair arrived.
            row = runtime.last;
            row->lock = LockState::Holdover;
            row->valid_from_canonical = now_ns;
            row->valid_to_canonical = now_ns + interval;
        }
        if (!row.has_value()) {
            continue;
        }
        staged.rows[staged.domain_count] = *row;
        ++staged.domain_count;
        ++appended;
        runtime.last = *row;
        runtime.published = true;
    }
    return appended;
}

void TimeBasePublisher::fill_snapshot(TimeBaseBlock& staged, std::uint64_t now_ns,
                                      std::uint64_t generation, TimeBaseStatus status) noexcept {
    if (last_snapshot_ns_ != 0U && now_ns >= last_snapshot_ns_ &&
        now_ns - last_snapshot_ns_ < 1'000'000'000ULL) {
        staged.last_snapshot = last_snapshot_;
        return;
    }
    TimeSnapshot& snapshot = staged.last_snapshot;
    snapshot = TimeSnapshot{};
    snapshot.generation = generation;
    snapshot.boot_token = boot_token_;
    snapshot.status = status;
    snapshot.sync_mode = sync_mode_;
    snapshot.service_flags = staged.service_flags;
    snapshot.monotonic_raw_ns = now_ns;
    snapshot.tai_offset_ns = staged.tai_offset_ns;
    snapshot.utc_offset_ns = staged.utc_offset_ns;
    snapshot.unc_ns = staged.utc_unc_ns;

    const std::uint32_t count =
        staged.domain_count < time_base_max_domains ? staged.domain_count : time_base_max_domains;
    for (std::uint32_t i = 0; i < count; ++i) {
        const DomainCoefficient& row = staged.rows[i];
        std::uint64_t value = row.native_anchor;
        if (row.has_reverse()) {
            value = native_from_canonical(row, static_cast<std::int64_t>(now_ns));
        }
        if (row.domain_id == k_monotonic_domain_id) {
            snapshot.monotonic_ns = value;
        } else if (row.domain_id == k_tai_domain_id) {
            snapshot.tai_ns = value;
        } else if (row.domain_id == k_realtime_domain_id) {
            snapshot.realtime_ns = value;
        }
        SnapshotDomainValue& entry = snapshot.domains[i];
        entry.domain_id = row.domain_id;
        entry.domain_class = row.domain_class;
        entry.lock = row.lock;
        entry.flags = row.flags;
        entry.epoch = row.epoch;
        entry.value = value;
    }
    snapshot.domain_count = count;
    last_snapshot_ = snapshot;
    last_snapshot_ns_ = now_ns;
}

bool TimeBasePublisher::row_changed(const DomainCoefficient& candidate,
                                    const DomainCoefficient& published) noexcept {
    if (candidate.domain_id != published.domain_id ||
        candidate.domain_class != published.domain_class || candidate.epoch != published.epoch ||
        candidate.lock != published.lock) {
        return true;
    }
    const std::uint64_t difference =
        scale_difference(candidate.scale_fwd_q32, published.scale_fwd_q32);
    if (difference != 0U && difference * 1'000'000U > published.scale_fwd_q32) { // > 1 ppm
        return true;
    }
    if (candidate.domain_class == TimeDomainClass::DeviceClock) {
        // Device maps are re-anchored to the least-delayed sample; a genuine
        // move is one the published map cannot explain.
        return TimeBaseEstimator::is_significant_change(candidate, published, 0U);
    }
    return false; // host rows are identity-measured; only scale/lock/epoch matter
}

SyncMode TimeBasePublisher::derive_sync_mode(const TimeBaseBlock& staged,
                                             std::uint64_t now_ns) noexcept {
    if (policy_mode_ != SyncMode::PtpSynced) {
        sync_mode_ = policy_mode_;
        return sync_mode_;
    }
    const bool tai_ok = staged.service_flags.has(ServiceFlag::TaiSynchronized) &&
                        !staged.service_flags.has(ServiceFlag::StepDetected);
    if (tai_ok) {
        was_synced_ = true;
        holdover_since_ns_ = 0U;
        sync_mode_ = SyncMode::PtpSynced;
        return sync_mode_;
    }
    if (was_synced_) {
        if (holdover_since_ns_ == 0U) {
            holdover_since_ns_ = now_ns;
        }
        const std::uint64_t window =
            config_.holdover_window.count() > 0
                ? static_cast<std::uint64_t>(config_.holdover_window.count())
                : 0U;
        if (now_ns >= holdover_since_ns_ && now_ns - holdover_since_ns_ <= window) {
            sync_mode_ = SyncMode::Holdover;
            return sync_mode_;
        }
    }
    sync_mode_ = SyncMode::Standalone;
    return sync_mode_;
}

bool TimeBasePublisher::row_publishable(const DomainCoefficient& row) noexcept {
    return row.scale_fwd_q32 != 0U && row.has_reverse() && row.scale_rev_q32 != 0U;
}

void TimeBasePublisher::finalize_publication(TimeBaseBlock& staged, TimeBaseStatus& status,
                                             const HostDomainResult& sampled) noexcept {
    static_cast<void>(sampled);
    // A non-positive or non-reversible scale can never be published; drop the
    // row and degrade instead of letting consumers divide by zero.
    std::uint32_t keep = 0U;
    for (std::uint32_t i = 0; i < staged.domain_count; ++i) {
        if (!row_publishable(staged.rows[i])) {
            if (status == TimeBaseStatus::Ready) {
                status = TimeBaseStatus::Degraded;
            }
            continue;
        }
        if (keep != i) {
            staged.rows[keep] = staged.rows[i];
        }
        ++keep;
    }
    if (keep != staged.domain_count) {
        for (std::uint32_t i = keep; i < staged.domain_count; ++i) {
            staged.rows[i] = DomainCoefficient{};
        }
        staged.domain_count = static_cast<std::uint16_t>(keep);
    }
    if (missing_domain_count() == 0U) {
        staged.service_flags.set(ServiceFlag::DeclaredDomainsComplete);
    } else if (status == TimeBaseStatus::Ready) {
        status = TimeBaseStatus::Degraded;
    }
    for (std::uint32_t i = 0; i < device_count_; ++i) {
        if (devices_[i].backend.mode() == DeviceAccessMode::PhcCrossTimestamp) {
            staged.service_flags.set(ServiceFlag::PhcPresent);
            break;
        }
    }
}

void TimeBasePublisher::publish(std::uint64_t now_ns) noexcept {
    if (segment_ == nullptr || segment_->block() == nullptr) {
        return;
    }

    TimeBaseBlock staged{};
    staged.boot_token = boot_token_;
    staged.utc_offset_ns = utc_offset_ns_;
    staged.utc_unc_ns = utc_unc_ns_;
    staged.service_flags = base_flags_;
    staged.sync_mode = sync_mode_;

    const HostDomainResult sampled = sample_host_domains(source_, staged, boot_token_, now_ns,
                                                         counter_scale_, config_.publish_tsc);
    static_cast<void>(sample_system_domains(source_, staged, now_ns, boot_token_));
    apply_time_sync_verdict(staged, now_ns);
    feed_system_filters(staged, now_ns);
    apply_adjtimex_flags(staged);
    append_device_rows(staged, now_ns);
    staged.sync_mode = derive_sync_mode(staged, now_ns);

    TimeBaseStatus status = sampled.status;
    finalize_publication(staged, status, sampled);
    const std::uint64_t next_generation = generation_ + 2U;

    // An interval longer than three publish periods means the daemon was not
    // running; record that explicitly instead of letting the analysis bridge it.
    if (last_publish_ns_ != 0U && now_ns > last_publish_ns_ &&
        now_ns - last_publish_ns_ >
            static_cast<std::uint64_t>(config_.publish_interval.count()) * 3U) {
        TimeBaseHistoryEntry gap{};
        gap.generation = next_generation;
        gap.status = status;
        gap.sync_mode = staged.sync_mode;
        gap.service_flags = staged.service_flags;
        gap.gap = true;
        set_uuid(gap.boot_uuid, uuid_view(boot_uuid_));
        static_cast<void>(append_history(gap));
    }

    for (std::uint32_t i = 0; i < staged.domain_count; ++i) {
        const DomainCoefficient& row = staged.rows[i];
        const DomainCoefficient* const previous = last_row(row.domain_id);
        if (previous == nullptr || row_changed(row, *previous)) {
            TimeBaseHistoryEntry entry{};
            entry.generation = next_generation;
            entry.status = status;
            entry.sync_mode = staged.sync_mode;
            entry.service_flags = staged.service_flags;
            entry.coefficient = row;
            set_uuid(entry.boot_uuid, uuid_view(boot_uuid_));
            static_cast<void>(append_history(entry));
        }
    }

    // Domains that disappeared are recorded as unavailable, never silently
    // dropped: a reader must be able to see when a domain stopped being valid.
    for (std::uint32_t i = 0; i < last_count_; ++i) {
        if (staged.find(last_[i].domain_id) != nullptr) {
            continue;
        }
        DomainCoefficient removed = last_[i];
        removed.lock = LockState::Unsupported;
        removed.valid_from_native = 0U;
        removed.valid_to_native = 0U;
        removed.valid_from_canonical = now_ns;
        removed.valid_to_canonical = now_ns;
        TimeBaseHistoryEntry entry{};
        entry.generation = next_generation;
        entry.status = status;
        entry.sync_mode = staged.sync_mode;
        entry.service_flags = staged.service_flags;
        entry.coefficient = removed;
        set_uuid(entry.boot_uuid, uuid_view(boot_uuid_));
        static_cast<void>(append_history(entry));
    }

    last_ = staged.rows;
    last_count_ = staged.domain_count;
    status_ = status;
    generation_ = next_generation;
    last_publish_ns_ = now_ns;

    staged.status = status;
    staged.generation = next_generation;
    fill_snapshot(staged, now_ns, next_generation, status);
    writer_.publish(staged, now_ns);
}

void TimeBasePublisher::mark_lost(std::uint64_t now_ns) noexcept {
    if (segment_ == nullptr || segment_->block() == nullptr) {
        return;
    }
    TimeBaseBlock staged{};
    staged.boot_token = boot_token_;
    staged.utc_offset_ns = utc_offset_ns_;
    staged.utc_unc_ns = utc_unc_ns_;
    staged.service_flags = base_flags_;
    staged.domain_count = static_cast<std::uint16_t>(last_count_);
    staged.rows = last_;
    staged.sync_mode = sync_mode_;
    staged.status = TimeBaseStatus::Lost;
    staged.last_snapshot = last_snapshot_;
    staged.last_snapshot.status = TimeBaseStatus::Lost;
    last_snapshot_.status = TimeBaseStatus::Lost;
    generation_ += 2U;
    staged.generation = generation_;
    status_ = TimeBaseStatus::Lost;
    writer_.publish(staged, now_ns);
}

} // namespace nexigen::chrono
