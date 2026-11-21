// nexigen::integration simulation_suite — flaky receiver
// (change add-simulation-suite-example; spec examples/simulation-suite).
//
// Behaves exactly like the well-behaved receiver and additionally terminates
// itself every NEXIGEN_SIM_EXIT_AFTER_S seconds with a non-zero exit code, so
// the Execution Manager's restart policy has something to raise. It shares the
// receive body with the healthy receiver, so display, trace linking, readiness,
// and checkpoint behaviour cannot drift.
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include <nexigen/com/event.hpp>
#include <nexigen/com/subscriber.hpp>
#include <nexigen/core/result.hpp>
#include <nexigen/metrics/policy_enums.hpp>
#include <nexigen/tracing/scoped_span.hpp>

#include "receiver_app.hpp"
#include "sim_common.hpp"
#include "vector_sample.hpp"

namespace {

using namespace simulation_suite;

/// @brief Executable tag used in every diagnostic.
constexpr std::string_view k_tag = "ex_sim_flaky_receiver";

/// @brief Application name; must match the suite and health manifests.
constexpr std::string_view k_app_name = "flaky_receiver";

/// @brief Checkpoint service the daemon listens on.
constexpr std::string_view k_checkpoint_service = "Nexigen/PHM/Simulation/flaky_receiver";

/// @brief Health service owned by this application.
constexpr std::string_view k_health_service = "Nexigen/PHM/Simulation/flaky_receiver/health";

/// @brief Default self-termination period.
constexpr std::uint64_t k_default_exit_after_s = 15;

/// @brief Four-character telemetry tag.
using Policy = SimTelemetryPolicy<'F', 'L', 'K', 'Y'>;

/// @brief Infrastructure instantiation used by the flaky receiver.
using Infrastructure = app::Infrastructure<Policy, 256, 8>;

using metrics::AccessPolicy;
using metrics::ConcurrencyPolicy;
using metrics::ResetPolicy;
constexpr ConcurrencyPolicy k_multi_threaded = ConcurrencyPolicy::MultiThreaded;
constexpr AccessPolicy k_readable = AccessPolicy::Readable;
constexpr ResetPolicy k_reset_on_read = ResetPolicy::ResetOnRead;

} // namespace

int main(int argc, char* argv[]) {
    const bool run_forever = env("NEXIGEN_SIM_RUN_FOREVER").value_or("") == "1";
    const std::uint64_t worker_seconds =
        env_u64("NEXIGEN_SIM_WORKER_SECONDS", k_default_worker_seconds);
    const bool spin = env("NEXIGEN_SIM_SPIN").value_or("") == "1";
    const std::int32_t rt_priority =
        static_cast<std::int32_t>(env_u64("NEXIGEN_SIM_RT_PRIORITY", 0));
    const std::uint64_t exit_after_s = env_u64("NEXIGEN_SIM_EXIT_AFTER_S", k_default_exit_after_s);

    std::vector<std::string_view> args;
    app::CommandLineOptions options;
    const std::optional<int> early_exit = prepare_command_line(argc, argv, args, k_tag, options);
    if (early_exit.has_value()) {
        return *early_exit;
    }

    auto node = com::Node::create();
    if (!node.has_value()) {
        print_error(k_tag, "com::Node::create failed");
        return 1;
    }
    install_shared_node(*node);

    auto created = app::make_infrastructure<Policy, 256, 8>();
    if (!created.has_value()) {
        print_error(k_tag, "make_infrastructure failed");
        return 1;
    }
    auto infra = std::move(*created);

    app::Settings loaded{};
    const core::Status initialized = initialize_infrastructure(*infra, options, loaded, k_tag);
    if (!initialized) {
        return fail_status(k_tag, "Infrastructure::initialize", initialized);
    }

    infra->logger().info("flaky receiver ready (exit_after_s={})", exit_after_s);

    if (rt_priority > 0) {
        const core::Status applied = apply_realtime_priority(rt_priority);
        if (applied) {
            infra->logger().info("SCHED_FIFO priority {}", rt_priority);
        } else {
            infra->logger().warning("SCHED_FIFO unavailable ({}); staying on the default policy",
                                    core::to_string(applied.error()));
        }
    }

    const auto meter = infra->meter("flaky_receiver", "1.0");
    if (!meter.has_value()) {
        static_cast<void>(infra->shutdown());
        return fail_status(k_tag, "Infrastructure::meter", meter);
    }
    const auto counter =
        (*meter)->create_counter<k_multi_threaded, k_readable, k_reset_on_read>("vectors_received");
    const auto latency =
        (*meter)->create_gauge<std::int64_t, k_multi_threaded, k_readable, k_reset_on_read>(
            "last_latency_us");
    if (!counter.has_value() || !latency.has_value()) {
        static_cast<void>(infra->shutdown());
        print_error(k_tag, "one or more instruments could not be created");
        return 1;
    }

    const auto tracer = infra->tracer_for("flaky_receiver.loop");
    if (!tracer.has_value()) {
        static_cast<void>(infra->shutdown());
        return fail_status(k_tag, "Infrastructure::tracer_for", tracer);
    }

    const auto service = com::ServiceName::create(std::string(k_vector_service).c_str());
    if (!service.has_value()) {
        static_cast<void>(infra->shutdown());
        return fail_status(k_tag, "com::ServiceName::create", service);
    }
    auto subscriber = com::Subscriber<VectorSample, tracing::TraceHeader>::create(*node, *service);
    if (!subscriber.has_value()) {
        static_cast<void>(infra->shutdown());
        return fail_status(k_tag, "com::Subscriber::create", subscriber);
    }
    const auto event_service =
        com::ServiceName::create(std::string(k_vector_event_service).c_str());
    if (!event_service.has_value()) {
        static_cast<void>(infra->shutdown());
        return fail_status(k_tag, "com::ServiceName::create", event_service);
    }
    auto vector_event = com::EventListener::create(*node, *event_service);
    if (!vector_event.has_value()) {
        static_cast<void>(infra->shutdown());
        return fail_status(k_tag, "com::EventListener::create", vector_event);
    }

    auto link_result =
        PlatformLink::create(*node, k_app_name, k_checkpoint_service, k_health_service);
    if (!link_result.has_value()) {
        static_cast<void>(infra->shutdown());
        return fail_status(k_tag, "PlatformLink::create", link_result);
    }
    PlatformLink& link = *link_result;
    static_cast<void>(link.report_ready());

    const auto start = std::chrono::steady_clock::now();
    const auto deadline = run_forever ? std::chrono::steady_clock::time_point::max()
                                      : start + std::chrono::seconds{worker_seconds};
    const auto crash_at = start + std::chrono::seconds{exit_after_s};
    bool crash_due = false;

    const app::InfrastructureStopReason reason =
        run_receiver_loop(*infra, *subscriber, link, *tracer, *counter, *latency, deadline,
                          crash_at, &crash_due, &*vector_event, spin);

    if (crash_due) {
        // Exit through the normal path so every destructor runs: iceoryx2
        // unregisters this process's node on teardown, and a raw `_exit` would
        // leak its registrations until the per-service node budget is gone.
        infra->logger().warning("flaky receiver terminating on schedule");
        const core::Status crash_shutdown = infra->shutdown();
        if (!crash_shutdown) {
            return fail_status(k_tag, "Infrastructure::shutdown", crash_shutdown);
        }
        std::printf("%.*s: simulated crash after %llu s\n", static_cast<int>(k_tag.size()),
                    k_tag.data(), static_cast<unsigned long long>(exit_after_s));
        std::fflush(stdout);
        return 1;
    }

    const core::Status shutdown_status = infra->shutdown();
    if (!shutdown_status) {
        return fail_status(k_tag, "Infrastructure::shutdown", shutdown_status);
    }
    std::printf("%.*s: done (reason=%s)\n", static_cast<int>(k_tag.size()), k_tag.data(),
                std::string(app::to_string(reason)).c_str());
    return 0;
}
