// nexigen::logging — the log service's publish-subscribe options
// (change fix-log-subscriber-capacity; spec logging/iceoryx2-logger "Log
// service capacity is a shared contract").
//
// Every log participant — the Iceoryx2Logger publisher, nexigen-logd,
// nexigen-recd, and the realtime viewer — must pass the same options when it
// creates or opens the service. The first participant fixes the shape for the
// lifetime of the service; a mismatch is rejected by the transport.
#pragma once

#include <nexigen/com/publish_subscribe_options.hpp>
#include <nexigen/logging/log_service_capacity.hpp>

namespace nexigen::logging {

/// @brief Build the shared quality-of-service options of the log service.
///
/// The returned options apply the capacity constants: four subscribers, 64
/// publishers, 64 nodes, a 16 384-frame subscriber depth, four frames of
/// publisher history, and safe overflow.
///
/// @return The options to pass to a log publisher or subscriber factory.
/// @note Cold path only: called once during initialization, never per record.
/// @note Spec `logging/iceoryx2-logger`, requirement "Log service capacity is a
///       shared contract".
[[nodiscard]] constexpr com::PublishSubscribeOptions log_service_options() noexcept {
    com::PublishSubscribeOptions options{};
    options.max_subscribers = kLogServiceMaxSubscribers;
    options.max_publishers = kLogServiceMaxPublishers;
    options.max_nodes = kLogServiceMaxNodes;
    options.subscriber_max_buffer_size = kLogServiceBufferDepth;
    options.history_size = kLogServiceHistoryDepth;
    options.enable_safe_overflow = kLogServiceSafeOverflow;
    return options;
}

} // namespace nexigen::logging
