// nexigen::chrono — one atomic 1 Hz snapshot of the time service.
//
// The snapshot lists every measured domain value and its lock state without
// duplicating the coefficient table; `generation` selects the history rows
// valid at the same instant.
#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <format>
#include <string>
#include <string_view>

#include <nlohmann/json.hpp>

#include <nexigen/chrono/lock_state.hpp>
#include <nexigen/chrono/service_flags.hpp>
#include <nexigen/chrono/sync_mode.hpp>
#include <nexigen/chrono/time_base_status.hpp>
#include <nexigen/chrono/time_domain_class.hpp>

namespace nexigen::chrono {

/// @brief One measured domain value inside a snapshot.
struct SnapshotDomainValue {
    std::uint16_t domain_id{0};                                 ///< Domain identifier.
    TimeDomainClass domain_class{TimeDomainClass::SystemClock}; ///< Domain role.
    LockState lock{LockState::Unsupported};                     ///< Lock state at the sample.
    std::uint16_t flags{0};                                     ///< Domain flags.
    std::uint16_t reserved0{0};                                 ///< Reserved; zero in this version.
    std::uint64_t epoch{0};                                     ///< Domain epoch.
    std::uint64_t value{0}; ///< Native value at the snapshot instant.
};
static_assert(sizeof(SnapshotDomainValue) == 24U, "snapshot domain value is 24 bytes");

/// @brief 1 Hz atomic service snapshot.
struct alignas(64) TimeSnapshot {
    /// @brief Maximum number of domains in a snapshot.
    static constexpr std::size_t max_domains = 32U;

    std::uint64_t generation{0};                     ///< Map generation the snapshot belongs to.
    std::uint64_t boot_token{0};                     ///< Kernel boot token.
    TimeBaseStatus status{TimeBaseStatus::NotReady}; ///< Aggregate status.
    SyncMode sync_mode{SyncMode::Standalone};        ///< Active sync mode.
    std::uint16_t reserved0{0};                      ///< Reserved; zero in this version.
    ServiceFlags service_flags{};                    ///< Observed service flags.
    std::uint64_t monotonic_raw_ns{0};               ///< CLOCK_MONOTONIC_RAW instant.
    std::uint64_t monotonic_ns{0};                   ///< CLOCK_MONOTONIC instant.
    std::uint64_t tai_ns{0};                         ///< CLOCK_TAI instant.
    std::uint64_t realtime_ns{0};                    ///< CLOCK_REALTIME instant.
    std::int64_t tai_offset_ns{0};                   ///< TAI - UTC offset.
    std::int64_t utc_offset_ns{0};                   ///< Hub - UTC offset.
    std::uint64_t unc_ns{0};                         ///< Offset uncertainty.
    std::uint32_t domain_count{0};                   ///< Valid entries in `domains`.
    std::uint32_t reserved1{0};                      ///< Reserved; zero in this version.
    std::array<SnapshotDomainValue, max_domains> domains{}; ///< Domain values.
    std::uint8_t reserved2[40]{};                           ///< Reserved; zero in this version.

    /// @brief Find a domain value by id.
    /// @param domain_id Domain id.
    /// @return Pointer to the value, or `nullptr`.
    [[nodiscard]] const SnapshotDomainValue* find(std::uint16_t domain_id) const noexcept {
        const std::uint32_t count =
            domain_count < max_domains ? domain_count : static_cast<std::uint32_t>(max_domains);
        for (std::uint32_t i = 0; i < count; ++i) {
            if (domains[i].domain_id == domain_id) {
                return &domains[i];
            }
        }
        return nullptr;
    }

    /// @brief Render the snapshot as a diagnostic string.
    /// @return Text naming the generation, status, mode, flags and every domain.
    /// @note Spec `chrono/time-base`, requirement "1 Hz service snapshot";
    ///       cold path only (allocates).
    [[nodiscard]] std::string to_string() const;
};

static_assert(sizeof(TimeSnapshot) == 896U, "snapshot is a wire constant");
static_assert(alignof(TimeSnapshot) == 64U, "snapshot starts on a cache line");

/// @brief Free helper mirroring `TimeSnapshot::to_string`.
[[nodiscard]] std::string to_string(const TimeSnapshot& snapshot);

/// @brief Serialize a snapshot as JSON (cold path only).
void to_json(nlohmann::json& j, const TimeSnapshot& snapshot);

/// @brief Parse a snapshot from JSON, non-throwing.
void from_json(const nlohmann::json& j, TimeSnapshot& snapshot) noexcept;

} // namespace nexigen::chrono

/// @brief `std::formatter` for `TimeSnapshot`.
template <>
struct std::formatter<nexigen::chrono::TimeSnapshot> : std::formatter<std::string_view> {
    /// @brief Format the snapshot through `to_string()`.
    ///
    /// @tparam Ctx Format context type.
    /// @param value Snapshot to format.
    /// @param ctx Output context supplied by `std::format`.
    /// @return Iterator past the last written character.
    /// @note Mandate 11 (`std::formatter`); spec `chrono/time-base`,
    ///       requirement "1 Hz service snapshot".
    template <class Ctx> auto format(const nexigen::chrono::TimeSnapshot& value, Ctx& ctx) const {
        const std::string text = value.to_string();
        return std::formatter<std::string_view>::format(text, ctx);
    }
};
