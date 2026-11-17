// nexigen::recorder — fixed-size SPSC shard for producer-to-writer handoff.
#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>

#include <nexigen/core/spsc_ring_buffer.hpp>
#include <nexigen/recorder/recorder_types.hpp>

namespace nexigen::recorder {

/// @brief One fixed-size record handed from a producer thread to the writer.
///
/// The shard path carries records up to `payload.size()` bytes; larger records
/// are appended directly through `SessionWriter::append()`.
struct alignas(64) RecordEnvelope {
    RecordType type{RecordType::ComPayload};
    std::uint32_t channel_id{0};
    std::uint32_t payload_size{0};
    std::uint64_t event_ns{0};
    std::uint64_t sequence{0};
    std::uint64_t publisher_id{0};
    std::array<std::byte, 4096> payload{};
};

/// @brief Capacity of one producer shard.
inline constexpr std::size_t kRecordShardSlots = 1024U;

/// @brief Per-producer SPSC shard; indices are cache-line separated.
///
/// A full shard never blocks the producer: `try_push` reports `BufferFull` and
/// records the dropped sequence. The writer drains the shard and emits one
/// `GapMarker` (`IoBackpressure`) for the dropped range.
class RecordShard {
public:
    /// @brief Push one envelope; a full shard records a backpressure drop.
    /// @param envelope Record to hand over.
    /// @return `Ok`, or `BufferFull` after recording the dropped sequence.
    [[nodiscard]] core::Status try_push(const RecordEnvelope& envelope) noexcept {
        const auto status = ring_.try_push(envelope);
        if (!status.has_value()) {
            if (dropped_count_ == 0U) {
                dropped_channel_ = envelope.channel_id;
                dropped_first_ = envelope.sequence;
            }
            ++dropped_count_;
            return status;
        }
        return status;
    }

    /// @brief Pop the next envelope for the writer.
    /// @param out Receives the envelope when one is available.
    /// @return `Ok`, or `BufferEmpty`.
    [[nodiscard]] core::Status try_pop(RecordEnvelope& out) noexcept {
        return ring_.try_pop(out);
    }

    /// @brief Dropped-range record consumed by the writer.
    struct Dropped {
        std::uint32_t channel_id{0};
        std::uint64_t first_sequence{0};
        std::uint64_t count{0};
    };

    /// @brief Take the accumulated backpressure drop, resetting the counter.
    /// @return Dropped range, or `std::nullopt` when nothing was dropped.
    [[nodiscard]] std::optional<Dropped> take_dropped() noexcept {
        if (dropped_count_ == 0U) {
            return std::nullopt;
        }
        Dropped dropped{dropped_channel_, dropped_first_, dropped_count_};
        dropped_count_ = 0;
        return dropped;
    }

    /// @brief Capacity in envelopes.
    [[nodiscard]] static constexpr std::size_t capacity() noexcept {
        return kRecordShardSlots;
    }

private:
    core::SpscRingBuffer<RecordEnvelope, kRecordShardSlots> ring_{};
    std::uint32_t dropped_channel_{0};
    std::uint64_t dropped_first_{0};
    std::uint64_t dropped_count_{0};
};

} // namespace nexigen::recorder
