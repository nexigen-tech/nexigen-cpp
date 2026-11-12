// nexigen-sm — State Manager process (change add-sm; spec sm/lifecycle +
// sm/state-machine; design D1/D6).
//
// Foreground process launched and supervised by the EM (never a standalone
// daemon): loads the SM manifest, locks its pages, pins the arbitration
// thread to the RT core under SCHED_FIFO, publishes Function Group states as
// Fields, serves TriggerIn requests, handles PHM recovery notifications, and
// commands the EM through the StateClient channel. The arbitration thread
// blocks on one WaitSet; 0% CPU while idle.
//
// Usage: nexigen-sm --config <path> [--rt-core <mask>] [--priority <n>]
//                   [--audit-log <path>] [--help]
// Env (EM-provided): EASY_SM_RT_CPU_MASK
#include <csignal>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <optional>
#include <string>
#include <vector>

#include <sys/mman.h>

#include <nlohmann/json.hpp>

#include <nexigen/com/client.hpp>
#include <nexigen/com/node.hpp>
#include <nexigen/com/server.hpp>
#include <nexigen/com/service_name.hpp>
#include <nexigen/com/waitset.hpp>
#include <nexigen/core/affinity.hpp>
#include <nexigen/core/thread_management.hpp>
#include <nexigen/exec/state_ack.hpp>
#include <nexigen/exec/state_command.hpp>
#include <nexigen/phm/checkpoint.hpp>
#include <nexigen/sm/recovery_handler.hpp>
#include <nexigen/sm/sm_manifest.hpp>
#include <nexigen/sm/sm_metrics.hpp>
#include <nexigen/sm/state_manager.hpp>
#include <nexigen/sm/state_publication.hpp>
#include <nexigen/sm/transition_audit.hpp>
#include <nexigen/sm/transition_preparation.hpp>
#include <nexigen/sm/transition_record.hpp>
#include <nexigen/sm/trigger_request.hpp>
#include <nexigen/sm/trigger_response.hpp>

namespace {

using Clock = std::chrono::steady_clock;

volatile std::sig_atomic_t g_stop = 0;

void stop_handler(int /*signal*/) {
    g_stop = 1;
}

void print_usage() {
    std::printf("nexigen-sm - State Manager\n"
                "Usage: nexigen-sm --config <path> [--rt-core <mask>] [--priority <n>]\n"
                "                  [--audit-log <path>] [--help]\n");
}

struct Options {
    std::string config_path{};
    std::uint64_t rt_core_mask{0};
    std::int32_t priority{85};
    std::string audit_log{"/tmp/nexigen-sm-audit.log"};
};

[[nodiscard]] bool take_value(int argc, char* argv[], int& index, std::string& target) {
    if (index + 1 >= argc) {
        return false;
    }
    target = argv[++index];
    return true;
}

[[nodiscard]] std::optional<int> parse_args(int argc, char* argv[], Options& options) {
    for (int index = 1; index < argc; ++index) {
        const std::string argument = argv[index];
        if (argument == "--help" || argument == "-h") {
            print_usage();
            return 0;
        }
        if (argument == "--config") {
            if (!take_value(argc, argv, index, options.config_path)) {
                return 2;
            }
            continue;
        }
        if (argument == "--rt-core") {
            std::string text;
            if (!take_value(argc, argv, index, text)) {
                return 2;
            }
            options.rt_core_mask = std::strtoull(text.c_str(), nullptr, 0);
            continue;
        }
        if (argument == "--priority") {
            std::string text;
            if (!take_value(argc, argv, index, text)) {
                return 2;
            }
            options.priority = static_cast<std::int32_t>(std::strtol(text.c_str(), nullptr, 10));
            continue;
        }
        if (argument == "--audit-log") {
            if (!take_value(argc, argv, index, options.audit_log)) {
                return 2;
            }
            continue;
        }
        std::fprintf(stderr, "nexigen-sm: unknown argument '%s'\n", argument.c_str());
        return 2;
    }
    if (options.config_path.empty()) {
        std::fprintf(stderr, "nexigen-sm: --config <path> is required\n");
        return 2;
    }
    return std::nullopt;
}

/// One accepted transition that awaits its preparation deadline before the
/// final EM command (prepare-for-transition handshake, timeout branch).
/// Fixed buffers keep the arbitration loop allocation-free (REQ_SM_012).
struct PendingTransition {
    std::size_t group_index{0};
    char target_state[nexigen::sm::k_holder_requester_length]{};
    Clock::time_point deadline{};
};

using TriggerServer =
    nexigen::com::Server<nexigen::sm::TriggerRequest, nexigen::sm::TriggerResponse>;
using EmClient = nexigen::com::Client<nexigen::exec::StateCommand, nexigen::exec::StateAck>;

/// Serve one TriggerIn request: arbitrate, journal, publish, prepare.
void serve_one_request(TriggerServer& server, nexigen::sm::StateManager& manager,
                       nexigen::sm::StatePublication& publication,
                       nexigen::sm::TransitionPreparation* preparation,
                       nexigen::sm::TransitionAudit& audit, nexigen::sm::SmMetrics& metrics,
                       std::vector<PendingTransition>& pending, Clock::time_point now) {
    auto received = server.receive();
    if (!received || !received->has_value()) {
        return;
    }
    const nexigen::sm::TriggerRequest& request = (*received)->payload();
    const Clock::time_point started = Clock::now();
    const std::size_t index = manager.find_group_index(request.function_group);
    const bool known = index != static_cast<std::size_t>(-1);
    const std::string_view old_state = known ? manager.state(index) : std::string_view{};
    const nexigen::sm::TransitionResult result =
        known ? manager.request_transition(index, request.target_state, request.requester_id)
              : nexigen::sm::TransitionResult::InvalidGraph;
    metrics.arbitration_latency_us.store(
        static_cast<std::uint64_t>(
            std::chrono::duration_cast<std::chrono::microseconds>(Clock::now() - started).count()),
        std::memory_order_relaxed);
    if (result == nexigen::sm::TransitionResult::RejectedByPriority) {
        metrics.rejected_requests_total.fetch_add(1, std::memory_order_relaxed);
    }

    nexigen::sm::TransitionRecord record;
    record.timestamp_ns = static_cast<std::uint64_t>(now.time_since_epoch().count());
    std::snprintf(record.function_group, sizeof(record.function_group), "%s",
                  request.function_group);
    std::snprintf(record.old_state, sizeof(record.old_state), "%s", old_state.data());
    std::snprintf(record.new_state, sizeof(record.new_state), "%s", request.target_state);
    std::snprintf(record.requester_id, sizeof(record.requester_id), "%s", request.requester_id);
    record.result = result;
    audit.record(record);

    (void)(*received)->send_copy(nexigen::sm::TriggerResponse{result});
    if (result == nexigen::sm::TransitionResult::Success) {
        (void)publication.publish(index, manager.state(index));
        if (preparation != nullptr) {
            (void)preparation->notify(manager.state(index));
        }
        const auto timeout = std::chrono::milliseconds(manager.preparation_timeout_ms(index));
        PendingTransition pending_transition;
        pending_transition.group_index = index;
        std::snprintf(pending_transition.target_state, sizeof(pending_transition.target_state),
                      "%s", manager.state(index).data());
        pending_transition.deadline = now + timeout;
        pending.push_back(pending_transition);
    }
}

/// Drive pending transitions: send the final EM command once the preparation
/// deadline elapsed. SafeState commands ride the emergency channel (REQ_EM_007).
void drive_pending(nexigen::sm::StateManager& manager, EmClient& em_client,
                   EmClient& em_emergency_client, nexigen::sm::SmMetrics& metrics,
                   std::vector<PendingTransition>& pending, Clock::time_point now) {
    for (std::size_t i = 0; i < pending.size();) {
        if (now < pending[i].deadline) {
            ++i;
            continue;
        }
        const std::size_t index = pending[i].group_index;
        nexigen::exec::StateCommand command{};
        std::snprintf(command.function_group, sizeof(command.function_group), "%s",
                      manager.group_name(index).data());
        std::snprintf(command.target_state, sizeof(command.target_state), "%s",
                      pending[i].target_state);
        const bool safe_state = std::string_view(pending[i].target_state) == "SafeState";
        const auto started = Clock::now();
        const auto ack =
            safe_state
                ? em_emergency_client.call(
                      command, std::chrono::milliseconds(manager.transition_max_time_ms(index)))
                : em_client.call(command,
                                 std::chrono::milliseconds(manager.transition_max_time_ms(index)));
        const std::uint64_t elapsed_us = static_cast<std::uint64_t>(
            std::chrono::duration_cast<std::chrono::microseconds>(Clock::now() - started).count());
        if (ack.has_value()) {
            metrics.last_transition_duration_us.store(elapsed_us, std::memory_order_relaxed);
        } else {
            // Transition watchdog (REQ_SM_014): force the stalled group into
            // SafeState or Off instead of staying in an intermediate state.
            metrics.transition_timeouts_total.fetch_add(1, std::memory_order_relaxed);
            if (manager.has_state(index, "SafeState")) {
                manager.force_state(index, "SafeState");
            } else if (manager.has_state(index, "Off")) {
                manager.force_state(index, "Off");
            }
        }
        pending.erase(pending.begin() + static_cast<std::ptrdiff_t>(i));
    }
}

/// Every endpoint the arbitration loop touches. Guards are held here so the
/// attachments outlive the loop.
struct Session {
    std::optional<nexigen::com::Node> node{};
    std::optional<nexigen::com::WaitSet> waitset{};
    std::optional<nexigen::sm::StatePublication> publication{};
    std::optional<TriggerServer> trigger_server{};
    std::optional<nexigen::sm::RecoveryHandler> recovery{};
    std::optional<nexigen::com::WaitSetGuard> recovery_guard{};
    std::optional<EmClient> em_client{};
    std::optional<EmClient> em_emergency_client{};
    std::optional<nexigen::phm::CheckpointEmitter> health{};
    std::optional<nexigen::sm::TransitionPreparation> preparation{};
    std::optional<nexigen::com::WaitSetGuard> tick_guard{};
};

/// Load the manifest (REQ_SM_019) and require at least one function group.
[[nodiscard]] bool load_manifest(const Options& options, nexigen::sm::SmManifest& manifest) {
    std::ifstream input(options.config_path);
    if (!input.is_open()) {
        std::fprintf(stderr, "nexigen-sm: cannot open config '%s'\n", options.config_path.c_str());
        return false;
    }
    const nlohmann::json parsed = nlohmann::json::parse(input, nullptr, false);
    if (parsed.is_discarded()) {
        std::fprintf(stderr, "nexigen-sm: config '%s' is not valid JSON\n",
                     options.config_path.c_str());
        return false;
    }
    from_json(parsed, manifest);
    if (manifest.function_groups.empty()) {
        std::fprintf(stderr, "nexigen-sm: config declares no function groups\n");
        return false;
    }
    return true;
}

/// Lock pages (REQ_SM_012), pin to the RT core and apply SCHED_FIFO
/// (REQ_SM_015/016). mlockall is best-effort: unprivileged dev environments may
/// lack CAP_IPC_LOCK, so it only warns.
[[nodiscard]] bool configure_realtime(const Options& options) {
    if (::mlockall(MCL_CURRENT | MCL_FUTURE) != 0) {
        std::fprintf(stderr, "nexigen-sm: warning: mlockall failed (CAP_IPC_LOCK?)\n");
    }

    std::uint64_t rt_mask = options.rt_core_mask;
    if (rt_mask == 0U) {
        const char* env = std::getenv("EASY_SM_RT_CPU_MASK");
        if (env != nullptr) {
            rt_mask = std::strtoull(env, nullptr, 0);
        }
    }
    if (rt_mask != 0U) {
        const auto pinned = nexigen::core::affinity::pin_self(rt_mask);
        if (!pinned) {
            std::fprintf(stderr, "nexigen-sm: core pinning failed\n");
            return false;
        }
    }
    nexigen::core::ThreadSpec spec;
    (void)spec.set_name("sm-arbitration");
    if (rt_mask != 0U) {
        spec.set_cpus(nexigen::core::CpuSet::from_core_mask(rt_mask));
    }
    const auto scheduled =
        spec.set_scheduler(nexigen::core::SchedulingPolicy::FIFO, options.priority);
    if (!scheduled) {
        std::fprintf(stderr, "nexigen-sm: invalid scheduler config\n");
        return false;
    }
    nexigen::core::ThreadApplyReport report;
    const auto applied = nexigen::core::apply_thread_spec_self(spec, report);
    if (!applied) {
        std::fprintf(stderr, "nexigen-sm: applying thread spec failed\n");
        return false;
    }
    return true;
}

/// Node + WaitSet.
[[nodiscard]] bool open_transport(Session& session) {
    auto node = nexigen::com::Node::create();
    if (!node) {
        std::fprintf(stderr, "nexigen-sm: node creation failed\n");
        return false;
    }
    session.node.emplace(std::move(*node));

    auto waitset = nexigen::com::WaitSet::create_without_signal_handling();
    if (!waitset) {
        std::fprintf(stderr, "nexigen-sm: waitset creation failed\n");
        return false;
    }
    session.waitset.emplace(std::move(*waitset));
    return true;
}

/// State publication, TriggerIn server, and the PHM recovery channel.
[[nodiscard]] bool open_state_channels(Session& session, nexigen::sm::StateManager& manager,
                                       const nexigen::sm::SmManifest& manifest) {
    auto publication = nexigen::sm::StatePublication::create(*session.node, manifest);
    if (!publication) {
        std::fprintf(stderr, "nexigen-sm: state publication failed\n");
        return false;
    }
    session.publication.emplace(std::move(*publication));

    const auto trigger_name = nexigen::com::ServiceName::create("Nexigen/SM/TriggerIn");
    if (!trigger_name) {
        std::fprintf(stderr, "nexigen-sm: trigger server creation failed\n");
        return false;
    }
    auto trigger_server = TriggerServer::create(*session.node, *trigger_name);
    if (!trigger_server) {
        std::fprintf(stderr, "nexigen-sm: trigger server creation failed\n");
        return false;
    }
    session.trigger_server.emplace(std::move(*trigger_server));

    const auto recovery_channel = nexigen::com::ServiceName::create("Nexigen/SM/Recovery");
    const auto recovery_event = nexigen::com::ServiceName::create("Nexigen/SM/RecoveryEvent");
    if (!recovery_channel || !recovery_event) {
        std::fprintf(stderr, "nexigen-sm: recovery handler creation failed\n");
        return false;
    }
    auto recovery = nexigen::sm::RecoveryHandler::create(*session.node, *recovery_channel,
                                                         *recovery_event, manager);
    if (!recovery) {
        std::fprintf(stderr, "nexigen-sm: recovery handler creation failed\n");
        return false;
    }
    session.recovery.emplace(std::move(*recovery));
    auto recovery_guard = session.waitset->attach_notification(session.recovery->listener());
    if (!recovery_guard) {
        std::fprintf(stderr, "nexigen-sm: recovery attach failed\n");
        return false;
    }
    session.recovery_guard.emplace(std::move(*recovery_guard));
    return true;
}

/// Optional EM/PHM channels plus the flush tick. A failed optional client stays
/// absent, exactly like the pre-refactor Result it replaces.
void open_control_channels(Session& session, const nexigen::sm::SmManifest& manifest) {
    const auto em_channel = nexigen::com::ServiceName::create("Nexigen/EM/StateCommand");
    const auto em_emergency_channel =
        nexigen::com::ServiceName::create("Nexigen/EM/StateCommandEmergency");
    if (em_channel) {
        auto client = EmClient::create(*session.node, *em_channel);
        if (client) {
            session.em_client.emplace(std::move(*client));
        }
    }
    if (em_emergency_channel) {
        auto client = EmClient::create(*session.node, *em_emergency_channel);
        if (client) {
            session.em_emergency_client.emplace(std::move(*client));
        }
    }

    const auto health_name = nexigen::com::ServiceName::create("Nexigen/SM/Health");
    if (health_name) {
        auto health = nexigen::phm::CheckpointEmitter::create(*session.node, *health_name);
        if (health) {
            session.health.emplace(std::move(*health));
        }
    }

    const auto prep_name = nexigen::com::ServiceName::create("Nexigen/SM/Preparation");
    if (prep_name) {
        auto preparation = nexigen::sm::TransitionPreparation::create(*session.node, *prep_name);
        if (preparation) {
            session.preparation.emplace(std::move(*preparation));
        }
    }

    const auto tick_interval = std::chrono::milliseconds(
        manifest.metrics_flush_interval_ms == 0U ? 100U : manifest.metrics_flush_interval_ms);
    auto tick_guard = session.waitset->attach_interval(tick_interval);
    if (!tick_guard) {
        std::fprintf(stderr, "nexigen-sm: interval attach failed\n");
        return;
    }
    session.tick_guard.emplace(std::move(*tick_guard));
}

/// Arbitration loop (REQ_SM_013): block in the kernel, wake on the recovery
/// event or the flush tick, and exit when the stop handler fires.
void run_loop(Session& session, nexigen::sm::StateManager& manager,
              nexigen::sm::TransitionAudit& audit, nexigen::sm::SmMetrics& metrics,
              std::vector<PendingTransition>& pending) {
    while (g_stop == 0) {
        const auto waited = session.waitset->wait_and_process_once_with_timeout(
            [&](nexigen::com::WaitSetAttachmentId attachment) -> nexigen::com::CallbackProgression {
                const Clock::time_point now = Clock::now();
                if (attachment.has_event_from(*session.recovery_guard)) {
                    (void)session.recovery->listener().try_wait([](nexigen::com::EventId) {});
                    session.recovery->handle_pending();
                    metrics.emergency_transitions_total.fetch_add(1, std::memory_order_relaxed);
                }
                if (attachment.has_event_from(*session.tick_guard)) {
                    for (std::size_t i = 0; i < manager.group_count() * 2U; ++i) {
                        serve_one_request(*session.trigger_server, manager, *session.publication,
                                          session.preparation ? &*session.preparation : nullptr,
                                          audit, metrics, pending, now);
                    }
                    if (session.em_client && session.em_emergency_client) {
                        drive_pending(manager, *session.em_client, *session.em_emergency_client,
                                      metrics, pending, now);
                    }
                    if (session.health) {
                        (void)session.health->emit(1);
                    }
                }
                return nexigen::com::CallbackProgression::Continue;
            },
            std::chrono::milliseconds(200));
        (void)waited;
    }
}

[[nodiscard]] int run_sm(const Options& options) {
    nexigen::sm::SmManifest manifest;
    if (!load_manifest(options, manifest)) {
        return 1;
    }
    if (!configure_realtime(options)) {
        return 1;
    }

    Session session;
    if (!open_transport(session)) {
        return 1;
    }

    nexigen::sm::StateManager manager(manifest);
    if (!open_state_channels(session, manager, manifest)) {
        return 1;
    }
    open_control_channels(session, manifest);

    const nexigen::core::ThreadSpec audit_spec =
        nexigen::core::effective_thread_spec(manifest.audit_thread, manifest.bg_thread);
    nexigen::sm::TransitionAudit audit(options.audit_log, audit_spec);
    if (!audit.startup_status()) {
        std::fprintf(stderr, "nexigen-sm: audit thread not started (%s)\n",
                     std::string(nexigen::core::to_string(audit.startup_status().error())).c_str());
        return 1;
    }
    nexigen::sm::SmMetrics metrics;
    std::vector<PendingTransition> pending;
    pending.reserve(manifest.function_groups.size());

    std::signal(SIGINT, stop_handler);
    std::signal(SIGTERM, stop_handler);

    // Startup sequencing + readiness report within startup_timeout
    // (REQ_SM_001/005).
    (void)manager.advance_startup();
    (void)manager.confirm_startup_step();
    std::printf("nexigen-sm: ready (kRunning), supervising %zu function group(s)\n",
                manager.group_count());
    std::fflush(stdout);

    run_loop(session, manager, audit, metrics, pending);

    // Shutdown sequencing (REQ_SM_005).
    manager.begin_shutdown();
    std::printf("nexigen-sm: stopping\n");
    return 0;
}

} // namespace

auto main(int argc, char* argv[]) -> int {
    Options options;
    const std::optional<int> parsed = parse_args(argc, argv, options);
    if (parsed.has_value()) {
        return *parsed;
    }
    return run_sm(options);
}
