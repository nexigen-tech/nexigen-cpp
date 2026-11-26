// nexigen::com::detail — transport sequence tracker for event consumers.
#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>

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
/// A single-publisher channel keeps one sequence/epoch state, so any epoch
/// change is a producer restart. A multi-publisher channel (for example the CAN
/// status service, where the receiver and the transmitter both publish) keeps
/// one state per application code: interleaved publishers do not look like a
/// restart, while a restart of one publisher is still reported against that
/// publisher.
///
/// @note Spec `com/event-system`, requirements "Transport loss detection and
///       callbacks" and "Multi-publisher sequence tracking".
class EventSequenceTracker {
public:
    /// @brief Hard upper bound on independently tracked publishers.
    static constexpr std::size_t max_tracked_publishers = 16U;

    /// @brief Construct a tracker for a channel limit.
    /// @param max_publishers Channel `max_publishers`; values above one enable
    ///        per-application tracking, values at or below one keep the
    ///        single-publisher behaviour.
    explicit EventSequenceTracker(std::uint64_t max_publishers = 1U) noexcept
        : publisher_count_(max_publishers > 1U ? (max_publishers < max_tracked_publishers
                                                      ? static_cast<std::size_t>(max_publishers)
                                                      : max_tracked_publishers)
                                               : 1U) {}

    /// @brief Observe one header and update counters.
    /// @param header Received event header.
    /// @return The decision for this sample.
    [[nodiscard]] SequenceObservation observe(const EventHeader& header) noexcept {
        if (publisher_count_ == 1U) {
            return observe_publisher(header, publishers_[0]);
        }
        for (std::size_t index = 0U; index < publisher_count_; ++index) {
            if (publishers_[index].tracked && same_application(publishers_[index], header)) {
                return observe_publisher(header, publishers_[index]);
            }
        }
        for (std::size_t index = 0U; index < publisher_count_; ++index) {
            if (!publishers_[index].tracked) {
                std::memcpy(publishers_[index].app_id.data(), header.app_id, sizeof(header.app_id));
                return observe_publisher(header, publishers_[index]);
            }
        }
        return observe_publisher(header, overflow_);
    }

    /// @brief Current statistics.
    /// @return Counters and state.
    [[nodiscard]] const EventConsumerStats& stats() const noexcept { return stats_; }

private:
    struct PublisherState {
        std::array<char, sizeof(EventHeader::app_id)> app_id{};
        std::uint64_t epoch{0};
        std::uint64_t expected{0};
        bool tracked{false};
    };

    [[nodiscard]] static bool same_application(const PublisherState& state,
                                               const EventHeader& header) noexcept {
        return std::memcmp(state.app_id.data(), header.app_id, sizeof(header.app_id)) == 0;
    }

    [[nodiscard]] SequenceObservation observe_publisher(const EventHeader& header,
                                                        PublisherState& state) noexcept {
        SequenceObservation observation{};
        observation.expected_sequence = state.expected;
        observation.old_epoch = state.epoch;
        observation.new_epoch = header.producer_epoch;

        if (!state.tracked) {
            state.tracked = true;
            state.epoch = header.producer_epoch;
            state.expected = header.sequence_id + 1U;
            if (!stats_.has_received) {
                stats_.has_received = true;
                stats_.first_sequence = header.sequence_id;
                stats_.history_skipped = header.sequence_id;
            }
            record_delivery(header);
            return observation;
        }

        if (header.producer_epoch != state.epoch) {
            observation.has_loss = true;
            observation.reason = LossReason::ProducerRestart;
            ++stats_.restarts;
            state.epoch = header.producer_epoch;
            state.expected = header.sequence_id + 1U;
            record_delivery(header);
            return observation;
        }

        if (header.sequence_id == state.expected) {
            ++state.expected;
            record_delivery(header);
            return observation;
        }

        if (header.sequence_id > state.expected) {
            observation.has_loss = true;
            observation.reason = LossReason::TransportGap;
            observation.lost_count = header.sequence_id - state.expected;
            stats_.transport_lost += observation.lost_count;
            state.expected = header.sequence_id + 1U;
            record_delivery(header);
            return observation;
        }

        if (header.sequence_id == 0U && state.expected > 1U) {
            observation.has_loss = true;
            observation.reason = LossReason::UnexpectedSequenceReset;
            ++stats_.unexpected_resets;
            state.expected = 1U;
            record_delivery(header);
            return observation;
        }

        observation.has_loss = true;
        observation.reason = LossReason::Duplicate;
        observation.deliver = false;
        ++stats_.duplicates;
        return observation;
    }

    void record_delivery(const EventHeader& header) noexcept {
        ++stats_.received;
        stats_.last_sequence = header.sequence_id;
        stats_.current_epoch = header.producer_epoch;
    }

    std::array<PublisherState, max_tracked_publishers> publishers_{};
    PublisherState overflow_{};
    std::size_t publisher_count_{1U};
    EventConsumerStats stats_{};
};

} // namespace nexigen::com::detail
