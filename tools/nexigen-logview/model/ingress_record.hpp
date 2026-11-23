// nexigen-logview — one item in the capture-to-UI hand-off queue.
#pragma once

#include <cstddef>
#include <cstdint>
#include <string>

#include <nexigen/logging/log_frame.hpp>

#include "record_kind.hpp"

namespace nexigen::logview {

/// @brief One frame or transport-loss marker waiting for the UI thread.
///
/// The receive thread is the sole producer and the UI thread the sole consumer,
/// so the fixed hand-off queue holds this plain value without synchronization
/// beyond the queue's own acquire/release pair. The struct is aligned to a
/// cache line and rounds to 320 bytes, so the 65 536-slot queue costs 20 MiB.
struct alignas(64) IngressRecord {
    nexigen::logging::LogFrame frame{}; ///< Received frame, or a synthetic gap marker.
    std::uint64_t lost_before{0};       ///< Frames the transport reported missing first.
    RecordKind kind{RecordKind::Frame}; ///< Frame or transport gap.

    /// @brief Cold-path diagnostic rendering.
    /// @return Text naming the kind and the frame.
    [[nodiscard]] std::string to_string() const;
};

static_assert(sizeof(IngressRecord) == 320, "IngressRecord must round to five cache lines");
static_assert(alignof(IngressRecord) == 64, "IngressRecord must be cache-line aligned");

/// @brief Free diagnostic mirror of `IngressRecord::to_string`.
/// @param record Record to render.
/// @return The same text.
[[nodiscard]] inline std::string to_string(const IngressRecord& record) {
    return record.to_string();
}

} // namespace nexigen::logview
