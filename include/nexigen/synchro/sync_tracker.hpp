// nexigen::synchro — logical phase continuity tracker
// (change add-synchro-event-sync; spec synchro/consumer).
//
// The transport already reports gaps, duplicates, epoch changes, and sequence
// resets. This tracker adds the logical layer: it maps every delivered payload
// to an absolute phase position (cycle * phases + phase) and attributes the
// difference to either transport loss or generator overrun (skip).
//
// The tracker is single-threaded by construction: only the consumer's dispatch
// thread calls observe()/note_loss().
#pragma once

#include <cstdint>

#include <nexigen/com/event_header.hpp>
#include <nexigen/com/loss_info.hpp>
#include <nexigen/synchro/sync_payload.hpp>

namespace nexigen::synchro {

/// @brief What one delivered event means for logical continuity.
///
/// @note Spec `synchro/consumer`, requirement "Logical phase continuity".
struct SyncObservation {
    bool first{false};                ///< First event of the consumer lifecycle.
    bool resync{false};               ///< Continuity restarted; do not count a skip.
    bool epoch_changed{false};        ///< Producer epoch differs from the previous event.
    bool phases_changed{false};       ///< Producer phase count differs from the tracked one.
    std::uint64_t absolute_phase{0};  ///< `cycle * phases_total + phase` of this event.
    std::uint64_t generator_skips{0}; ///< Slots the generator skipped before this event.
};

/// @brief Maps payload positions to absolute phase continuity.
///
/// @note Spec `synchro/consumer`, requirement "Logical phase continuity";
///       requirement "Transport loss reconciliation".
class SyncTracker {
public:
    /// @brief Record a transport-level anomaly for the next delivered event.
    ///
    /// `TransportGap` remembers the exact lost count so the next event can
    /// separate transport loss from generator skips. `ProducerRestart` and
    /// `UnexpectedSequenceReset` force a resynchronization; `Duplicate` changes
    /// nothing because duplicated samples are never delivered.
    ///
    /// @param info Loss observation reported by the transport.
    /// @note Spec `synchro/consumer`, requirement "Transport loss reconciliation".
    void note_loss(const com::LossInfo& info) noexcept {
        switch (info.reason) {
            case com::LossReason::TransportGap:            pending_gap_ += info.lost_count; break;
            case com::LossReason::ProducerRestart:         pending_restart_ = true; break;
            case com::LossReason::UnexpectedSequenceReset: pending_reset_ = true; break;
            case com::LossReason::Duplicate:               break;
        }
    }

    /// @brief Classify one delivered event and advance the tracked position.
    ///
    /// @param header Event header carrying the producer epoch.
    /// @param payload Payload carrying the logical position.
    /// @return The classification consumed by the consumer.
    /// @note Spec `synchro/consumer`, requirement "Logical phase continuity".
    [[nodiscard]] SyncObservation observe(const com::EventHeader& header,
                                          const SyncPayload& payload) noexcept {
        SyncObservation observation{};
        observation.absolute_phase =
            payload.cycle_number * payload.phases_total + payload.phase_index;
        observation.epoch_changed = has_position_ && header.producer_epoch != epoch_;
        observation.phases_changed = has_position_ && payload.phases_total != phases_total_;

        if (!has_position_) {
            observation.first = true;
        } else if (pending_restart_ || pending_reset_ || observation.epoch_changed ||
                   observation.phases_changed || payload.cycle_number < last_cycle_ ||
                   observation.absolute_phase < last_absolute_phase_) {
            observation.resync = true;
        } else {
            const std::uint64_t expected = last_absolute_phase_ + 1U + pending_gap_;
            if (observation.absolute_phase > expected) {
                observation.generator_skips = observation.absolute_phase - expected;
            } else if (observation.absolute_phase < expected) {
                // An out-of-order or history-replayed sample: restart rather than
                // report a negative skip.
                observation.resync = true;
            }
        }

        last_absolute_phase_ = observation.absolute_phase;
        last_cycle_ = payload.cycle_number;
        phases_total_ = payload.phases_total;
        epoch_ = header.producer_epoch;
        has_position_ = true;
        pending_gap_ = 0;
        pending_restart_ = false;
        pending_reset_ = false;
        return observation;
    }

    /// @brief Forget every tracked position, as on consumer restart.
    /// @note Spec `synchro/consumer`, requirement "Logical phase continuity".
    void reset() noexcept { *this = SyncTracker{}; }

    /// @brief Whether at least one event has been classified.
    /// @return `true` after the first `observe()`.
    /// @note Spec `synchro/consumer`, requirement "Logical phase continuity".
    [[nodiscard]] bool has_position() const noexcept { return has_position_; }
    /// @brief Last classified absolute phase position.
    /// @return Absolute slot index, zero before the first event.
    /// @note Spec `synchro/consumer`, requirement "Logical phase continuity".
    [[nodiscard]] std::uint64_t absolute_phase() const noexcept { return last_absolute_phase_; }
    /// @brief Producer phase count of the last event.
    /// @return Phases per cycle, zero before the first event.
    /// @note Spec `synchro/consumer`, requirement "Logical phase continuity".
    [[nodiscard]] std::uint32_t phases_total() const noexcept { return phases_total_; }
    /// @brief Producer epoch of the last event.
    /// @return Epoch value, zero before the first event.
    /// @note Spec `synchro/consumer`, requirement "Logical phase continuity".
    [[nodiscard]] std::uint64_t epoch() const noexcept { return epoch_; }

private:
    std::uint64_t last_absolute_phase_{0}; ///< Previous logical position.
    std::uint64_t last_cycle_{0};          ///< Previous cycle number.
    std::uint64_t epoch_{0};               ///< Previous producer epoch.
    std::uint64_t pending_gap_{0};         ///< Exact transport loss awaiting reconciliation.
    std::uint32_t phases_total_{0};        ///< Previous phases per cycle.
    bool has_position_{false};             ///< A first event has been classified.
    bool pending_restart_{false};          ///< Next event follows a producer restart.
    bool pending_reset_{false};            ///< Next event follows an unexpected reset.
};

} // namespace nexigen::synchro
