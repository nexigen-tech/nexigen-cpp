// nexigen::recorder — post-hoc any-domain time-base resolution (change
// add-chrono-service-contract; design D-G and D-I).
#include <cstdint>
#include <limits>
#include <optional>

#include <nexigen/chrono/detail/domain_map.hpp>
#include <nexigen/chrono/domain_coefficient.hpp>
#include <nexigen/chrono/time_domain.hpp>
#include <nexigen/recorder/time_base_resolver.hpp>

namespace nexigen::recorder {

namespace {

/// @brief The hub row resolves to itself.
constexpr std::uint16_t hub_domain_id =
    static_cast<std::uint16_t>(chrono::TimeDomain::MonotonicRaw);

/// @brief Whether a row belongs to the requested domain and epoch.
///
/// An epoch of zero means "any epoch", matching `chrono::DomainInstant`.
[[nodiscard]] bool epoch_matches(const chrono::DomainCoefficient& row, std::uint16_t domain_id,
                                 std::uint64_t epoch) noexcept {
    return row.domain_id == domain_id && row.lock != chrono::LockState::Unsupported &&
           (epoch == 0U || row.epoch == epoch);
}

/// @brief Clamp the payload row count to the table capacity.
[[nodiscard]] std::uint32_t row_count(const TimeBasePayload& entry) noexcept {
    const std::uint32_t count = entry.domain_count < kTimeBaseMaxRows
                                    ? entry.domain_count
                                    : static_cast<std::uint32_t>(kTimeBaseMaxRows);
    return count;
}

/// @brief Find the row that covers a native instant inside one entry.
[[nodiscard]] const chrono::DomainCoefficient* find_forward_row(const TimeBasePayload& entry,
                                                                std::uint16_t domain_id,
                                                                std::uint64_t epoch,
                                                                std::uint64_t native) noexcept {
    const std::uint32_t count = row_count(entry);
    for (std::uint32_t i = 0; i < count; ++i) {
        const chrono::DomainCoefficient& row = entry.rows[i];
        if (epoch_matches(row, domain_id, epoch) && chrono::contains_native(row, native)) {
            return &row;
        }
    }
    return nullptr;
}

/// @brief Find the row active at a snapshot instant inside one entry.
[[nodiscard]] const chrono::DomainCoefficient*
find_snapshot_row(const TimeBasePayload& entry, const chrono::TimeSnapshot& snapshot,
                  std::uint16_t domain_id, std::uint64_t epoch) noexcept {
    const std::uint32_t count = row_count(entry);
    for (std::uint32_t i = 0; i < count; ++i) {
        const chrono::DomainCoefficient& row = entry.rows[i];
        if (!epoch_matches(row, domain_id, epoch) || row.scale_fwd_q32 == 0U ||
            !chrono::contains_canonical(row, snapshot.monotonic_raw_ns)) {
            continue;
        }
        return &row;
    }
    return nullptr;
}

/// @brief Anchor one snapshot and its generation-selected row to a native value.
[[nodiscard]] std::optional<std::int64_t>
snapshot_forward(const std::vector<TimeBasePayload>& entries, const chrono::TimeSnapshot& snapshot,
                 std::uint16_t domain_id, std::uint64_t epoch, std::uint64_t native) noexcept {
    const chrono::SnapshotDomainValue* value = snapshot.find(domain_id);
    if (value == nullptr || value->lock == chrono::LockState::Unsupported ||
        (epoch != 0U && value->epoch != epoch)) {
        return std::nullopt;
    }
    for (auto it = entries.rbegin(); it != entries.rend(); ++it) {
        if (it->gap != 0U || it->boot_token != snapshot.boot_token ||
            it->generation > snapshot.generation) {
            continue;
        }
        const chrono::DomainCoefficient* row = find_snapshot_row(*it, snapshot, domain_id, epoch);
        if (row == nullptr) {
            continue;
        }
        return chrono::detail::map_forward(native, value->value,
                                           static_cast<std::int64_t>(snapshot.monotonic_raw_ns),
                                           row->scale_fwd_q32);
    }
    return std::nullopt;
}

} // namespace

bool TimeBaseResolver::add(const TimeBasePayload& entry) {
    if (!entry.has_current_layout()) {
        saw_legacy_ = true;
        return false;
    }
    entries_.push_back(entry);
    return true;
}

void TimeBaseResolver::add_snapshot(const chrono::TimeSnapshot& snapshot) {
    snapshots_.push_back(snapshot);
}

void TimeBaseResolver::clear() noexcept {
    entries_.clear();
    snapshots_.clear();
    saw_legacy_ = false;
}

std::optional<std::uint64_t> TimeBaseResolver::boot_token() const noexcept {
    for (const TimeBasePayload& entry : entries_) {
        if (entry.gap == 0U) {
            return entry.boot_token;
        }
    }
    return std::nullopt;
}

std::optional<std::uint64_t> TimeBaseResolver::time_base_epoch() const noexcept {
    for (const TimeBasePayload& entry : entries_) {
        if (entry.gap == 0U) {
            return entry.time_base_epoch;
        }
    }
    return std::nullopt;
}

std::optional<std::int64_t>
TimeBaseResolver::canonical_from_native(std::uint16_t domain_id, std::uint64_t epoch,
                                        std::uint64_t native) const noexcept {
    if (domain_id == hub_domain_id) {
        return native <= static_cast<std::uint64_t>(std::numeric_limits<std::int64_t>::max())
                   ? std::optional<std::int64_t>{static_cast<std::int64_t>(native)}
                   : std::nullopt;
    }
    // Newest matching row wins; earlier entries remain fallbacks for their own
    // validity intervals only. A gap entry is never used as a map.
    for (auto it = entries_.rbegin(); it != entries_.rend(); ++it) {
        if (it->gap != 0U) {
            continue;
        }
        const chrono::DomainCoefficient* row = find_forward_row(*it, domain_id, epoch, native);
        if (row != nullptr) {
            return chrono::canonical_from_native(*row, native);
        }
    }
    // A recorded snapshot anchors the row active at that generation: the domain
    // value and the hub instant are one measured pair, so the row scale still
    // maps a nearby native value even when no interval covers it.
    for (auto it = snapshots_.rbegin(); it != snapshots_.rend(); ++it) {
        const auto mapped = snapshot_forward(entries_, *it, domain_id, epoch, native);
        if (mapped.has_value()) {
            return mapped;
        }
    }
    return std::nullopt;
}

std::optional<std::uint64_t>
TimeBaseResolver::native_from_canonical(std::uint16_t domain_id, std::uint64_t epoch,
                                        std::int64_t canonical) const noexcept {
    if (domain_id == hub_domain_id) {
        return canonical < 0 ? std::nullopt
                             : std::optional<std::uint64_t>{static_cast<std::uint64_t>(canonical)};
    }
    if (canonical < 0) {
        return std::nullopt;
    }
    const auto hub = static_cast<std::uint64_t>(canonical);
    for (auto it = entries_.rbegin(); it != entries_.rend(); ++it) {
        const TimeBasePayload& entry = *it;
        if (entry.gap != 0U) {
            continue;
        }
        const std::uint32_t count = row_count(entry);
        for (std::uint32_t i = 0; i < count; ++i) {
            const chrono::DomainCoefficient& row = entry.rows[i];
            if (!epoch_matches(row, domain_id, epoch) || !row.has_reverse() ||
                !chrono::contains_canonical(row, hub)) {
                continue;
            }
            return chrono::native_from_canonical(row, canonical);
        }
    }
    return std::nullopt;
}

std::optional<std::uint64_t> TimeBaseResolver::convert(std::uint16_t from_domain,
                                                       std::uint64_t from_epoch,
                                                       std::uint64_t value,
                                                       std::uint16_t to_domain) const noexcept {
    const auto hub = canonical_from_native(from_domain, from_epoch, value);
    if (!hub.has_value()) {
        return std::nullopt;
    }
    if (to_domain == hub_domain_id) {
        return *hub < 0 ? std::nullopt
                        : std::optional<std::uint64_t>{static_cast<std::uint64_t>(*hub)};
    }
    return native_from_canonical(to_domain, 0U, *hub);
}

std::optional<std::int64_t>
TimeBaseResolver::utc_from_canonical(std::uint64_t canonical_ns) const noexcept {
    if (canonical_ns > static_cast<std::uint64_t>(std::numeric_limits<std::int64_t>::max())) {
        return std::nullopt;
    }
    const auto utc = native_from_canonical(chrono::k_realtime_domain_id, 0U,
                                           static_cast<std::int64_t>(canonical_ns));
    if (!utc.has_value()) {
        return std::nullopt;
    }
    return static_cast<std::int64_t>(*utc);
}

std::optional<std::int64_t>
TimeBaseResolver::tai_from_canonical(std::uint64_t canonical_ns) const noexcept {
    if (canonical_ns > static_cast<std::uint64_t>(std::numeric_limits<std::int64_t>::max())) {
        return std::nullopt;
    }
    const auto tai =
        native_from_canonical(chrono::k_tai_domain_id, 0U, static_cast<std::int64_t>(canonical_ns));
    if (!tai.has_value()) {
        return std::nullopt;
    }
    return static_cast<std::int64_t>(*tai);
}

} // namespace nexigen::recorder
