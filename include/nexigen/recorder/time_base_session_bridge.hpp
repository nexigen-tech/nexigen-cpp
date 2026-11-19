// nexigen::recorder — bridge from a published time-base block to a session.
//
// The daemon owns the block; the recorder owns the session. This adapter is the
// production caller that turns each published generation into one
// `TimeBasePayload` record and each new 1 Hz `TimeSnapshot` into a snapshot
// record, so an exported session carries the complete time-base history.
#pragma once

#include <array>
#include <cstdint>
#include <string>
#include <string_view>

#include <nexigen/chrono/time_base_block.hpp>

namespace nexigen::recorder {

class SessionWriter;

/// @brief Records time-base generations and snapshots into one session.
///
/// One instance is bound to one `SessionWriter`; it allocates nothing while
/// recording (the boot UUID is copied once at construction).
///
/// @note Spec `recorder/raw-storage`, requirements "Time-base history
///       persistence" and "TimeSnapshot record"; design D-G.
class TimeBaseSessionBridge {
public:
    /// @brief Bind the bridge to a session writer.
    ///
    /// @param writer Destination session; it must outlive the bridge.
    /// @param boot_uuid Full kernel boot UUID written into each payload.
    /// @param boot_token 64-bit boot token of the recorded daemon lifetime.
    TimeBaseSessionBridge(SessionWriter& writer, std::string_view boot_uuid,
                          std::uint64_t boot_token) noexcept;

    /// @brief Copying is deleted: the bridge refers to one writer.
    TimeBaseSessionBridge(const TimeBaseSessionBridge&) = delete;
    /// @brief Copy assignment is deleted.
    TimeBaseSessionBridge& operator=(const TimeBaseSessionBridge&) = delete;

    /// @brief Record one published block.
    ///
    /// A generation is recorded once; a `Lost` block is recorded as an explicit
    /// gap with no rows. The embedded 1 Hz snapshot is appended when its
    /// generation moved since the last recorded snapshot.
    ///
    /// @param block Block read from the published segment.
    /// @return `true` when nothing had to be written or both records were
    ///         accepted; `false` when the writer failed.
    [[nodiscard]] bool record(const chrono::TimeBaseBlock& block) noexcept;

    /// @brief Number of `TimeBase` records appended.
    [[nodiscard]] std::uint64_t recorded_entries() const noexcept { return entries_; }

    /// @brief Number of `TimeSnapshot` records appended.
    [[nodiscard]] std::uint64_t recorded_snapshots() const noexcept { return snapshots_; }

private:
    SessionWriter* writer_{nullptr};            ///< Borrowed session writer.
    std::array<char, 37> boot_uuid_{};          ///< Full boot UUID.
    std::uint64_t boot_token_{0};               ///< Daemon boot token.
    std::uint64_t last_generation_{0};          ///< Last recorded generation.
    std::uint64_t last_snapshot_generation_{0}; ///< Last recorded snapshot generation.
    std::uint64_t entries_{0};                  ///< Recorded `TimeBase` records.
    std::uint64_t snapshots_{0};                ///< Recorded snapshot records.
};

} // namespace nexigen::recorder
