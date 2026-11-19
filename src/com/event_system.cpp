// nexigen::com — cold-path event-system serialization and value helpers.
#include <nexigen/com/channel_config.hpp>
#include <nexigen/com/event_consumer_stats.hpp>
#include <nexigen/com/event_header.hpp>
#include <nexigen/com/event_options.hpp>
#include <nexigen/com/event_producer_stats.hpp>
#include <nexigen/com/event_time.hpp>
#include <nexigen/com/hardware_metadata.hpp>
#include <nexigen/com/loss_info.hpp>
#include <nexigen/com/publish_subscribe_options.hpp>

#include <format>
#include <string>

#include <nexigen/core/detail/json_field.hpp>

namespace nexigen::com {

std::string PublishSubscribeOptions::to_string() const {
    return std::format("PublishSubscribeOptions(max_subscribers={}, max_publishers={}, "
                       "max_nodes={}, subscriber_max_buffer_size={}, "
                       "subscriber_buffer_size={}, history_size={}, "
                       "subscriber_history_request={}, enable_safe_overflow={})",
                       max_subscribers, max_publishers, max_nodes, subscriber_max_buffer_size,
                       subscriber_buffer_size, history_size, subscriber_history_request,
                       enable_safe_overflow);
}

void to_json(nlohmann::json& j, const PublishSubscribeOptions& options) {
    j = nlohmann::json{{"max_subscribers", options.max_subscribers},
                       {"max_publishers", options.max_publishers},
                       {"max_nodes", options.max_nodes},
                       {"subscriber_max_buffer_size", options.subscriber_max_buffer_size},
                       {"subscriber_buffer_size", options.subscriber_buffer_size},
                       {"history_size", options.history_size},
                       {"subscriber_history_request", options.subscriber_history_request},
                       {"enable_safe_overflow", options.enable_safe_overflow}};
}

void from_json(const nlohmann::json& j, PublishSubscribeOptions& options) noexcept {
    if (!j.is_object()) {
        return;
    }
    core::detail::read_unsigned(j, "max_subscribers", options.max_subscribers);
    core::detail::read_unsigned(j, "max_publishers", options.max_publishers);
    core::detail::read_unsigned(j, "max_nodes", options.max_nodes);
    core::detail::read_unsigned(j, "subscriber_max_buffer_size",
                                options.subscriber_max_buffer_size);
    core::detail::read_unsigned(j, "subscriber_buffer_size", options.subscriber_buffer_size);
    core::detail::read_unsigned(j, "history_size", options.history_size);
    core::detail::read_unsigned(j, "subscriber_history_request",
                                options.subscriber_history_request);
    core::detail::read_bool(j, "enable_safe_overflow", options.enable_safe_overflow);
}

std::string EventOptions::to_string() const {
    return std::format("EventOptions(max_listeners={}, max_notifiers={}, max_nodes={})",
                       max_listeners, max_notifiers, max_nodes);
}

void to_json(nlohmann::json& j, const EventOptions& options) {
    j = nlohmann::json{{"max_listeners", options.max_listeners},
                       {"max_notifiers", options.max_notifiers},
                       {"max_nodes", options.max_nodes}};
}

void from_json(const nlohmann::json& j, EventOptions& options) noexcept {
    if (!j.is_object()) {
        return;
    }
    core::detail::read_unsigned(j, "max_listeners", options.max_listeners);
    core::detail::read_unsigned(j, "max_notifiers", options.max_notifiers);
    core::detail::read_unsigned(j, "max_nodes", options.max_nodes);
}

std::string EventHeader::to_string() const {
    return std::format(
        "EventHeader(sequence={}, timestamp_ns={}, canonical={}, domain={}, domain_epoch={}, "
        "ingest_ns={}, event_code={}, native_ticks={}, hardware_sequence={}, "
        "hardware_event_code={}, flags={})",
        sequence_id, timestamp_ns, has_canonical_projection(), domain_id, domain_epoch,
        ingest_timestamp_ns, event_code, native_ticks, hardware_sequence, hardware_event_code,
        flags);
}

void to_json(nlohmann::json& j, const EventHeader& header) {
    j = nlohmann::json{{"sequence_id", header.sequence_id},
                       {"timestamp_ns", header.timestamp_ns},
                       {"has_canonical_projection", header.has_canonical_projection()},
                       {"domain_id", header.domain_id},
                       {"domain_epoch", header.domain_epoch},
                       {"producer_epoch", header.producer_epoch},
                       {"ingest_timestamp_ns", header.ingest_timestamp_ns},
                       {"hardware_sequence", header.hardware_sequence},
                       {"native_ticks", header.native_ticks},
                       {"event_code", header.event_code},
                       {"hardware_event_code", header.hardware_event_code},
                       {"flags", header.flags},
                       {"has_payload", header.has_payload()},
                       {"has_hardware_sequence", header.has_hardware_sequence()},
                       {"has_native_ticks", header.has_native_ticks()},
                       {"has_hardware_event_code", header.has_hardware_event_code()}};
}

void from_json(const nlohmann::json& j, EventHeader& header) noexcept {
    if (!j.is_object()) {
        return;
    }
    core::detail::read_unsigned(j, "sequence_id", header.sequence_id);
    core::detail::read_unsigned(j, "timestamp_ns", header.timestamp_ns);
    core::detail::read_unsigned(j, "producer_epoch", header.producer_epoch);
    core::detail::read_unsigned(j, "ingest_timestamp_ns", header.ingest_timestamp_ns);
    core::detail::read_unsigned(j, "domain_epoch", header.domain_epoch);
    core::detail::read_unsigned(j, "hardware_sequence", header.hardware_sequence);
    core::detail::read_unsigned(j, "native_ticks", header.native_ticks);
    core::detail::read_unsigned(j, "event_code", header.event_code);
    core::detail::read_unsigned(j, "hardware_event_code", header.hardware_event_code);
    core::detail::read_unsigned(j, "domain_id", header.domain_id);
    core::detail::read_unsigned(j, "flags", header.flags);
    // Legacy diagnostics carried `hardware_timestamp_ns` and a domain name.
    core::detail::read_unsigned(j, "hardware_timestamp_ns", header.native_ticks);
    std::string domain;
    if (core::detail::read_string(j, "timestamp_domain", domain) && domain == "Hardware") {
        header.domain_id = header.domain_id == 0U ? 1U : header.domain_id;
    }
}

Status ChannelConfig::validate() const noexcept {
    if (max_consumers == 0U || max_consumers > max_supported_consumers || queue_size == 0U ||
        history_size > queue_size || default_timestamp_domain == TimestampDomain::Unknown) {
        return std::unexpected(Error::InvalidArgument);
    }
    return {};
}

PublishSubscribeOptions ChannelConfig::publish_subscribe_options() const noexcept {
    PublishSubscribeOptions options{};
    options.max_subscribers = max_consumers;
    options.max_publishers = 1U;
    options.max_nodes = max_consumers + 1U;
    options.subscriber_max_buffer_size = queue_size;
    options.subscriber_buffer_size = queue_size;
    options.history_size = history_size;
    options.subscriber_history_request = history_size;
    options.enable_safe_overflow = true;
    return options;
}

EventOptions ChannelConfig::event_options() const noexcept {
    EventOptions options{};
    options.max_listeners = max_consumers;
    options.max_notifiers = 1U;
    options.max_nodes = max_consumers + 1U;
    return options;
}

std::string ChannelConfig::to_string() const {
    return std::format("ChannelConfig(max_consumers={}, queue_size={}, history_size={}, "
                       "default_timestamp_domain={})",
                       max_consumers, queue_size, history_size,
                       nexigen::com::to_string(default_timestamp_domain));
}

void to_json(nlohmann::json& j, const ChannelConfig& config) {
    j = nlohmann::json{
        {"max_consumers", config.max_consumers},
        {"queue_size", config.queue_size},
        {"history_size", config.history_size},
        {"default_timestamp_domain", nexigen::com::to_string(config.default_timestamp_domain)}};
}

void from_json(const nlohmann::json& j, ChannelConfig& config) noexcept {
    if (!j.is_object()) {
        return;
    }
    core::detail::read_unsigned(j, "max_consumers", config.max_consumers);
    core::detail::read_unsigned(j, "queue_size", config.queue_size);
    core::detail::read_unsigned(j, "history_size", config.history_size);
    std::string domain;
    if (core::detail::read_string(j, "default_timestamp_domain", domain)) {
        TimestampDomain parsed{};
        if (from_string(domain, parsed)) {
            config.default_timestamp_domain = parsed;
        }
    }
}

std::string EventTime::to_string() const {
    return std::format("EventTime(ns={}, domain={})", ns, nexigen::com::to_string(domain));
}

void to_json(nlohmann::json& j, const EventTime& time) {
    j = nlohmann::json{{"ns", time.ns}, {"domain", nexigen::com::to_string(time.domain)}};
}

void from_json(const nlohmann::json& j, EventTime& time) noexcept {
    if (!j.is_object()) {
        return;
    }
    core::detail::read_unsigned(j, "ns", time.ns);
    std::string domain;
    if (core::detail::read_string(j, "domain", domain)) {
        TimestampDomain parsed{};
        if (from_string(domain, parsed)) {
            time.domain = parsed;
        }
    }
}

std::string HardwareMetadata::to_string() const {
    return std::format("HardwareMetadata(sequence={}, native_ticks={}, domain={}, epoch={}, "
                       "canonical_ns={}, event_code={}, has_sequence={}, has_ticks={}, "
                       "has_canonical={}, has_event_code={})",
                       sequence, native_ticks, domain_id, domain_epoch, canonical_ns, event_code,
                       has_sequence, has_ticks, has_canonical, has_event_code);
}

void to_json(nlohmann::json& j, const HardwareMetadata& metadata) {
    j = nlohmann::json{
        {"sequence", metadata.sequence},           {"native_ticks", metadata.native_ticks},
        {"domain_id", metadata.domain_id},         {"domain_epoch", metadata.domain_epoch},
        {"canonical_ns", metadata.canonical_ns},   {"event_code", metadata.event_code},
        {"has_sequence", metadata.has_sequence},   {"has_ticks", metadata.has_ticks},
        {"has_canonical", metadata.has_canonical}, {"has_event_code", metadata.has_event_code}};
}

void from_json(const nlohmann::json& j, HardwareMetadata& metadata) noexcept {
    if (!j.is_object()) {
        return;
    }
    core::detail::read_unsigned(j, "sequence", metadata.sequence);
    core::detail::read_unsigned(j, "native_ticks", metadata.native_ticks);
    core::detail::read_unsigned(j, "domain_id", metadata.domain_id);
    core::detail::read_unsigned(j, "domain_epoch", metadata.domain_epoch);
    core::detail::read_unsigned(j, "canonical_ns", metadata.canonical_ns);
    core::detail::read_unsigned(j, "event_code", metadata.event_code);
    core::detail::read_bool(j, "has_sequence", metadata.has_sequence);
    core::detail::read_bool(j, "has_ticks", metadata.has_ticks);
    core::detail::read_bool(j, "has_canonical", metadata.has_canonical);
    core::detail::read_bool(j, "has_event_code", metadata.has_event_code);
    // Legacy diagnostics: a driver-converted timestamp was the only time field.
    core::detail::read_unsigned(j, "timestamp_ns", metadata.native_ticks);
    bool legacy_has_timestamp = false;
    if (core::detail::read_bool(j, "has_timestamp", legacy_has_timestamp) && legacy_has_timestamp) {
        metadata.has_ticks = metadata.has_ticks || metadata.native_ticks != 0U;
    }
}

std::string LossInfo::to_string() const {
    return std::format("LossInfo(reason={}, expected={}, actual={}, lost={}, old_epoch={}, "
                       "new_epoch={}, timestamp_ns={}, domain={})",
                       nexigen::com::to_string(reason), expected_sequence, actual_sequence,
                       lost_count, old_epoch, new_epoch, actual_timestamp_ns,
                       nexigen::com::to_string(actual_timestamp_domain));
}

void to_json(nlohmann::json& j, const LossInfo& info) {
    j = nlohmann::json{
        {"reason", nexigen::com::to_string(info.reason)},
        {"expected_sequence", info.expected_sequence},
        {"actual_sequence", info.actual_sequence},
        {"lost_count", info.lost_count},
        {"old_epoch", info.old_epoch},
        {"new_epoch", info.new_epoch},
        {"actual_timestamp_ns", info.actual_timestamp_ns},
        {"actual_timestamp_domain", nexigen::com::to_string(info.actual_timestamp_domain)}};
}

void from_json(const nlohmann::json& j, LossInfo& info) noexcept {
    if (!j.is_object()) {
        return;
    }
    core::detail::read_unsigned(j, "expected_sequence", info.expected_sequence);
    core::detail::read_unsigned(j, "actual_sequence", info.actual_sequence);
    core::detail::read_unsigned(j, "lost_count", info.lost_count);
    core::detail::read_unsigned(j, "old_epoch", info.old_epoch);
    core::detail::read_unsigned(j, "new_epoch", info.new_epoch);
    core::detail::read_unsigned(j, "actual_timestamp_ns", info.actual_timestamp_ns);
    std::string reason;
    if (core::detail::read_string(j, "reason", reason)) {
        LossReason parsed{};
        if (from_string(reason, parsed)) {
            info.reason = parsed;
        }
    }
    std::string domain;
    if (core::detail::read_string(j, "actual_timestamp_domain", domain)) {
        TimestampDomain parsed{};
        if (from_string(domain, parsed)) {
            info.actual_timestamp_domain = parsed;
        }
    }
}

std::string EventProducerStats::to_string() const {
    return std::format("EventProducerStats(published={}, send_failures={}, notify_failures={}, "
                       "last_notify_error={}, sequence={}, epoch={})",
                       published, send_failures, notify_failures,
                       nexigen::com::to_string(last_notify_error), current_sequence, current_epoch);
}

void to_json(nlohmann::json& j, const EventProducerStats& stats) {
    j = nlohmann::json{{"published", stats.published},
                       {"send_failures", stats.send_failures},
                       {"notify_failures", stats.notify_failures},
                       {"last_notify_error", nexigen::com::to_string(stats.last_notify_error)},
                       {"current_sequence", stats.current_sequence},
                       {"current_epoch", stats.current_epoch}};
}

void from_json(const nlohmann::json& j, EventProducerStats& stats) noexcept {
    if (!j.is_object()) {
        return;
    }
    core::detail::read_unsigned(j, "published", stats.published);
    core::detail::read_unsigned(j, "send_failures", stats.send_failures);
    core::detail::read_unsigned(j, "notify_failures", stats.notify_failures);
    core::detail::read_unsigned(j, "current_sequence", stats.current_sequence);
    core::detail::read_unsigned(j, "current_epoch", stats.current_epoch);
    std::string error;
    if (core::detail::read_string(j, "last_notify_error", error)) {
        Error parsed{};
        if (from_string(error, parsed)) {
            stats.last_notify_error = parsed;
        }
    }
}

std::string EventConsumerStats::to_string() const {
    return std::format("EventConsumerStats(received={}, transport_lost={}, restarts={}, "
                       "duplicates={}, unexpected_resets={}, first_sequence={}, "
                       "last_sequence={}, history_skipped={}, epoch={}, has_received={})",
                       received, transport_lost, restarts, duplicates, unexpected_resets,
                       first_sequence, last_sequence, history_skipped, current_epoch, has_received);
}

void to_json(nlohmann::json& j, const EventConsumerStats& stats) {
    j = nlohmann::json{{"received", stats.received},
                       {"transport_lost", stats.transport_lost},
                       {"restarts", stats.restarts},
                       {"duplicates", stats.duplicates},
                       {"unexpected_resets", stats.unexpected_resets},
                       {"first_sequence", stats.first_sequence},
                       {"last_sequence", stats.last_sequence},
                       {"history_skipped", stats.history_skipped},
                       {"current_epoch", stats.current_epoch},
                       {"has_received", stats.has_received}};
}

void from_json(const nlohmann::json& j, EventConsumerStats& stats) noexcept {
    if (!j.is_object()) {
        return;
    }
    core::detail::read_unsigned(j, "received", stats.received);
    core::detail::read_unsigned(j, "transport_lost", stats.transport_lost);
    core::detail::read_unsigned(j, "restarts", stats.restarts);
    core::detail::read_unsigned(j, "duplicates", stats.duplicates);
    core::detail::read_unsigned(j, "unexpected_resets", stats.unexpected_resets);
    core::detail::read_unsigned(j, "first_sequence", stats.first_sequence);
    core::detail::read_unsigned(j, "last_sequence", stats.last_sequence);
    core::detail::read_unsigned(j, "history_skipped", stats.history_skipped);
    core::detail::read_unsigned(j, "current_epoch", stats.current_epoch);
    core::detail::read_bool(j, "has_received", stats.has_received);
}

} // namespace nexigen::com
