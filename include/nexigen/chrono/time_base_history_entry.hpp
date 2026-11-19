// nexigen::chrono — one persisted time-base history entry (change
// add-time-base-service; spec chrono/time-base "Coefficient history").
//
// History is append-only: every published change of a domain row becomes one
// entry, and an interval with no entry is a gap. An entry carries the block
// generation it came from, the aggregate status at that moment, the row itself,
// and the full boot UUID (the shared block only keeps a 64-bit token, which is
// enough to reject stale readers but not to identify a boot in a copied file).
//
// This is a cold-path value: it is written to the history stream and to the
// recorder, never read on a producer path.
#pragma once

#include <array>
#include <cstdint>
#include <string>
#include <string_view>

#include <nlohmann/json.hpp>

#include <nexigen/chrono/domain_coefficient.hpp>
#include <nexigen/chrono/service_flags.hpp>
#include <nexigen/chrono/sync_mode.hpp>
#include <nexigen/chrono/time_base_status.hpp>

namespace nexigen::chrono {

/// @brief One appended change of the time base.
///
/// `boot_uuid` is the canonical 36-character kernel boot id; it is fixed-size so
/// the entry stays trivially copyable for the recorder payload.
///
/// @note Spec `chrono/time-base`, requirement "Coefficient history".
struct TimeBaseHistoryEntry {
    std::uint64_t generation{0};                     ///< Block generation the entry came from.
    TimeBaseStatus status{TimeBaseStatus::NotReady}; ///< Aggregate status at that moment.
    SyncMode sync_mode{SyncMode::Standalone};        ///< Sync mode at that moment.
    ServiceFlags service_flags{};                    ///< Service flags at that moment.
    DomainCoefficient coefficient{};                 ///< The domain row that changed.
    std::array<char, 37> boot_uuid{};                ///< NUL-terminated kernel boot id.
    bool gap{false}; ///< True when this entry marks an unrecorded interval.

    /// @brief Assign the boot UUID from a string view.
    ///
    /// Copies at most 36 bytes and always terminates; longer input is truncated,
    /// which cannot happen for a kernel boot id.
    ///
    /// @param uuid Boot id text.
    /// @note Cold path only.
    void set_boot_uuid(std::string_view uuid) noexcept;

    /// @brief The stored boot UUID as a view (without the terminator).
    ///
    /// @return View over the stored characters.
    /// @note Cold path only.
    [[nodiscard]] std::string_view boot_uuid_view() const noexcept;

    /// @brief Render this entry as a diagnostic string.
    ///
    /// @return Text naming the generation, status, domain and boot.
    /// @note Spec `chrono/time-base`, requirement "Time-base serialization";
    ///       project mandate 9.
    [[nodiscard]] std::string to_string() const;
};

/// @brief Free-helper mirror of `TimeBaseHistoryEntry::to_string`.
///
/// @param entry Entry to render.
/// @return The same text as the member.
/// @note Project mandate 9.
[[nodiscard]] std::string to_string(const TimeBaseHistoryEntry& entry);

/// @brief Serialize one history entry as JSON (cold path only).
///
/// @param j Destination JSON value.
/// @param entry Entry to serialize.
/// @note Spec `chrono/time-base`, requirement "Time-base serialization";
///       project mandate 10.
void to_json(nlohmann::json& j, const TimeBaseHistoryEntry& entry);

/// @brief Parse one history entry from JSON, strictly non-throwing.
///
/// @param j Source JSON value.
/// @param out Destination entry, updated in place.
/// @note Spec `chrono/time-base`, requirement "Time-base serialization";
///       project mandate 10.
void from_json(const nlohmann::json& j, TimeBaseHistoryEntry& out) noexcept;

static_assert(sizeof(TimeBaseHistoryEntry::boot_uuid) == 37U, "boot UUID is 36 chars plus NUL");

} // namespace nexigen::chrono
