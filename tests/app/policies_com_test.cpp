// nexigen::app policy tests for the COM-gated Iceoryx2Policy.

#include "app_test_support.hpp"

#include <chrono>
#include <format>
#include <optional>
#include <string>
#include <thread>
#include <type_traits>
#include <utility>

#include <nlohmann/json.hpp>

#include <nexigen/app/policies/iceoryx2_policy.hpp>
#include <nexigen/com/node.hpp>
#include <nexigen/com/service_name.hpp>
#include <nexigen/com/subscriber.hpp>
#include <nexigen/com/transport_header.hpp>
#include <nexigen/logging/log_frame.hpp>
#include <nexigen/metrics/metric_batch_snapshot.hpp>
#include <nexigen/metrics/metric_frame.hpp>
#include <nexigen/metrics/metric_record.hpp>
#include <nexigen/tracing/span_frame.hpp>
#include <nexigen/tracing/span_record.hpp>

namespace {

namespace com = nexigen::com;

using Policy = app::policies::Iceoryx2Policy<'P', 'A', 'L', 'T'>;

struct FailingNodeProvider {
    [[nodiscard]] static com::Node* get() noexcept { return nullptr; }
};
using FailingPolicy = app::policies::Iceoryx2Policy<'F', 'A', 'I', 'L', FailingNodeProvider>;

const app::TelemetryIdentity& policy_identity() {
    static const app::TelemetryIdentity identity{
        "policy-com", nexigen::core::AppId::from_literal("CN_LOOP_1_FULL16")};
    return identity;
}

static_assert(app::InfrastructurePolicy<Policy>);
static_assert(app::InfrastructurePolicy<FailingPolicy>);

void test_effective_id() {
    const app::TelemetryIdentity fallback{"name", nexigen::core::AppId{}};
    NX_CHECK(Policy::effective_id(fallback).text() == "PALT");

    const app::TelemetryIdentity runtime{"name", nexigen::core::AppId::from_literal("CN_LOOP_1")};
    NX_CHECK(Policy::effective_id(runtime).text() == "CN_LOOP_1");

    const app::TelemetryIdentity full{"name",
                                      nexigen::core::AppId::from_literal("ABCDEFGHIJKLMNOP")};
    NX_CHECK(Policy::effective_id(full).size() == 16);
}

void test_iceoryx2_diagnostics() {
    Policy policy;
    const std::string text = policy.to_string();
    NX_CHECK(text == "Iceoryx2Policy(app_id=PALT)");
    NX_CHECK(std::format("{}", policy) == text);
    nlohmann::json json = policy;
    NX_CHECK(json.at("type") == "Iceoryx2Policy");
    NX_CHECK(json.at("app_id") == "PALT");
    Policy restored;
    from_json(json, restored);
    NX_CHECK(restored.to_string() == text);

    FailingPolicy failing;
    NX_CHECK(failing.to_string() == "Iceoryx2Policy(app_id=FAIL)");
    NX_CHECK(std::format("{}", failing) == failing.to_string());
    nlohmann::json failing_json = failing;
    NX_CHECK(failing_json.at("app_id") == "FAIL");
    from_json(failing_json, failing);
}

void test_iceoryx2_log_frame() {
    auto node = com::Node::create();
    NX_CHECK(node.has_value());
    if (!node.has_value()) {
        return;
    }
    const auto service = com::ServiceName::create("Nexigen/Log/nexigen");
    NX_CHECK(service.has_value());
    if (!service.has_value()) {
        return;
    }
    auto subscriber =
        com::Subscriber<logging::LogFrame, com::TransportHeader>::create(*node, *service);
    NX_CHECK(subscriber.has_value());
    if (!subscriber.has_value()) {
        return;
    }

    logging::Settings settings{};
    settings.options.initial_log_level = core::LogLevel::Info;
    std::optional<Policy::Logger> logger;
    NX_CHECK(Policy::emplace_logger(logger, settings, policy_identity()).has_value());
    NX_CHECK(logger.has_value());
    if (!logger.has_value()) {
        return;
    }
    for (int i = 0; i < 500 && !logger->writer_started(); ++i) {
        std::this_thread::sleep_for(std::chrono::milliseconds{1});
    }
    NX_CHECK(logger->writer_started());
    NX_CHECK(logger->writer_start_code() == core::ErrorCode::Ok);

    logger->info("com policy {}", 42);
    NX_CHECK(logger->flush().has_value());

    std::optional<com::Sample<logging::LogFrame, com::TransportHeader>> received_frame;
    for (int i = 0; i < 1000 && !received_frame.has_value(); ++i) {
        auto received = subscriber->receive();
        NX_CHECK(received.has_value());
        if (received.has_value() && received->has_value()) {
            received_frame = std::move(*received);
        } else {
            std::this_thread::sleep_for(std::chrono::milliseconds{5});
        }
    }
    NX_CHECK(received_frame.has_value());
    if (received_frame.has_value()) {
        NX_CHECK(received_frame->payload().app_id_text() == "CN_LOOP_1_FULL16");
        NX_CHECK(received_frame->payload().payload_text() == "com policy 42");
        NX_CHECK(received_frame->payload().log_level == core::LogLevel::Info);
    }
}

void test_iceoryx2_node_failure() {
    std::optional<FailingPolicy::MetricsSink> metric_sink;
    const core::Status metric_status =
        FailingPolicy::emplace_metrics_sink(metric_sink, metrics::Settings{}, policy_identity());
    NX_CHECK(!metric_status.has_value());
    NX_CHECK(metric_status.error() == core::ErrorCode::SystemError);
    NX_CHECK(!metric_sink.has_value());

    std::optional<FailingPolicy::TraceSink> trace_sink;
    const core::Status trace_status =
        FailingPolicy::emplace_trace_sink(trace_sink, tracing::Settings{}, policy_identity());
    NX_CHECK(!trace_status.has_value());
    NX_CHECK(trace_status.error() == core::ErrorCode::SystemError);
    NX_CHECK(!trace_sink.has_value());
}

void test_iceoryx2_metric_frame() {
    std::optional<Policy::MetricsSink> sink;
    NX_CHECK(
        Policy::emplace_metrics_sink(sink, metrics::Settings{}, policy_identity()).has_value());
    NX_CHECK(sink.has_value());
    if (!sink.has_value()) return;

    auto node = com::Node::create();
    NX_CHECK(node.has_value());
    if (!node.has_value()) return;
    const auto service = com::ServiceName::create("nexigen/metrics/stream");
    NX_CHECK(service.has_value());
    if (!service.has_value()) return;
    auto subscriber =
        com::Subscriber<metrics::MetricFrame, com::TransportHeader>::create(*node, *service);
    NX_CHECK(subscriber.has_value());
    if (!subscriber.has_value()) return;

    metrics::MetricRecord record{};
    record.kind = metrics::MetricKind::Counter;
    record.scalar.counter = 42;
    metrics::MetricBatchSnapshot batch{};
    batch.records = &record;
    batch.record_count = 1;
    batch.record_capacity = 1;
    NX_CHECK(sink->on_event(batch).has_value());

    std::optional<com::Sample<metrics::MetricFrame, com::TransportHeader>> received_frame;
    for (int i = 0; i < 1000 && !received_frame.has_value(); ++i) {
        auto received = subscriber->receive();
        NX_CHECK(received.has_value());
        if (received.has_value() && received->has_value()) {
            received_frame = std::move(*received);
        } else {
            std::this_thread::sleep_for(std::chrono::milliseconds{5});
        }
    }
    NX_CHECK(received_frame.has_value());
    if (received_frame.has_value()) {
        NX_CHECK(received_frame->payload().app_id_text() == "CN_LOOP_1_FULL16");
        NX_CHECK(received_frame->payload().record.kind == metrics::MetricKind::Counter);
        NX_CHECK(received_frame->payload().record.scalar.counter == 42U);
    }
}

void test_iceoryx2_trace_frame() {
    std::optional<Policy::TraceSink> sink;
    NX_CHECK(Policy::emplace_trace_sink(sink, tracing::Settings{}, policy_identity()).has_value());
    NX_CHECK(sink.has_value());
    if (!sink.has_value()) return;
    NX_CHECK(sink->start().has_value());

    auto node = com::Node::create();
    NX_CHECK(node.has_value());
    if (!node.has_value()) return;
    const auto service = com::ServiceName::create("nexigen/trace/stream");
    NX_CHECK(service.has_value());
    if (!service.has_value()) return;
    auto subscriber =
        com::Subscriber<tracing::SpanFrame, com::TransportHeader>::create(*node, *service);
    NX_CHECK(subscriber.has_value());
    if (!subscriber.has_value()) return;

    tracing::SpanRecord record{};
    record.kind = tracing::SpanKind::Internal;
    record.status = tracing::SpanStatus::Unset;
    record.start = *chrono::Timestamp::now();
    record.end = record.start;
    record.file_name = "policies_com_test.cpp";
    record.function_name = "test_iceoryx2_trace_frame";
    record.line = 1;
    std::snprintf(record.name, sizeof(record.name), "policy span");
    NX_CHECK(sink->on_event(record).has_value());

    std::optional<com::Sample<tracing::SpanFrame, com::TransportHeader>> received_frame;
    for (int i = 0; i < 1000 && !received_frame.has_value(); ++i) {
        auto received = subscriber->receive();
        NX_CHECK(received.has_value());
        if (received.has_value() && received->has_value()) {
            received_frame = std::move(*received);
        } else {
            std::this_thread::sleep_for(std::chrono::milliseconds{5});
        }
    }
    NX_CHECK(received_frame.has_value());
    if (received_frame.has_value()) {
        NX_CHECK(received_frame->payload().app_id_text() == "CN_LOOP_1_FULL16");
        NX_CHECK(received_frame->payload().name_view() == "policy span");
    }
}

} // namespace

int main() {
    run_suite("iceoryx2_policy_effective_id", test_effective_id);
    run_suite("iceoryx2_policy_diagnostics", test_iceoryx2_diagnostics);
    run_suite("iceoryx2_policy_log_frame", test_iceoryx2_log_frame);
    run_suite("iceoryx2_policy_node_failure", test_iceoryx2_node_failure);
    run_suite("iceoryx2_policy_metric_frame", test_iceoryx2_metric_frame);
    run_suite("iceoryx2_policy_trace_frame", test_iceoryx2_trace_frame);
    std::printf("app policies com: %d checks, %d failures\n", g_checks, g_failures);
    return g_failures == 0 ? 0 : 1;
}
