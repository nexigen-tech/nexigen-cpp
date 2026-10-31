// nexigen::app — minimal Infrastructure example.
//
// No application class, no settings files, no signal helper: one main()
// creates the heap-only infrastructure with a stateless policy, initializes
// it with defaults, demonstrates logging, a metric and a span, runs three
// ticks, requests a stop and shuts everything down.
#include <cstdint>
#include <cstdio>
#include <memory>
#include <optional>
#include <string_view>
#include <utility>

#include <nexigen/app/infrastructure.hpp>
#include <nexigen/logging/console_logger.hpp>
#include <nexigen/metrics/console_metric_sink.hpp>
#include <nexigen/tracing/console_trace_sink.hpp>

namespace {

namespace app = nexigen::app;
namespace core = nexigen::core;
namespace logging = nexigen::logging;
namespace metrics = nexigen::metrics;
namespace tracing = nexigen::tracing;

struct Simple_policy {
    using Logger = logging::ConsoleLogger<>;
    using Metrics_sink = metrics::ConsoleMetricSink;
    using Trace_sink = tracing::ConsoleTraceSink;

    static core::Status emplace_logger(
        std::optional<Logger>& out,
        const logging::Logging_settings&) noexcept {
        out.emplace();
        return out->initialize(logging::ConsoleLogger_config{});
    }

    static core::Status emplace_metrics_sink(
        std::optional<Metrics_sink>& out,
        const app::Metrics_settings&) noexcept {
        out.emplace();
        return {};
    }

    static core::Status emplace_trace_sink(
        std::optional<Trace_sink>& out,
        const app::Tracing_settings&) noexcept {
        out.emplace(1);
        return {};
    }
};

int fail(std::string_view message) {
    std::fprintf(stderr, "ex_app_lifecycle: %s\n", std::string(message).c_str());
    return 1;
}

} // namespace

int main() {
    auto created = app::make_infrastructure<Simple_policy, 256, 8>();
    if (!created.has_value()) {
        return fail("make_infrastructure failed");
    }
    auto infra = std::move(*created);

    if (!infra->initialize(app::App_settings{}).has_value()) {
        return fail("Infrastructure::initialize failed");
    }

    infra->logger().info("infrastructure ready");

    const auto meter = infra->meter("demo", "1.0");
    if (!meter.has_value()) {
        return fail("infra->meter failed");
    }
    const auto ticks = (*meter)->create_counter<metrics::ConcurrencyPolicy::SingleThreaded,
                                                metrics::AccessPolicy::Readable>("ticks");
    if (!ticks.has_value()) {
        return fail("create_counter failed");
    }

    const auto tracer = infra->tracer_for("demo.loop");
    if (!tracer.has_value()) {
        return fail("infra->tracer_for failed");
    }

    auto span = (*tracer)->start_span("loop", tracing::SpanKind::Internal);
    int count = 0;
    const app::Infrastructure_stop_reason reason = infra->run([&] {
        (*ticks)->inc();
        ++count;
        if (count == 3) {
            infra->request_stop(app::Infrastructure_stop_reason::Requested);
        }
    });
    static_cast<void>(span.set_attribute("ticks", static_cast<std::int64_t>(count)));
    static_cast<void>(span.end());

    if (reason != app::Infrastructure_stop_reason::Requested) {
        return fail("run() did not return Requested");
    }
    if (!infra->shutdown().has_value()) {
        return fail("shutdown reported a flush failure");
    }

    std::printf("ex_app_lifecycle: OK (ticks=%d, reason=requested)\n", count);
    return 0;
}
