// nexigen::chrono — the published time-base block.
//
// One fixed-size value, written by exactly one daemon through a seqlock and
// read by any process. The block carries the versioned header, aggregate
// status, sync mode, service flags, the domain table, and the last 1 Hz
// snapshot.
#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <string>

#include <nlohmann/json.hpp>

#include <nexigen/chrono/domain_coefficient.hpp>
#include <nexigen/chrono/service_flags.hpp>
#include <nexigen/chrono/sync_mode.hpp>
#include <nexigen/chrono/time_base_status.hpp>
#include <nexigen/chrono/time_domain.hpp>
#include <nexigen/chrono/time_snapshot.hpp>

namespace nexigen::chrono {

/// @brief Magic word identifying a time-base block (`"NXTB"` little-endian).
inline constexpr std::uint64_t time_base_magic = 0x4E58'5442ULL;
/// @brief Layout version; a different value is rejected.
inline constexpr std::uint32_t time_base_version = 2U;
/// @brief Maximum number of domain rows a block can publish.
inline constexpr std::size_t time_base_max_domains = 32U;
/// @brief Exact byte size of a `TimeBaseBlock`.
inline constexpr std::size_t time_base_block_bytes = 5120U;

/// @brief The fixed-size published time-base value.
struct alignas(64) TimeBaseBlock {
    std::uint64_t magic{time_base_magic};     ///< `time_base_magic`.
    std::uint32_t version{time_base_version}; ///< `time_base_version`.
    std::uint32_t capacity{static_cast<std::uint32_t>(time_base_max_domains)}; ///< Row capacity.
    TimeBaseStatus status{TimeBaseStatus::NotReady}; ///< Aggregate health.
    SyncMode sync_mode{SyncMode::Standalone};        ///< Active synchronization mode.
    std::uint16_t domain_count{0};                   ///< Number of valid rows.
    ServiceFlags service_flags{};                    ///< Observed service flags.
    std::uint64_t generation{0};                     ///< Seqlock word; even when stable.
    std::uint64_t boot_token{0};                     ///< 64-bit prefix of the kernel boot id.
    std::int64_t tai_offset_ns{0};                   ///< TAI - UTC offset.
    std::int64_t utc_offset_ns{0};                   ///< Hub - UTC offset.
    std::uint64_t utc_unc_ns{0};                     ///< UTC offset uncertainty.
    alignas(64) std::uint64_t heartbeat_ns{0};       ///< Hub instant of the last publish.
    std::uint8_t reserved1[56]{};                    ///< Reserved; zero in this version.
    std::array<DomainCoefficient, time_base_max_domains> rows{}; ///< Domain table.
    TimeSnapshot last_snapshot{};                                ///< Last 1 Hz service snapshot.

    /// @brief Find a row by domain id.
    /// @param domain_id Domain to look up.
    /// @return Pointer to the row, or `nullptr`.
    [[nodiscard]] const DomainCoefficient* find(std::uint16_t domain_id) const noexcept {
        const std::uint32_t count = domain_count < capacity
                                        ? domain_count
                                        : static_cast<std::uint32_t>(time_base_max_domains);
        for (std::uint32_t i = 0; i < count; ++i) {
            if (rows[i].domain_id == domain_id) {
                return &rows[i];
            }
        }
        return nullptr;
    }

    /// @brief Find a well-known domain row.
    /// @param domain Well-known domain.
    /// @return Pointer to the row, or `nullptr`.
    [[nodiscard]] const DomainCoefficient* find(TimeDomain domain) const noexcept {
        return find(static_cast<std::uint16_t>(domain));
    }

    /// @brief Render this block as a diagnostic string.
    [[nodiscard]] std::string to_string() const;
};

static_assert(offsetof(TimeBaseBlock, generation) == 24U, "generation offset is a wire constant");
static_assert(offsetof(TimeBaseBlock, heartbeat_ns) == 64U, "heartbeat owns its cache line");
static_assert(offsetof(TimeBaseBlock, rows) == 128U, "rows start after the header");
static_assert(offsetof(TimeBaseBlock, last_snapshot) == 4224U, "snapshot follows the rows");
static_assert(sizeof(TimeBaseBlock) == time_base_block_bytes, "block size is a wire constant");
static_assert(alignof(TimeBaseBlock) == 64U, "block alignment is a wire constant");

/// @brief Free-helper mirror of `TimeBaseBlock::to_string`.
[[nodiscard]] std::string to_string(const TimeBaseBlock& block);

/// @brief Serialize a block as JSON (cold path only).
void to_json(nlohmann::json& j, const TimeBaseBlock& block);

/// @brief Parse a block from JSON, non-throwing.
void from_json(const nlohmann::json& j, TimeBaseBlock& block) noexcept;

} // namespace nexigen::chrono
