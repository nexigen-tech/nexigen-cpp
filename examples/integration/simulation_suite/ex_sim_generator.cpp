// nexigen::integration simulation_suite — vector generator
// (change add-simulation-suite-example; spec examples/simulation-suite).
//
// Publishes one VectorSample per period over nexigen::com with a tracing
// TraceHeader carrying a fresh per-vector trace, reports readiness to the
// Execution Manager, publishes checkpoints to PlatformHealthManagement, and
// honours the State Manager's published Function Group state. Telemetry (logs,
// metrics, spans) reaches the three daemons through the iceoryx2 sinks.
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <optional>
#include <random>
#include <string>
#include <string_view>
#include <thread>
#include <vector>
#include <utility>

#include <unistd.h>

#include <nexigen/com/event.hpp>
#include <nexigen/com/publisher.hpp>
#include <nexigen/core/result.hpp>
#include <nexigen/metrics/policy_enums.hpp>
#include <nexigen/tracing/scoped_span.hpp>
#include <nexigen/tracing/span_kind.hpp>
#include <nexigen/tracing/span_status.hpp>
#include <nexigen/tracing/trace_header.hpp>

#include "sim_common.hpp"
#include "vector_sample.hpp"

namespace {

using namespace simulation_suite;

/// @brief Executable tag used in every diagnostic.
constexpr std::string_view k_tag = "ex_sim_generator";

/// @brief Application name; must match the suite and health manifests.
constexpr std::string_view k_app_name = "vector_generator";

/// @brief Checkpoint service the daemon listens on.
constexpr std::string_view k_checkpoint_service = "Nexigen/PHM/Simulation/vector_generator";

/// @brief Health service owned by this application.
constexpr std::string_view k_health_service = "Nexigen/PHM/Simulation/vector_generator/health";

/// @brief Four-character telemetry tag.
using Policy = SimTelemetryPolicy<'G', 'E', 'N', '0'>;

/// @brief Infrastructure instantiation used by the generator.
using Infrastructure = app::Infrastructure<Policy, 256, 8>;

using metrics::AccessPolicy;
using metrics::ConcurrencyPolicy;
using metrics::ResetPolicy;
constexpr ConcurrencyPolicy k_multi_threaded = ConcurrencyPolicy::MultiThreaded;
constexpr AccessPolicy k_readable = AccessPolicy::Readable;
constexpr ResetPolicy k_reset_on_read = ResetPolicy::ResetOnRead;

/// @brief Generate the ten values of one vector.
///
/// @param rng Generator-owned engine.
/// @param distribution Uniform distribution over the value range.
/// @param sample Receives the values.
void fill_values(std::mt19937_64& rng, std::uniform_real_distribution<double>& distribution,
                 VectorSample& sample) {
    for (std::size_t i = 0; i < k_vector_values; ++i) {
        sample.values[i] = distribution(rng);
    }
}

} // namespace

int main(int argc, char* argv[]) {
    const bool run_forever = env("NEXIGEN_SIM_RUN_FOREVER").value_or("") == "1";
    const std::uint64_t worker_seconds =
        env_u64("NEXIGEN_SIM_WORKER_SECONDS", k_default_worker_seconds);
    const std::uint64_t period_ms = env_u64("NEXIGEN_SIM_PERIOD_MS", 1000);

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

    (*infra->logger())->info("generator ready (period_ms={})", period_ms);
    (*infra->logger())->info("function group '{}'", k_function_group);

    const auto meter = infra->meter("generator", "1.0");
    if (!meter.has_value()) {
        static_cast<void>(infra->shutdown());
        return fail_status(k_tag, "Infrastructure::meter", meter);
    }
    const auto counter =
        (*meter)->create_counter<k_multi_threaded, k_readable, k_reset_on_read>("vectors_sent");
    const auto gauge =
        (*meter)->create_gauge<std::int64_t, k_multi_threaded, k_readable, k_reset_on_read>(
            "last_sequence");
    const auto publish_cost =
        (*meter)->create_gauge<std::int64_t, k_multi_threaded, k_readable, k_reset_on_read>(
            "publish_cost_us");
    const auto loan_cost =
        (*meter)->create_gauge<std::int64_t, k_multi_threaded, k_readable, k_reset_on_read>(
            "loan_cost_us");
    if (!counter.has_value() || !gauge.has_value() || !publish_cost.has_value() ||
        !loan_cost.has_value()) {
        static_cast<void>(infra->shutdown());
        print_error(k_tag, "one or more instruments could not be created");
        return 1;
    }

    const auto tracer = infra->tracer_for("generator.loop");
    if (!tracer.has_value()) {
        static_cast<void>(infra->shutdown());
        return fail_status(k_tag, "Infrastructure::tracer_for", tracer);
    }

    const auto service = com::ServiceName::create(std::string(k_vector_service).c_str());
    if (!service.has_value()) {
        static_cast<void>(infra->shutdown());
        return fail_status(k_tag, "com::ServiceName::create", service);
    }
    auto publisher = com::Publisher<VectorSample, tracing::TraceHeader>::create(*node, *service);
    if (!publisher.has_value()) {
        static_cast<void>(infra->shutdown());
        return fail_status(k_tag, "com::Publisher::create", publisher);
    }
    const auto event_service =
        com::ServiceName::create(std::string(k_vector_event_service).c_str());
    if (!event_service.has_value()) {
        static_cast<void>(infra->shutdown());
        return fail_status(k_tag, "com::ServiceName::create", event_service);
    }
    auto vector_event = com::EventNotifier::create(*node, *event_service);
    if (!vector_event.has_value()) {
        static_cast<void>(infra->shutdown());
        return fail_status(k_tag, "com::EventNotifier::create", vector_event);
    }

    auto link_result =
        PlatformLink::create(*node, k_app_name, k_checkpoint_service, k_health_service);
    if (!link_result.has_value()) {
        static_cast<void>(infra->shutdown());
        return fail_status(k_tag, "PlatformLink::create", link_result);
    }
    PlatformLink& link = *link_result;
    static_cast<void>(link.report_ready());

    // Pre-loan the first chunk and keep one ahead of the loop: `loan_uninit`
    // costs microseconds, and taking it off the publish path is exactly the
    // "pre-allocate before the hot path" rule. The fallback below still handles
    // a pool that cannot lend at that moment.
    using Loan = com::SampleMutUninit<VectorSample, tracing::TraceHeader>;
    std::optional<Loan> next_loan{};
    const auto preloan = [&]() -> std::optional<std::uint64_t> {
        const std::uint64_t before = now_ns();
        auto loaned = publisher->loan_uninit();
        if (!loaned.has_value()) {
            return std::nullopt;
        }
        next_loan.emplace(std::move(*loaned));
        return (now_ns() - before) / 1000U;
    };
    static_cast<void>(preloan());

    std::mt19937_64 rng(static_cast<std::uint64_t>(::getpid()) ^ now_ns());
    std::uniform_real_distribution<double> distribution(-1.0, 1.0);

    const auto start = std::chrono::steady_clock::now();
    const auto deadline = run_forever ? std::chrono::steady_clock::time_point::max()
                                      : start + std::chrono::seconds{worker_seconds};
    const auto period = std::chrono::milliseconds(period_ms);
    std::uint64_t sequence = 0;
    std::uint32_t header_sequence = 0;
    std::string last_state{};
    auto next_publish = start;

    const app::InfrastructureStopReason reason = infra->run([&]() -> bool {
        const auto now = std::chrono::steady_clock::now();
        if (now >= deadline) {
            return false;
        }
        if (now < next_publish) {
            std::this_thread::sleep_until(next_publish);
            return true;
        }
        next_publish += (period.count() > 0 ? period : std::chrono::milliseconds{1});

        const std::string_view state = link.group_state();
        if (state != last_state) {
            (*infra->logger())->info("group state '{}'", state_name(state));
            last_state = state;
        }
        if (!state_allows_run(state)) {
            (*infra->logger())->warning("generator paused while group is '{}'", state_name(last_state));
            return true;
        }

        NEXIGEN_TRACE_NEW_ROOT("SimulationGenerator");
        auto span = (*tracer)->start_span("publish-vector", tracing::SpanKind::Producer);
        static_cast<void>(span.set_attribute("sequence", static_cast<std::int64_t>(sequence)));

        VectorSample sample{};
        sample.sequence = sequence;
        sample.sent_ns = now_ns();
        fill_values(rng, distribution, sample);

        if (!next_loan.has_value()) {
            // Pool was empty when we tried to pre-loan: take the cost here.
            if (const auto cold = preloan(); !cold.has_value()) {
                (*infra->logger())->warning("loan_uninit failed: pool empty");
                return true;
            }
        }
        {
            auto mut = next_loan->write_payload(sample);
            next_loan.reset();
            const tracing::TraceHeader header = tracing::make_trace_header(header_sequence++);
            mut.user_header_mut() = header;
            const auto sent = publisher->send(std::move(mut));
            const std::uint64_t send_done_ns = now_ns();
            // Pre-loan the next chunk while its segment and the pool are hot.
            if (const auto cost = preloan(); cost.has_value()) {
                (*loan_cost)->set(static_cast<std::int64_t>(*cost));
            }
            if (!sent) {
                (*infra->logger())->warning("publish failed: {}", com::to_string(sent.error()));
            } else {
                const std::uint64_t publish_cost_us = (send_done_ns - sample.sent_ns) / 1000U;
                (*counter)->inc();
                (*gauge)->set(static_cast<std::int64_t>(sequence));
                (*publish_cost)->set(static_cast<std::int64_t>(publish_cost_us));
                // Wake the receivers: the notification is a counter, so a lost
                // one is still covered by the queued sample.
                static_cast<void>(vector_event->notify());
                static_cast<void>(link.checkpoint(1));
                (*infra->logger())->debug("published vector #{} trace_id={:016x} cost_us={}", sequence,
                                      header.trace_id.value(), publish_cost_us);
            }
        }

        static_cast<void>(span.set_status(tracing::SpanStatus::Ok));
        ++sequence;
        return true;
    });

    const core::Status shutdown_status = infra->shutdown();
    if (!shutdown_status) {
        return fail_status(k_tag, "Infrastructure::shutdown", shutdown_status);
    }
    std::printf("%.*s: done (vectors=%llu, reason=%s)\n", static_cast<int>(k_tag.size()),
                k_tag.data(), static_cast<unsigned long long>(sequence),
                std::string(app::to_string(reason)).c_str());
    return 0;
}
