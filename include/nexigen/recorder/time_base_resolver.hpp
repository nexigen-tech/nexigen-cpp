/// @file time_base_resolver.hpp
/// @brief Post-hoc any-domain resolution over a recorded time-base series
/// (change add-chrono-service-contract; spec `recorder/raw-storage`,
/// requirement "Time-base history persistence"; design D-G and D-I).
///
/// The resolver is a cold-path analysis helper: it is fed every recorded
/// `TimeBasePayload` and `chrono::TimeSnapshot` and answers resolution questions
/// without consulting the local clock or the live daemon. Every conversion goes
/// through the `TimeDomain::MonotonicRaw` hub: a native instant is mapped
/// forward with the `DomainCoefficient` row that covers it, and the hub result
/// is mapped back with the destination row. An instant with no covering entry
/// stays unresolved; neighbouring entries are never substituted, and a `gap`
/// entry is never used as a map.
#pragma once

#include <cstddef>
#include <cstdint>
#include <optional>
#include <vector>

#include <nexigen/chrono/time_snapshot.hpp>
#include <nexigen/recorder/wire_types.hpp>

namespace nexigen::recorder {

/// @brief Append-only resolver over recorded time-base entries and snapshots.
///
/// Entries are kept in recording order. Lookups scan them linearly, which is
/// fine for the change-only history volume an export processes.
///
/// @note Spec `recorder/raw-storage`, requirement "Time-base history
///       persistence"; scenario "Snapshot resolves a stored event".
class TimeBaseResolver {
public:
    /// @brief Add one recorded history entry.
    ///
    /// A payload without the current layout guard is rejected as a legacy
    /// session and reported through `saw_legacy_payload()`.
    ///
    /// @param entry Entry to append; copied.
    /// @return `true` when the entry uses the current contract.
    [[nodiscard]] bool add(const TimeBasePayload& entry);

    /// @brief Add one recorded 1 Hz snapshot.
    ///
    /// Snapshots supply the per-domain values and the generation that selects
    /// the history row active at that instant.
    ///
    /// @param snapshot Snapshot to append; copied.
    void add_snapshot(const chrono::TimeSnapshot& snapshot);

    /// @brief Drop every entry and snapshot.
    void clear() noexcept;

    /// @brief Whether any entry or snapshot was recorded.
    /// @return `true` when the series is non-empty.
    [[nodiscard]] bool empty() const noexcept { return entries_.empty() && snapshots_.empty(); }

    /// @brief Number of recorded history entries.
    /// @return Entry count.
    [[nodiscard]] std::size_t size() const noexcept { return entries_.size(); }

    /// @brief Number of recorded snapshots.
    /// @return Snapshot count.
    [[nodiscard]] std::size_t snapshot_count() const noexcept { return snapshots_.size(); }

    /// @brief Whether a payload with the retired layout was observed.
    /// @return `true` when `add()` rejected a legacy entry.
    /// @note Old sessions are not supported; the exporter reports them instead
    ///       of reinterpreting bytes.
    [[nodiscard]] bool saw_legacy_payload() const noexcept { return saw_legacy_; }

    /// @brief Boot token of the series (first non-gap entry).
    /// @return Token, or `std::nullopt` when nothing was recorded.
    [[nodiscard]] std::optional<std::uint64_t> boot_token() const noexcept;

    /// @brief Time-base epoch of the series (first non-gap entry).
    /// @return Epoch, or `std::nullopt` when nothing was recorded.
    [[nodiscard]] std::optional<std::uint64_t> time_base_epoch() const noexcept;

    /// @brief Resolve a native domain instant to hub nanoseconds.
    ///
    /// Finds the newest history entry whose row matches `domain_id`/`epoch` and
    /// whose native validity interval contains `native`. When no row covers the
    /// value, a snapshot plus the row active at the snapshot instant is used as
    /// the anchor. `TimeDomain::MonotonicRaw` is the hub and resolves to itself.
    ///
    /// @param domain_id Domain id of the native value.
    /// @param epoch Domain epoch the value belongs to; zero accepts any epoch.
    /// @param native Native counter value.
    /// @return Hub nanoseconds, or `std::nullopt` when unresolved.
    [[nodiscard]] std::optional<std::int64_t>
    canonical_from_native(std::uint16_t domain_id, std::uint64_t epoch,
                          std::uint64_t native) const noexcept;

    /// @brief Resolve a hub instant to a native domain value.
    ///
    /// Finds the newest history entry whose row matches `domain_id`/`epoch`,
    /// carries a reverse map, and whose hub validity interval contains
    /// `canonical`.
    ///
    /// @param domain_id Destination domain id.
    /// @param epoch Destination epoch; zero accepts any epoch.
    /// @param canonical Hub `CLOCK_MONOTONIC_RAW` nanoseconds.
    /// @return Native value, or `std::nullopt` when unresolved.
    [[nodiscard]] std::optional<std::uint64_t>
    native_from_canonical(std::uint16_t domain_id, std::uint64_t epoch,
                          std::int64_t canonical) const noexcept;

    /// @brief Convert any recorded domain instant into another domain.
    ///
    /// Maps through `TimeDomain::MonotonicRaw`: a forward row for the source and
    /// a reverse row for the destination. Either hop may fail; then the whole
    /// conversion is unresolved.
    ///
    /// @param from_domain Source domain id.
    /// @param from_epoch Source epoch; zero accepts any epoch.
    /// @param value Source native value.
    /// @param to_domain Destination domain id.
    /// @return Destination native value, or `std::nullopt` when unresolved.
    [[nodiscard]] std::optional<std::uint64_t> convert(std::uint16_t from_domain,
                                                       std::uint64_t from_epoch,
                                                       std::uint64_t value,
                                                       std::uint16_t to_domain) const noexcept;

    /// @brief Resolve a hub instant to UTC through the `Realtime` row.
    ///
    /// @param canonical_ns Hub `CLOCK_MONOTONIC_RAW` nanoseconds.
    /// @return UTC epoch nanoseconds, or `std::nullopt` when no `Realtime` row
    ///         covers the instant.
    [[nodiscard]] std::optional<std::int64_t>
    utc_from_canonical(std::uint64_t canonical_ns) const noexcept;

    /// @brief Resolve a hub instant to TAI through the `Tai` row.
    ///
    /// @param canonical_ns Hub `CLOCK_MONOTONIC_RAW` nanoseconds.
    /// @return TAI epoch nanoseconds, or `std::nullopt` when no `Tai` row covers
    ///         the instant.
    [[nodiscard]] std::optional<std::int64_t>
    tai_from_canonical(std::uint64_t canonical_ns) const noexcept;

private:
    std::vector<TimeBasePayload> entries_{};        ///< Recorded history series.
    std::vector<chrono::TimeSnapshot> snapshots_{}; ///< Recorded 1 Hz snapshots.
    bool saw_legacy_{false};                        ///< A retired payload was seen.
};

} // namespace nexigen::recorder
