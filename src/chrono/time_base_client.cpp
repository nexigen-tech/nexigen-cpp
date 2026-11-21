// nexigen::chrono — application-side time-base client implementation (change
// add-time-base-service; spec chrono/time-base "Client attach and required
// domains", "Holdover and fail-fast").
//
// Cold path for attach and diagnostics; hot path for refresh/conversion (one
// acquire generation load plus arithmetic). No allocation on the hot path.
#include <cstdint>
#include <format>
#include <optional>
#include <string>
#include <utility>

#include <nexigen/chrono/time_base_client.hpp>
#include <nexigen/chrono/time_base_host_domain.hpp>

namespace nexigen::chrono {
namespace {

/// @brief Convert a duration to nanoseconds without a negative result.
///
/// @param value Duration to read.
/// @return Non-negative nanosecond count.
[[nodiscard]] std::uint64_t non_negative(const std::chrono::nanoseconds& value) noexcept {
    return value.count() > 0 ? static_cast<std::uint64_t>(value.count()) : 0U;
}

} // namespace

TimeBaseClient::TimeBaseClient(detail::TimeBaseSegment segment,
                               const TimeBaseClientConfig& config) noexcept
    : segment_(std::move(segment)), reader_(*segment_.block()), config_(config) {}

core::Result<TimeBaseClient> TimeBaseClient::attach(std::string_view segment_name,
                                                    const TimeBaseClientConfig& config,
                                                    core::DiagnosticBinding binding) noexcept {
    const auto fail = [&binding](core::ErrorCode code) -> core::Result<TimeBaseClient> {
        if (binding.on_event != nullptr) {
            const core::DiagnosticEvent event{core::DiagnosticSource::Chrono,
                                              core::DiagnosticKind::Degraded, code};
            binding.on_event(binding.self, event);
        }
        return std::unexpected(code);
    };

    auto attached = detail::TimeBaseSegment::attach(segment_name);
    if (!attached) {
        return fail(attached.error());
    }
    TimeBaseClient client{std::move(*attached), config};

    TimeBaseBlock block{};
    if (!client.reader_.read(block)) {
        return fail(core::ErrorCode::CorruptData);
    }
    // A base that never reached Ready, or that already reported Lost, is not
    // usable: fail attach instead of letting the application start on it.
    if (block.status == TimeBaseStatus::NotReady) {
        return fail(core::ErrorCode::NotInitialized);
    }
    if (block.status == TimeBaseStatus::Lost) {
        return fail(core::ErrorCode::NotFound);
    }
    client.block_ = block;
    client.generation_ = block.generation;
    client.block_valid_ = true;

    if (block.find(k_monotonic_raw_domain_id) == nullptr) {
        return fail(core::ErrorCode::NotFound);
    }
    for (std::uint8_t i = 0; i < config.required_count; ++i) {
        const DomainCoefficient* const row = block.find(config.required_domains[i]);
        if (row == nullptr) {
            return fail(core::ErrorCode::NotFound);
        }
        client.epochs_[i] = row->epoch;
    }
    client.state_ = ClientState::Attached;
    client.diag_ = binding;
    return client;
}

void TimeBaseClient::bind_diagnostics(core::DiagnosticBinding binding) noexcept {
    diag_ = binding;
    have_reported_ = false;
    report_state(state_);
}

void TimeBaseClient::report_state(ClientState state) noexcept {
    if (diag_.on_event == nullptr) {
        last_reported_state_ = state;
        return;
    }
    if (have_reported_ && state == last_reported_state_) {
        return;
    }

    core::DiagnosticEvent event{core::DiagnosticSource::Chrono, core::DiagnosticKind::Degraded,
                                core::ErrorCode::SystemError};
    if (state == ClientState::Attached) {
        event.kind = have_reported_ ? core::DiagnosticKind::Recovered
                                    : core::DiagnosticKind::Started;
        event.error = core::ErrorCode::Ok;
    } else if (state == ClientState::Lost) {
        event.kind = core::DiagnosticKind::Degraded;
        event.error = core::ErrorCode::NotFound;
    }
    diag_.on_event(diag_.self, event);
    last_reported_state_ = state;
    have_reported_ = true;
}

bool TimeBaseClient::is_required(std::uint16_t domain_id) const noexcept {
    for (std::uint8_t i = 0; i < config_.required_count; ++i) {
        if (config_.required_domains[i] == domain_id) {
            return true;
        }
    }
    return false;
}

bool TimeBaseClient::refresh() noexcept {
    if (reader_.generation() == generation_ && block_valid_) {
        return true;
    }
    TimeBaseBlock fresh{};
    if (!reader_.read(fresh)) {
        return block_valid_; // keep the last complete copy during a write in flight
    }
    block_ = fresh;
    generation_ = fresh.generation;
    block_valid_ = true;
    epoch_changed_ = false;
    required_missing_ = false;
    for (std::uint8_t i = 0; i < config_.required_count; ++i) {
        const DomainCoefficient* const row = block_.find(config_.required_domains[i]);
        if (row == nullptr) {
            required_missing_ = true;
        } else if (row->epoch != epochs_[i]) {
            epoch_changed_ = true;
        }
    }
    return true;
}

ClientState TimeBaseClient::on_tick(std::uint64_t now_ns) noexcept {
    static_cast<void>(refresh());
    if (!block_valid_ || block_.status == TimeBaseStatus::Lost || epoch_changed_ ||
        required_missing_ || block_.heartbeat_ns == 0U) {
        state_ = ClientState::Lost;
        report_state(state_);
        return state_;
    }
    const std::uint64_t age = now_ns > block_.heartbeat_ns ? now_ns - block_.heartbeat_ns : 0U;
    const std::uint64_t stale_after = non_negative(config_.stale_after);
    const std::uint64_t holdover = non_negative(config_.holdover);
    if (age <= stale_after) {
        state_ = ClientState::Attached;
    } else if (age <= holdover) {
        state_ = ClientState::Holdover;
    } else {
        state_ = ClientState::Lost;
    }
    report_state(state_);
    return state_;
}

std::optional<std::int64_t>
TimeBaseClient::canonical_from_native(std::uint16_t domain_id,
                                      std::uint64_t native) const noexcept {
    const DomainCoefficient* const row = block_.find(domain_id);
    if (row == nullptr || row->lock == LockState::Unsupported || !contains_native(*row, native)) {
        return std::nullopt;
    }
    return nexigen::chrono::canonical_from_native(*row, native);
}

std::optional<std::int64_t>
TimeBaseClient::utc_from_canonical(std::int64_t canonical_ns) const noexcept {
    if (canonical_ns < 0) {
        return std::nullopt;
    }
    const DomainCoefficient* const realtime = block_.find(k_realtime_domain_id);
    if (realtime == nullptr || !realtime->has_reverse() ||
        realtime->lock == LockState::Unsupported ||
        !contains_canonical(*realtime, static_cast<std::uint64_t>(canonical_ns))) {
        return std::nullopt;
    }
    return static_cast<std::int64_t>(native_from_canonical(*realtime, canonical_ns));
}

std::optional<DomainInstant> TimeBaseClient::convert(DomainInstant from,
                                                     std::uint16_t to_domain) const noexcept {
    return convert(from, to_domain, 0U);
}

namespace {

/// @brief Convert a typed instant into the `MonotonicRaw` hub.
///
/// @return The hub value, or `std::nullopt` when the source row is missing,
///         unsupported, from another epoch, or outside its validity window.
[[nodiscard]] std::optional<std::int64_t> hub_from_instant(const TimeBaseBlock& block,
                                                           DomainInstant from) noexcept {
    if (from.domain_id == k_monotonic_raw_domain_id) {
        return static_cast<std::int64_t>(from.value);
    }
    const DomainCoefficient* const row = block.find(from.domain_id);
    if (row == nullptr || row->lock == LockState::Unsupported) {
        return std::nullopt;
    }
    const std::uint64_t epoch = from.epoch == 0U ? row->epoch : from.epoch;
    if (epoch != row->epoch || !contains_native(*row, from.value)) {
        return std::nullopt;
    }
    return nexigen::chrono::canonical_from_native(*row, from.value);
}

/// @brief Convert a hub value into one destination domain.
///
/// @return The typed instant, or `std::nullopt` when the destination row cannot
///         represent the instant or the required epoch does not match.
[[nodiscard]] std::optional<DomainInstant> instant_from_hub(const TimeBaseBlock& block,
                                                            std::int64_t canonical,
                                                            std::uint16_t to_domain,
                                                            std::uint64_t to_epoch) noexcept {
    if (to_domain == k_monotonic_raw_domain_id) {
        return DomainInstant{to_domain, 0U, static_cast<std::uint64_t>(canonical)};
    }
    const DomainCoefficient* const row = block.find(to_domain);
    if (row == nullptr || !row->has_reverse() || row->lock == LockState::Unsupported ||
        !contains_canonical(*row, static_cast<std::uint64_t>(canonical))) {
        return std::nullopt;
    }
    if (to_epoch != 0U && to_epoch != row->epoch) {
        return std::nullopt;
    }
    return DomainInstant{to_domain, row->epoch,
                         nexigen::chrono::native_from_canonical(*row, canonical)};
}

} // namespace

std::optional<DomainInstant> TimeBaseClient::convert(DomainInstant from, std::uint16_t to_domain,
                                                     std::uint64_t to_epoch) const noexcept {
    if (from.domain_id == to_domain) {
        const DomainCoefficient* const row = block_.find(from.domain_id);
        if (row == nullptr || row->lock == LockState::Unsupported ||
            (from.epoch != 0U && from.epoch != row->epoch) ||
            (to_epoch != 0U && to_epoch != row->epoch)) {
            return std::nullopt;
        }
        return DomainInstant{to_domain, row->epoch, from.value};
    }
    const auto canonical = hub_from_instant(block_, from);
    if (!canonical.has_value()) {
        return std::nullopt;
    }
    return instant_from_hub(block_, *canonical, to_domain, to_epoch);
}

std::optional<TimeSnapshot> TimeBaseClient::last_snapshot() const noexcept {
    if (!block_valid_) {
        return std::nullopt;
    }
    return block_.last_snapshot;
}

std::string TimeBaseClient::to_string() const {
    return std::format(
        "TimeBaseClient(state={}, status={}, generation={}, required={}, domains={})",
        nexigen::chrono::to_string(state_), nexigen::chrono::to_string(block_.status), generation_,
        config_.required_count, block_.domain_count);
}

std::string to_string(const TimeBaseClient& client) {
    return client.to_string();
}

} // namespace nexigen::chrono
