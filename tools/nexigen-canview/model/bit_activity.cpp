#include "bit_activity.hpp"

#include <algorithm>
#include <format>

namespace nexigen::canview {

void BitActivity::init() noexcept {
    matrix_ = {};
    frames_.clear();
    bytes_ = 8U;
    frame_count_ = 0U;
    span_ns_ = 0U;
    available_ = false;
    low_confidence_ = true;
}

void BitActivity::count_transitions(const nexigen::hw::can::CanMessage& previous,
                                    const nexigen::hw::can::CanMessage& current) noexcept {
    const std::size_t bytes = std::min(
        {bytes_, static_cast<std::size_t>(previous.dlc), static_cast<std::size_t>(current.dlc)});
    for (std::size_t byte = 0U; byte < bytes; ++byte) {
        const auto changed = static_cast<std::uint8_t>(previous.data[byte] ^ current.data[byte]);
        for (std::size_t bit = 0U; bit < 8U; ++bit) {
            if (((changed >> bit) & 1U) != 0U) {
                ++matrix_[byte][bit].transitions;
            }
        }
    }
}

bool BitActivity::matches(const CanRecordView& record, std::uint16_t channel_id,
                          std::uint32_t can_id, bool extended) noexcept {
    if (record.message == nullptr || !record.is_frame()) {
        return false;
    }
    const auto& message = *record.message;
    if (message.channel_id != channel_id || message.can_id != can_id ||
        message.extended != extended) {
        return false;
    }
    return message.kind != nexigen::hw::can::CanMessageKind::Error &&
           message.kind != nexigen::hw::can::CanMessageKind::Overload &&
           message.kind != nexigen::hw::can::CanMessageKind::BusIdle;
}

void BitActivity::collect(const RecordSource& source, std::uint16_t channel_id,
                          std::uint32_t can_id, bool extended) {
    std::uint64_t newest = 0U;
    for (std::uint64_t seq = source.tail(); seq > source.head() && frames_.size() < max_frames;) {
        --seq;
        const auto record = source.find(seq);
        if (!record.has_value() || !matches(*record, channel_id, can_id, extended)) {
            continue;
        }
        const std::uint64_t stamp = record->message->hardware_timestamp_ns;
        if (newest == 0U) {
            newest = stamp;
        } else if (newest > stamp && newest - stamp > window_ns) {
            break;
        }
        frames_.push_back(*record->message);
    }
}

void BitActivity::finalize() noexcept {
    std::reverse(frames_.begin(), frames_.end());
    frame_count_ = static_cast<std::uint32_t>(frames_.size());
    available_ = true;
    low_confidence_ = frame_count_ < 2U;
    bytes_ = std::any_of(frames_.begin(), frames_.end(),
                         [](const auto& message) { return message.dlc > 8U; })
                 ? max_bytes
                 : 8U;
    const std::uint64_t newest = frames_.back().hardware_timestamp_ns;
    span_ns_ = newest >= frames_.front().hardware_timestamp_ns
                   ? newest - frames_.front().hardware_timestamp_ns
                   : 0U;
    for (std::size_t index = 0U; index + 1U < frames_.size(); ++index) {
        count_transitions(frames_[index], frames_[index + 1U]);
    }
    for (std::size_t byte = 0U; byte < bytes_; ++byte) {
        for (std::size_t bit = 0U; bit < 8U; ++bit) {
            matrix_[byte][bit].frames = frame_count_;
            matrix_[byte][bit].frequency =
                span_ns_ == 0U
                    ? 0.0
                    : (static_cast<double>(matrix_[byte][bit].transitions) * 1'000'000'000.0) /
                          static_cast<double>(span_ns_);
        }
    }
}

void BitActivity::build(const RecordSource& source, std::uint16_t channel_id, std::uint32_t can_id,
                        bool extended) {
    init();
    frames_.reserve(std::min<std::size_t>(max_frames, source.size()));
    collect(source, channel_id, can_id, extended);
    if (frames_.empty()) {
        return;
    }
    finalize();
}

const BitActivityCell& BitActivity::at(std::size_t byte, std::size_t bit) const noexcept {
    static const BitActivityCell empty{};
    if (byte >= bytes_ || bit >= 8U) {
        return empty;
    }
    return matrix_[byte][bit];
}

std::string BitActivity::to_string() const {
    return std::format("BitActivity(frames={}, bytes={}, span_ns={}, available={}, "
                       "low_confidence={})",
                       frame_count_, bytes_, span_ns_, available_, low_confidence_);
}

} // namespace nexigen::canview
