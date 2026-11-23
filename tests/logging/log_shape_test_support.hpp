// Shared test helper: read a live log service's static capacity shape.
//
// Both the logger suite and the daemon suite assert the same contract, so the
// transport query lives here instead of being copied into each test file.
#pragma once

#include <cstdint>
#include <optional>
#include <string>

#include <iox2/iceoryx2.hpp>

#include <nexigen/com/node.hpp>
#include <nexigen/com/service_name.hpp>

namespace nexigen::logging::test_support {

/// The service-level shape visible to any participant.
struct ServiceShape {
    std::uint64_t max_subscribers{0};
    std::uint64_t max_publishers{0};
    std::uint64_t max_nodes{0};
    std::uint64_t buffer_depth{0};
    std::uint64_t history{0};
    bool safe_overflow{false};
};

/// Read the live service's static publish-subscribe configuration.
inline std::optional<ServiceShape> query_service_shape(const std::string& service,
                                                       com::Node& node) {
    const auto name = com::ServiceName::create(service.c_str());
    if (!name) {
        return std::nullopt;
    }
    auto details = iox2::Service<iox2::ServiceType::Ipc>::details(
        name->handle(), node.handle().config(), iox2::MessagingPattern::PublishSubscribe);
    if (!details.has_value() || !details->has_value()) {
        return std::nullopt;
    }
    const auto ps = (*details)->static_details.publish_subscribe();
    return ServiceShape{ps.max_subscribers(), ps.max_publishers(),
                        ps.max_nodes(),       ps.subscriber_max_buffer_size(),
                        ps.history_size(),    ps.has_safe_overflow()};
}

} // namespace nexigen::logging::test_support
