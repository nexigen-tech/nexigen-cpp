// nexigen::app policy tests: ready-made infrastructure policies.
//
// Covers the COM-free policies, their diagnostics, FilePolicy path/capacity
// failures, a JSONL round trip, and NullPolicy's no-output behavior.

#include "app_test_support.hpp"

#include <filesystem>
#include <format>
#include <fstream>
#include <iterator>
#include <string>
#include <system_error>
#include <vector>

#include <nlohmann/json.hpp>

#include <nexigen/app/policies/policies.hpp>
#include <nexigen/metrics/metric_batch_snapshot.hpp>
#include <nexigen/metrics/metric_record.hpp>
#include <nexigen/tracing/span_record.hpp>

namespace fs = std::filesystem;

static_assert(app::InfrastructurePolicy<app::policies::ConsolePolicy>);
static_assert(app::InfrastructurePolicy<app::policies::FilePolicy>);
static_assert(app::InfrastructurePolicy<app::policies::NullPolicy>);

namespace {

const app::TelemetryIdentity& policy_identity() {
    static const app::TelemetryIdentity identity{
        "policy", nexigen::core::AppId::from_literal("POL")};
    return identity;
}

std::string read_file(const fs::path& path) {
    std::ifstream in{path};
    return std::string{std::istreambuf_iterator<char>{in}, std::istreambuf_iterator<char>{}};
}

fs::path find_file(const fs::path& root, std::string_view name) {
    std::error_code error;
    for (const auto& entry : fs::recursive_directory_iterator(root, error)) {
        if (error) {
            break;
        }
        if (entry.is_regular_file() && entry.path().filename() == name) {
            return entry.path();
        }
    }
    return {};
}

void test_policy_diagnostics() {
    {
        app::policies::ConsolePolicy policy;
        NX_CHECK(policy.to_string() == "ConsolePolicy()");
        NX_CHECK(std::format("{}", policy) == policy.to_string());
        nlohmann::json json = policy;
        NX_CHECK(json.at("type") == "ConsolePolicy");
        app::policies::ConsolePolicy restored;
        from_json(json, restored);
    }
    {
        app::policies::FilePolicy policy;
        NX_CHECK(policy.to_string() == "FilePolicy()");
        NX_CHECK(std::format("{}", policy) == policy.to_string());
        nlohmann::json json = policy;
        NX_CHECK(json.at("type") == "FilePolicy");
        app::policies::FilePolicy restored;
        from_json(json, restored);
    }
    {
        app::policies::NullPolicy policy;
        NX_CHECK(policy.to_string() == "NullPolicy()");
        NX_CHECK(std::format("{}", policy) == policy.to_string());
        nlohmann::json json = policy;
        NX_CHECK(json.at("type") == "NullPolicy");
        app::policies::NullPolicy restored;
        from_json(json, restored);
    }
}

void test_file_policy_rejects_invalid_settings() {
    const std::string root = unique_path("app-policy-file-invalid");
    fs::remove_all(root);

    logging::Settings log_settings{};
    log_settings.options.log_root_directory = root;
    log_settings.ring_capacity_set = true;
    log_settings.options.spsc_ring_buffer_capacity = 1U << 20U;
    std::optional<app::policies::FilePolicy::Logger> logger;
    const core::Status capacity =
        app::policies::FilePolicy::emplace_logger(logger, log_settings, policy_identity());
    NX_CHECK(!capacity.has_value());
    NX_CHECK(capacity.error() == core::ErrorCode::InvalidArgument);

    metrics::Settings metrics_settings{};
    std::optional<app::policies::FilePolicy::MetricsSink> metric_sink;
    const core::Status metrics =
        app::policies::FilePolicy::emplace_metrics_sink(metric_sink, metrics_settings, policy_identity());
    NX_CHECK(!metrics.has_value());
    NX_CHECK(metrics.error() == core::ErrorCode::InvalidArgument);

    tracing::Settings tracing_settings{};
    std::optional<app::policies::FilePolicy::TraceSink> trace_sink;
    const core::Status traces =
        app::policies::FilePolicy::emplace_trace_sink(trace_sink, tracing_settings, policy_identity());
    NX_CHECK(!traces.has_value());
    NX_CHECK(traces.error() == core::ErrorCode::InvalidArgument);
}

void test_file_policy_writes_three_streams() {
    const fs::path root = unique_path("app-policy-file");
    std::error_code error;
    fs::remove_all(root, error);
    fs::create_directories(root, error);
    NX_CHECK(!error);

    logging::Settings log_settings{};
    log_settings.options.log_root_directory = root.string();
    std::optional<app::policies::FilePolicy::Logger> logger;
    NX_CHECK(app::policies::FilePolicy::emplace_logger(logger, log_settings, policy_identity()).has_value());
    NX_CHECK(logger.has_value());
    if (!logger.has_value()) {
        return;
    }
    logger->info("policy file record {}", 7);
    NX_CHECK(logger->flush().has_value());
    const fs::path log_file = find_file(root, "app.jsonl");
    NX_CHECK(!log_file.empty());
    const std::string log_text = read_file(log_file);
    NX_CHECK(log_text.find("policy file record 7") != std::string::npos);
    NX_CHECK(log_text.find("\"app\":\"policy\"") != std::string::npos);

    metrics::Settings metrics_settings{};
    metrics_settings.sink.path = (root / "metrics.jsonl").string();
    std::optional<app::policies::FilePolicy::MetricsSink> metric_sink;
    NX_CHECK(
        app::policies::FilePolicy::emplace_metrics_sink(metric_sink, metrics_settings, policy_identity())
            .has_value());
    NX_CHECK(metric_sink.has_value());
    NX_CHECK(fs::exists(root / "metrics.jsonl"));
    if (metric_sink.has_value()) {
        metrics::MetricRecord metric{};
        metric.kind = metrics::MetricKind::Counter;
        metric.scalar.counter = 9;
        metrics::MetricBatchSnapshot batch{};
        batch.records = &metric;
        batch.record_count = 1;
        batch.record_capacity = 1;
        NX_CHECK(metric_sink->on_event(batch).has_value());
        NX_CHECK(metric_sink->flush().has_value());
        const std::string metric_text = read_file(root / "metrics.jsonl");
        NX_CHECK(metric_text.find("\"app\":\"policy\"") != std::string::npos);
    }

    tracing::Settings tracing_settings{};
    tracing_settings.sink.path = (root / "traces.jsonl").string();
    std::optional<app::policies::FilePolicy::TraceSink> trace_sink;
    NX_CHECK(app::policies::FilePolicy::emplace_trace_sink(trace_sink, tracing_settings, policy_identity())
                 .has_value());
    NX_CHECK(trace_sink.has_value());
    if (trace_sink.has_value()) {
        NX_CHECK(trace_sink->start().has_value());
        NX_CHECK(fs::exists(root / "traces.jsonl"));
        tracing::SpanRecord span{};
        span.kind = tracing::SpanKind::Internal;
        span.status = tracing::SpanStatus::Unset;
        span.start = *chrono::Timestamp::now();
        span.end = span.start;
        span.file_name = "policies_test.cpp";
        span.function_name = "test_file_policy_writes_three_streams";
        span.line = 1;
        std::snprintf(span.name, sizeof(span.name), "file span");
        NX_CHECK(trace_sink->on_event(span).has_value());
        NX_CHECK(trace_sink->flush().has_value());
        const std::string trace_text = read_file(root / "traces.jsonl");
        NX_CHECK(trace_text.find("file span") != std::string::npos);
        NX_CHECK(trace_text.find("\"app\":\"policy\"") != std::string::npos);
    }

    fs::remove_all(root, error);
}

void test_null_policy_has_no_output() {
    std::fflush(stdout); // flush earlier suite output before redirecting
    int pipe_fds[2]{};
    NX_CHECK(::pipe(pipe_fds) == 0);
    const int saved_out = ::dup(STDOUT_FILENO);
    NX_CHECK(saved_out >= 0);
    NX_CHECK(::dup2(pipe_fds[1], STDOUT_FILENO) == STDOUT_FILENO);
    static_cast<void>(::close(pipe_fds[1]));

    logging::Settings log_settings{};
    metrics::Settings metrics_settings{};
    tracing::Settings tracing_settings{};
    std::optional<app::policies::NullPolicy::Logger> logger;
    std::optional<app::policies::NullPolicy::MetricsSink> metric_sink;
    std::optional<app::policies::NullPolicy::TraceSink> trace_sink;
    const bool logger_ok =
        app::policies::NullPolicy::emplace_logger(logger, log_settings, policy_identity()).has_value();
    const bool metrics_ok =
        app::policies::NullPolicy::emplace_metrics_sink(metric_sink, metrics_settings, policy_identity())
            .has_value();
    const bool traces_ok =
        app::policies::NullPolicy::emplace_trace_sink(trace_sink, tracing_settings, policy_identity())
            .has_value();
    if (logger.has_value()) {
        logger->info("this must not be written");
        static_cast<void>(logger->flush());
    }
    if (metric_sink.has_value()) {
        static_cast<void>(metric_sink->flush());
    }
    if (trace_sink.has_value()) {
        static_cast<void>(trace_sink->flush());
    }
    std::fflush(stdout);

    NX_CHECK(::dup2(saved_out, STDOUT_FILENO) == STDOUT_FILENO);
    static_cast<void>(::close(saved_out));

    char buffer[64]{};
    const ssize_t bytes = ::read(pipe_fds[0], buffer, sizeof(buffer));
    NX_CHECK(bytes == 0);
    static_cast<void>(::close(pipe_fds[0]));

    NX_CHECK(logger_ok);
    NX_CHECK(metrics_ok);
    NX_CHECK(traces_ok);
}

} // namespace

void run_app_policies_suites() {
    run_suite("policy_diagnostics", test_policy_diagnostics);
    run_suite("file_policy_invalid", test_file_policy_rejects_invalid_settings);
    run_suite("file_policy_streams", test_file_policy_writes_three_streams);
    run_suite("null_policy_no_output", test_null_policy_has_no_output);
}
