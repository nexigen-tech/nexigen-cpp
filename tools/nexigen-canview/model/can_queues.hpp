// nexigen-canview — fixed SPSC queues between capture and UI.
#pragma once

#include <cstddef>

#include <nexigen/core/spsc_ring_buffer.hpp>

#include "can_ingress_record.hpp"

namespace nexigen::canview {

/// @brief Frames and loss markers: 65 536 x 128 B = 8 MiB.
using IngressQueue = nexigen::core::SpscRingBuffer<CanIngressRecord, 65536>;

/// @brief Channel-status samples: 1 024 slots.
using StatusQueue = nexigen::core::SpscRingBuffer<StatusEvent, 1024>;

/// @brief UTC anchor updates: 256 slots.
using AnchorQueue = nexigen::core::SpscRingBuffer<AnchorEvent, 256>;

} // namespace nexigen::canview
