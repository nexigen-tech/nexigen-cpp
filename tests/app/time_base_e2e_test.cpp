// End-to-end tests for the time-base service (change add-time-base-service;
// spec chrono/time-base; tasks 8.3 and 8.4).
//
// The daemon itself is represented by a real POSIX shared-memory block with the
// production layout. The application attaches through the same client the
// daemon would feed, the telemetry sinks write real files, and the recorder
// stores a TimeBase history entry that the exporter resolver consumes.
#include "app_test_support.hpp"

#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <optional>
#include <string>
#include <thread>
#include <vector>

#include <unistd.h>

#include <nexigen/app/infrastructure_stop_reason.hpp>
#include <nexigen/chrono/boot_identity.hpp>
#include <nexigen/chrono/detail/time_base_segment.hpp>
#include <nexigen/chrono/detail/time_base_seqlock.hpp>
#include <nexigen/chrono/time_base_clock.hpp>
#include <nexigen/chrono/time_base_host_domain.hpp>
#include <nexigen/recorder/session_writer.hpp>
#include <nexigen/recorder/time_base_resolver.hpp>

namespace {

namespace app = nexigen::app;
namespace chrono = nexigen::chrono;
namespace core = nexigen::core;
namespace logging = nexigen::logging;
namespace metrics = nexigen::metrics;
namespace recorder = nexigen::recorder;
namespace tracing = nexigen::tracing;

[[nodiscard]] std::string e2e_dir() {
    return "/tmp/opencode/time_base_e2e_" + std::to_string(static_cast<long>(::getpid()));
}

[[nodiscard]] std::string read_file(const std::string& path) {
    std::ifstream file{path, std::ios::binary};
    return {std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>()};
}

/// File-backed policy whose paths are fixed per test process.
struct E2ePolicy {
    using Logger = logging::FileLogger<>;
    using MetricsSink = metrics::JsonFileMetricSink;
    using TraceSink = tracing::JsonFileTraceSink;
    using Sink = chrono::NullSink;

    static core::Status emplace_logger(std::optional<Logger>& out, const logging::Settings&,
                                       std::string_view app_name) noexcept {
        logging::FileLoggerConfig config{};
        config.app_name = std::string(app_name);
        config.base_dir = e2e_dir();
        config.file_name = "app.jsonl";
        config.rotation.kind = logging::RotationSchedule::None;
        out.emplace();
        return out->initialize(config);
    }
    static core::Status emplace_metrics_sink(std::optional<MetricsSink>& out,
                                             const metrics::Settings&,
                                             std::string_view app_name) noexcept {
        metrics::JsonFileMetricSinkConfig config{};
        config.path = e2e_dir() + "/metrics.jsonl";
        config.append = false;
        config.app_name = std::string(app_name);
        out.emplace(config);
        return {};
    }
    static core::Status emplace_trace_sink(std::optional<TraceSink>& out, const tracing::Settings&,
                                           std::string_view app_name) noexcept {
        tracing::JsonFileTraceSinkConfig config{};
        config.path = e2e_dir() + "/traces.jsonl";
        config.app_name = std::string(app_name);
        out.emplace(config);
        return out->start();
    }
};

using E2eInfra = app::Infrastructure<E2ePolicy, 64, 4>;

[[nodiscard]] std::unique_ptr<E2eInfra> make_e2e_infra() {
    auto created = app::make_infrastructure<E2ePolicy, 64, 4>();
    NX_CHECK(created.has_value());
    return created.has_value() ? std::move(*created) : nullptr;
}

/// Publish one Ready block with a UTC calibration window.
void publish_ready(chrono::detail::TimeBaseSegment& segment, std::uint64_t heartbeat,
                   std::int64_t utc_offset) {
    const auto now = chrono::Timestamp::now();
    NX_CHECK(now.has_value());
    const std::uint64_t mono = now ? now->nanoseconds() : heartbeat;
    chrono::TimeBaseBlock block{};
    block.status = chrono::TimeBaseStatus::Ready;
    block.boot_token = chrono::current_boot_token();
    block.domain_count = 3U;
    block.utc_offset_ns = utc_offset;
    block.rows[0].domain_id = chrono::k_monotonic_raw_domain_id;
    block.rows[0].domain_class = chrono::TimeDomainClass::SystemClock;
    block.rows[0].lock = chrono::LockState::Locked;
    block.rows[0].scale_fwd_q32 = 1ULL << 32U;
    block.rows[0].native_anchor = mono;
    block.rows[0].canonical_anchor_ns = static_cast<std::int64_t>(mono);
    block.rows[0].valid_from_native = mono;
    block.rows[0].valid_to_native = mono + 60'000'000'000ULL;
    block.rows[0].valid_from_canonical = mono;
    block.rows[0].valid_to_canonical = mono + 60'000'000'000ULL;
    block.rows[1].domain_id = chrono::k_host_counter_domain_id;
    block.rows[1].domain_class = chrono::TimeDomainClass::CycleCounter;
    block.rows[1].lock = chrono::LockState::Locked;
    block.rows[1].epoch = 1U;
    block.rows[1].scale_fwd_q32 = 1ULL << 32U;
    block.rows[1].scale_rev_q32 = 1ULL << 32U;
    block.rows[1].flags = chrono::DomainCoefficient::has_reverse_bit;
    block.rows[1].native_anchor = mono;
    block.rows[1].canonical_anchor_ns = static_cast<std::int64_t>(mono);
    block.rows[1].valid_from_native = mono;
    block.rows[1].valid_to_native = mono + 60'000'000'000ULL;
    block.rows[1].valid_from_canonical = mono;
    block.rows[1].valid_to_canonical = mono + 60'000'000'000ULL;
    block.rows[2].domain_id = chrono::k_realtime_domain_id;
    block.rows[2].domain_class = chrono::TimeDomainClass::SystemClock;
    block.rows[2].lock = chrono::LockState::Locked;
    block.rows[2].epoch = 1U;
    block.rows[2].native_anchor =
        static_cast<std::uint64_t>(static_cast<std::int64_t>(mono) + utc_offset);
    block.rows[2].canonical_anchor_ns = static_cast<std::int64_t>(mono);
    block.rows[2].scale_fwd_q32 = 1ULL << 32U;
    block.rows[2].scale_rev_q32 = 1ULL << 32U;
    block.rows[2].flags = chrono::DomainCoefficient::has_reverse_bit;
    block.rows[2].valid_from_native = block.rows[2].native_anchor;
    block.rows[2].valid_to_native = block.rows[2].native_anchor + 60'000'000'000ULL;
    block.rows[2].valid_from_canonical = mono;
    block.rows[2].valid_to_canonical = mono + 60'000'000'000ULL;
    chrono::detail::TimeBaseWriter{*segment.block()}.publish(block, heartbeat);
}

void test_e2e_records_and_resolution() {
    static_cast<void>(::mkdir(e2e_dir().c_str(), 0700));
    const std::string segment_name =
        "/nexigen-e2e-" + std::to_string(static_cast<long>(::getpid()));
    auto created = chrono::detail::TimeBaseSegment::create(segment_name, 0xE2E0U);
    NX_CHECK(created.has_value());
    if (!created) {
        return;
    }
    std::optional<chrono::detail::TimeBaseSegment> segment{std::move(*created)};
    std::int64_t utc_offset = 0;
    NX_CHECK(chrono::sample_utc_offset(utc_offset).has_value());
    const auto now = chrono::Timestamp::now();
    NX_CHECK(now.has_value());
    publish_ready(*segment, now ? now->nanoseconds() : 1U, utc_offset);
    NX_CHECK(chrono::TimeBaseClock::attach(segment_name).has_value());

    // Any-to-any conversion through the client: a TSC instant becomes a hub
    // instant and then a realtime instant, without treating TSC as the hub.
    auto client = chrono::TimeBaseClient::attach(segment_name, chrono::TimeBaseClientConfig{});
    NX_CHECK(client.has_value());
    if (client.has_value()) {
        const auto tsc_now = chrono::Timestamp::now();
        NX_CHECK(tsc_now.has_value());
        const std::uint64_t tsc_native =
            tsc_now ? tsc_now->nanoseconds() : static_cast<std::uint64_t>(1U);
        const auto realtime =
            client->convert(chrono::DomainInstant{chrono::k_host_counter_domain_id, 1U, tsc_native},
                            chrono::k_realtime_domain_id);
        NX_CHECK(realtime.has_value());
        if (realtime.has_value()) {
            NX_CHECK(realtime->epoch == 1U);
            NX_CHECK(realtime->value == static_cast<std::uint64_t>(
                                            static_cast<std::int64_t>(tsc_native) + utc_offset));
        }
    }

    auto infra = make_e2e_infra();
    NX_CHECK(infra != nullptr);
    if (infra != nullptr) {
        app::Settings settings{};
        settings.app_name = "e2e";
        settings.chrono.segment_name = segment_name;
        NX_CHECK(infra->initialize(settings).has_value());
        (*infra->logger())->info("e2e record {}", 42);
        auto meter = infra->meter("e2e", "1.0");
        NX_CHECK(meter.has_value());
        if (meter.has_value()) {
            auto counter = (*meter)->create_counter("ticks");
            if (counter.has_value()) {
                (*counter)->add(3U);
            }
        }
        auto tracer = infra->tracer_for("e2e.span");
        NX_CHECK(tracer.has_value());
        if (tracer.has_value()) {
            auto span = (*tracer)->start_span("work", tracing::SpanKind::Internal);
            static_cast<void>(span.end());
        }
        NX_CHECK((*infra->metrics())->collect_now().has_value());
        NX_CHECK((*infra->metrics())->flush().has_value());
        NX_CHECK((*infra->tracer())->flush().has_value());
        NX_CHECK(infra->shutdown().has_value());
    }
    chrono::TimeBaseClock::detach();

    const std::string log = read_file(e2e_dir() + "/app.jsonl");
    const std::string metrics_file = read_file(e2e_dir() + "/metrics.jsonl");
    const std::string traces = read_file(e2e_dir() + "/traces.jsonl");
    NX_CHECK(log.find("\"mono_ns\":") != std::string::npos);
    NX_CHECK(log.find("\"boot\":\"") != std::string::npos);
    NX_CHECK(log.find("\"timestamp\":null") == std::string::npos);
    NX_CHECK(metrics_file.find("\"mono_ns\":") != std::string::npos);
    NX_CHECK(traces.find("\"start_mono_ns\":") != std::string::npos);

    // The recorder captures the same rows as one TimeBase history entry.
    recorder::TimeBasePayload payload{};
    payload.generation = 1U;
    payload.time_base_epoch = 7U;
    payload.boot_token = chrono::current_boot_token();
    payload.utc_offset_ns = utc_offset;
    payload.domain_count = 3U;
    payload.status = chrono::TimeBaseStatus::Ready;
    payload.rows[0].domain_id = chrono::k_host_counter_domain_id;
    payload.rows[0].domain_class = chrono::TimeDomainClass::CycleCounter;
    payload.rows[0].lock = chrono::LockState::Locked;
    payload.rows[0].epoch = 1U;
    payload.rows[0].native_anchor = 3'000U;
    payload.rows[0].canonical_anchor_ns = 1'000;
    payload.rows[0].scale_fwd_q32 = 1ULL << 32U;
    payload.rows[0].valid_from_native = 0U;
    payload.rows[0].valid_to_native = 60'000U;
    payload.rows[0].valid_from_canonical = 0U;
    payload.rows[0].valid_to_canonical = 60'000U;
    payload.rows[1].domain_id = chrono::k_realtime_domain_id;
    payload.rows[1].domain_class = chrono::TimeDomainClass::SystemClock;
    payload.rows[1].lock = chrono::LockState::Locked;
    payload.rows[1].epoch = 1U;
    payload.rows[1].native_anchor = static_cast<std::uint64_t>(utc_offset) + 1'000U;
    payload.rows[1].canonical_anchor_ns = 1'000;
    payload.rows[1].scale_fwd_q32 = 1ULL << 32U;
    payload.rows[1].scale_rev_q32 = 1ULL << 32U;
    payload.rows[1].valid_from_canonical = 0U;
    payload.rows[1].valid_to_canonical = 60'000U;
    payload.rows[1].flags = chrono::DomainCoefficient::has_reverse_bit;
    payload.rows[2].domain_id = chrono::k_tai_domain_id;
    payload.rows[2].domain_class = chrono::TimeDomainClass::SystemClock;
    payload.rows[2].lock = chrono::LockState::Locked;
    payload.rows[2].epoch = 1U;
    payload.rows[2].native_anchor = 37'000'000'000ULL + 1'000U;
    payload.rows[2].canonical_anchor_ns = 1'000;
    payload.rows[2].scale_fwd_q32 = 1ULL << 32U;
    payload.rows[2].scale_rev_q32 = 1ULL << 32U;
    payload.rows[2].valid_from_native = 37'000'000'000ULL;
    payload.rows[2].valid_to_native = 37'000'000'000ULL + 60'000U;
    payload.rows[2].valid_from_canonical = 0U;
    payload.rows[2].valid_to_canonical = 60'000U;
    payload.rows[2].flags = chrono::DomainCoefficient::has_reverse_bit;

    recorder::TimeBaseResolver resolver{};
    NX_CHECK(resolver.add(payload));
    const auto canonical =
        resolver.canonical_from_native(chrono::k_host_counter_domain_id, 1U, 6'000U);
    NX_CHECK(canonical == std::optional<std::int64_t>{4'000});
    const auto utc = resolver.utc_from_canonical(4'000U);
    NX_CHECK(utc.has_value());
    const auto tai =
        resolver.convert(chrono::k_host_counter_domain_id, 1U, 6'000U, chrono::k_tai_domain_id);
    NX_CHECK(tai == std::optional<std::uint64_t>{37'000'000'000ULL + 4'000U});
    NX_CHECK(resolver.tai_from_canonical(4'000U) ==
             std::optional<std::int64_t>{37'000'000'000LL + 4'000LL});

    segment->unlink();
}

void test_e2e_time_base_loss() {
    const std::string segment_name =
        "/nexigen-e2e-loss-" + std::to_string(static_cast<long>(::getpid()));
    auto created = chrono::detail::TimeBaseSegment::create(segment_name, 0xE2E1U);
    NX_CHECK(created.has_value());
    if (!created) {
        return;
    }
    std::optional<chrono::detail::TimeBaseSegment> segment{std::move(*created)};
    const auto now = chrono::Timestamp::now();
    NX_CHECK(now.has_value());
    publish_ready(*segment, now ? now->nanoseconds() : 1U, 0);
    NX_CHECK(chrono::TimeBaseClock::attach(segment_name).has_value());

    auto infra = make_e2e_infra();
    NX_CHECK(infra != nullptr);
    if (infra != nullptr) {
        app::Settings settings{};
        settings.app_name = "e2e-loss";
        settings.chrono.segment_name = segment_name;
        settings.chrono.stale_after = std::chrono::milliseconds{20};
        settings.chrono.holdover = std::chrono::milliseconds{40};
        NX_CHECK(infra->initialize(settings).has_value());
        // The daemon stops publishing: the heartbeat freezes and the client
        // must move through holdover to lost without a retry.
        std::this_thread::sleep_for(std::chrono::milliseconds{90});
        const app::InfrastructureStopReason reason = infra->run([] {});
        NX_CHECK(reason == app::InfrastructureStopReason::TimeBaseLost);
        NX_CHECK(app::time_base_lost_exit_code == 75);
        NX_CHECK(infra->shutdown().has_value());
    }
    chrono::TimeBaseClock::detach();
    segment->unlink();
}

} // namespace

void run_app_time_base_e2e_suites() {
    run_suite("time_base_e2e_records", test_e2e_records_and_resolution);
    run_suite("time_base_e2e_loss", test_e2e_time_base_loss);
}
