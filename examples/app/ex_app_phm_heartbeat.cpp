// nexigen::app + nexigen::phm — application-side health participation example
// (change add-app-phm-client): an application reports a checkpoint each tick,
// reports a health fault once, and reacts to a Function Group SafeState read
// from the SM-published Field.
#include <cstdint>
#include <cstdio>
#include <memory>
#include <optional>
#include <string_view>
#include <utility>

#include <nexigen/app/infrastructure.hpp>
#include <nexigen/chrono/null_sink.hpp>
#include <nexigen/com/node.hpp>
#include <nexigen/com/service_name.hpp>
#include <nexigen/logging/console_logger.hpp>
#include <nexigen/metrics/console_metric_sink.hpp>
#include <nexigen/phm/health_state.hpp>
#include <nexigen/phm/supervised_entity.hpp>
#include <nexigen/tracing/console_trace_sink.hpp>

namespace {

namespace app = nexigen::app;
namespace com = nexigen::com;
namespace core = nexigen::core;
namespace chrono = nexigen::chrono;
namespace logging = nexigen::logging;
namespace metrics = nexigen::metrics;
namespace phm = nexigen::phm;
namespace tracing = nexigen::tracing;

struct SimplePolicy {
    using Logger = logging::ConsoleLogger<>;
    using MetricsSink = metrics::ConsoleMetricSink;
    using TraceSink = tracing::ConsoleTraceSink;
    using Sink = chrono::NullSink;

    static core::Status emplace_logger(std::optional<Logger>& out, const logging::Settings&,
                                       std::string_view) noexcept {
        out.emplace();
        return out->initialize(logging::ConsoleLoggerConfig{});
    }

    static core::Status emplace_metrics_sink(std::optional<MetricsSink>& out,
                                             const metrics::Settings&, std::string_view) noexcept {
        out.emplace();
        return {};
    }

    static core::Status emplace_trace_sink(std::optional<TraceSink>& out, const tracing::Settings&,
                                           std::string_view) noexcept {
        out.emplace(1);
        return {};
    }

    static core::Status emplace_chrono_sink(std::optional<Sink>& out,
                                            const chrono::Settings&) noexcept {
        out.emplace();
        return {};
    }
};

int fail(std::string_view message) {
    std::fprintf(stderr, "ex_app_phm_heartbeat: %s\n", std::string(message).c_str());
    return 1;
}

} // namespace

auto main() -> int {
    auto node = com::Node::create();
    if (!node) {
        return fail("com::Node::create failed");
    }
    const auto checkpoint_service = com::ServiceName::create("Nexigen/Example/Checkpoints");
    const auto health_service = com::ServiceName::create("Nexigen/Example/Health");
    if (!checkpoint_service || !health_service) {
        return fail("bad service name");
    }
    auto entity = phm::SupervisedEntity::create(*node, "hub_example", *checkpoint_service,
                                                *health_service);
    if (!entity) {
        return fail("SupervisedEntity::create failed");
    }

    auto created = app::make_infrastructure<SimplePolicy, 256, 8>();
    if (!created.has_value()) {
        return fail("infrastructure allocation failed");
    }
    auto infra = std::move(*created);
    if (!infra->initialize(app::Settings{}).has_value()) {
        return fail("Infrastructure::initialize failed");
    }

    std::uint32_t tick_count = 0;
    bool health_reported = false;
    bool safe_state_seen = false;
    const app::InfrastructureStopReason reason = infra->run([&] {
        ++tick_count;
        // Heartbeat: alive progress for the PHM daemon every tick.
        static_cast<void>(entity->report_checkpoint(tick_count));
        if (!health_reported) {
            static_cast<void>(entity->report_health(phm::HealthState::Warning));
            health_reported = true;
        }
        // Recovery: react to the Function Group state read from the SM Field.
        const auto state = entity->current_group_state("motion_control");
        if (state.has_value() && state->has_value() &&
            std::string_view((*state)->state) == "SafeState") {
            safe_state_seen = true;
            infra->request_stop(app::InfrastructureStopReason::Requested);
        }
        if (tick_count >= 3) {
            infra->request_stop(app::InfrastructureStopReason::Requested);
        }
    });

    std::printf("ex_app_phm_heartbeat: ticks=%u checkpoints=%s safe_state=%s\n", tick_count,
                entity->to_string().c_str(), safe_state_seen ? "yes" : "no");
    return reason == app::InfrastructureStopReason::Requested ? 0 : 1;
}
