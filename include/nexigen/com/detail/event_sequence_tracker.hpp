// nexigen::com::detail — transport sequence tracker for event consumers.
#pragma once

#include <cstdint>

#include <nexigen/com/event_consumer_stats.hpp>
#include <nexigen/com/event_header.hpp>
#include <nexigen/com/loss_reason.hpp>

namespace nexigen::com::detail {

/// @brief One sequence-tracker decision for the current sample.
struct SequenceObservation {
    bool has_loss{false};                        ///< Whether a loss callback is required.
    LossReason reason{LossReason::TransportGap}; ///< Loss classification.
    std::uint64_t expected_sequence{0};          ///< Expected transport sequence before the sample.
    std::uint64_t lost_count{0};                 ///< Exact lost count for `TransportGap`.
    std::uint64_t old_epoch{0};                  ///< Epoch before the observation.
    std::uint64_t new_epoch{0};                  ///< Epoch after the observation.
    bool deliver{true};                          ///< Whether the sample should be delivered.
};

/// @brief Pure transport-sequence state machine used by `EventConsumer`.
///
/// Keeping the policy in one value object makes gap, duplicate, restart, and
/// unexpected-reset branches testable without a live transport.
///
/// @note Spec `com/event-system`, requirement "Transport loss detection and
///       callbacks".
class EventSequenceTracker {
public:
    /// @brief Observe one header and update counters.
    /// @param header Received event header.
    /// @return The decision for this sample.
    [[nodiscard]] SequenceObservation observe(const EventHeader& header) noexcept {
        SequenceObservation observation{};
        observation.expected_sequence = expected_;
        observation.old_epoch = stats_.current_epoch;
        observation.new_epoch = header.producer_epoch;

        if (!stats_.has_received) {
            stats_.has_received = true;
            stats_.first_sequence = header.sequence_id;
            stats_.history_skipped = header.sequence_id;
            stats_.current_epoch = header.producer_epoch;
            expected_ = header.sequence_id + 1U;
            record_delivery(header);
            return observation;
        }

        if (header.producer_epoch != stats_.current_epoch) {
            observation.has_loss = true;
            observation.reason = LossReason::ProducerRestart;
            ++stats_.restarts;
            stats_.current_epoch = header.producer_epoch;
            expected_ = header.sequence_id + 1U;
            record_delivery(header);
            return observation;
        }

        if (header.sequence_id == expected_) {
            ++expected_;
            record_delivery(header);
            return observation;
        }

        if (header.sequence_id > expected_) {
            observation.has_loss = true;
            observation.reason = LossReason::TransportGap;
            observation.lost_count = header.sequence_id - expected_;
            stats_.transport_lost += observation.lost_count;
            expected_ = header.sequence_id + 1U;
            record_delivery(header);
            return observation;
        }

        if (header.sequence_id == 0U && expected_ > 1U) {
            observation.has_loss = true;
            observation.reason = LossReason::UnexpectedSequenceReset;
            ++stats_.unexpected_resets;
            expected_ = 1U;
            record_delivery(header);
            return observation;
        }

        observation.has_loss = true;
        observation.reason = LossReason::Duplicate;
        observation.deliver = false;
        ++stats_.duplicates;
        return observation;
    }

    /// @brief Current statistics.
    /// @return Counters and state.
    [[nodiscard]] const EventConsumerStats& stats() const noexcept { return stats_; }

private:
    void record_delivery(const EventHeader& header) noexcept {
        ++stats_.received;
        stats_.last_sequence = header.sequence_id;
    }

    EventConsumerStats stats_{};
    std::uint64_t expected_{0};
};

} // namespace nexigen::com::detail
