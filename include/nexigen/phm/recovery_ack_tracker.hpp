// nexigen::phm — acknowledgement budget tracker (change
// complete-phm-safety-loop; spec phm/recovery "Missing acknowledgement
// escalates to the watchdog").
//
// Tracks the escalations the daemon is still waiting on. One entry per
// outstanding recovery, matched by correlation id: an accepted answer clears
// it, a refusal or an expired budget demands a watchdog reaction, and an
// in-progress answer buys no extra time.
#pragma once

#include <array>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <string_view>

#include <nexigen/phm/recovery_ack.hpp>
#include <nexigen/phm/recovery_outcome.hpp>

namespace nexigen::phm {

/// @brief Budget bookkeeping for outstanding State Manager acknowledgements.
///
/// Fixed capacity, no allocation, no synchronization: the daemon's monitoring
/// thread is the only writer and reader.
///
/// @note Spec `phm/recovery`, requirement "Missing acknowledgement escalates to
///       the watchdog".
class RecoveryAckTracker {
public:
    /// @brief Clock every budget is expressed in.
    using Clock = std::chrono::steady_clock;

    /// @brief Number of escalations that can be outstanding at once.
    static constexpr std::size_t k_capacity = 8;

    /// @brief Longest tracked Function Group name, including its NUL.
    static constexpr std::size_t k_group_length = 32;

    /// @brief Result of applying one acknowledgement.
    struct Decision {
        bool matched{false};           ///< The id belonged to an outstanding recovery.
        bool watchdog_reaction{false}; ///< Stop feeding and fire the watchdog.
    };

    /// @brief Start waiting for one recovery's acknowledgement.
    ///
    /// @param request_id Correlation id carried by the notification.
    /// @param group Function Group that was notified, for diagnostics.
    /// @param deadline Budget expiry.
    /// @return `false` when every slot is taken; the caller must then treat the
    ///         escalation as unacknowledged.
    [[nodiscard]] bool expect(std::uint64_t request_id, std::string_view group,
                              Clock::time_point deadline) noexcept {
        for (Entry& entry : entries_) {
            if (entry.active) {
                continue;
            }
            entry.active = true;
            entry.request_id = request_id;
            entry.deadline = deadline;
            std::snprintf(entry.group, sizeof(entry.group), "%.*s", static_cast<int>(group.size()),
                          group.data());
            return true;
        }
        return false;
    }

    /// @brief Apply one acknowledgement.
    ///
    /// @param ack Acknowledgement received from the State Manager.
    /// @return Whether it matched an outstanding recovery and whether it
    ///         demands a watchdog reaction.
    [[nodiscard]] Decision on_ack(const RecoveryAck& ack) noexcept {
        Decision decision{};
        for (Entry& entry : entries_) {
            if (!entry.active || entry.request_id != ack.request_id) {
                continue;
            }
            decision.matched = true;
            switch (ack.outcome) {
                case RecoveryOutcome::Accepted: entry.active = false; break;
                case RecoveryOutcome::InProgress:
                    // The budget is not extended: the original deadline stands.
                    break;
                case RecoveryOutcome::Rejected:
                    entry.active = false;
                    decision.watchdog_reaction = true;
                    break;
            }
            return decision;
        }
        return decision;
    }

    /// @brief Expire every recovery whose budget elapsed.
    ///
    /// @param now Current monotonic time.
    /// @return `true` when at least one escalation went unacknowledged.
    [[nodiscard]] bool expire(Clock::time_point now) noexcept {
        bool expired = false;
        for (Entry& entry : entries_) {
            if (!entry.active || now < entry.deadline) {
                continue;
            }
            entry.active = false;
            expired = true;
        }
        return expired;
    }

    /// @brief Number of escalations still awaiting an acknowledgement.
    [[nodiscard]] std::size_t pending() const noexcept {
        std::size_t count = 0;
        for (const Entry& entry : entries_) {
            if (entry.active) {
                ++count;
            }
        }
        return count;
    }

private:
    /// @brief One outstanding escalation.
    struct Entry {
        std::uint64_t request_id{0};  ///< Correlation id awaited.
        Clock::time_point deadline{}; ///< Budget expiry.
        char group[k_group_length]{}; ///< Notified group, for diagnostics.
        bool active{false};           ///< Slot in use.
    };

    std::array<Entry, k_capacity> entries_{}; ///< Outstanding escalations.
};

} // namespace nexigen::phm
