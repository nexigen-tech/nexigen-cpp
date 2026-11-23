// nexigen::logging — the fixed quality-of-service shape of the log service
// (change fix-log-subscriber-capacity; spec logging/iceoryx2-logger "Log
// service capacity is a shared contract").
//
// The log publish-subscribe service is one cross-process contract: the first
// participant creates it and iceoryx2 verifies the shape when any later
// participant opens it. These constants are the single source of truth for the
// logger, the daemon, the recorder, and the viewer.
//
// COM-FREE by design: `nexigen_logging` compiles every src/logging/*.cpp,
// including the config JSON unit, so this header may not include nexigen::com.
// The options factory lives in log_service_options.hpp.
#pragma once

#include <cstdint>

namespace nexigen::logging {

/// @brief Maximum log-subscriber ports on the shared service.
///
/// Four covers the daemon, the recorder, the viewer, and one spare. Each unit
/// multiplies the shared-memory data segment every publisher allocates.
inline constexpr std::uint64_t kLogServiceMaxSubscribers = 4U;

/// @brief Maximum log-publisher ports on the shared service.
///
/// One per logging application process. The iceoryx2 default of two rejects a
/// third application's logger outright.
inline constexpr std::uint64_t kLogServiceMaxPublishers = 64U;

/// @brief Maximum nodes that may open the log service in parallel.
///
/// Publishers, subscribers, and tools each count as one node.
inline constexpr std::uint64_t kLogServiceMaxNodes = 64U;

/// @brief Per-subscriber sample depth, in frames.
///
/// 16 384 frames cover about 327 ms at 50 000 msg/s, absorbing scheduler wakes
/// and page faults without hiding sustained lag.
inline constexpr std::uint64_t kLogServiceBufferDepth = 16384U;

/// @brief Publisher history kept for late subscribers, in frames.
///
/// Small on purpose: the viewer does not promise a backlog, but the transport
/// may hand over a few recent frames on attach.
inline constexpr std::uint64_t kLogServiceHistoryDepth = 4U;

/// @brief Whether the transport evicts the oldest unread frame on overflow.
///
/// Enabled for the log service: a slow subscriber loses its oldest frames
/// (visible through the sequence gap) instead of failing the publisher's send.
inline constexpr bool kLogServiceSafeOverflow = true;

} // namespace nexigen::logging
