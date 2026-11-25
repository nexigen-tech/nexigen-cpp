#include "sequence_tracker.hpp"

namespace nexigen::canview {

SequenceObservation SequenceTracker::observe(const nexigen::hw::can::CanMessage& message) noexcept {
    if (!message.frame_flags.has(nexigen::hw::can::CanFrameFlags::has_hardware_sequence)) {
        return {SequenceOutcome::Unavailable, 0U};
    }
    const std::uint64_t sequence = message.hardware_sequence;
    if (reset_pending_) {
        reset_pending_ = false;
        seen_ = true;
        last_ = sequence;
        return {SequenceOutcome::Reset, 0U};
    }
    if (!seen_) {
        seen_ = true;
        last_ = sequence;
        return {SequenceOutcome::First, 0U};
    }
    if (sequence == last_) {
        return {SequenceOutcome::Duplicate, 0U};
    }
    if (sequence < last_) {
        return {SequenceOutcome::Reorder, 0U};
    }
    const std::uint64_t jump = sequence - last_;
    last_ = sequence;
    if (jump == 1U) {
        return {SequenceOutcome::InOrder, 0U};
    }
    const std::uint64_t missing = jump - 1U;
    if (missing > max_gap_) {
        // Do not synthesize a giant gap from a counter jump; restart the
        // baseline and let the caller record a `SequenceReset` warning.
        return {SequenceOutcome::SequenceReset, 0U};
    }
    return {SequenceOutcome::Gap, missing};
}

} // namespace nexigen::canview
