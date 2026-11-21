// nexigen-phmd — Platform Health Management daemon (change add-phm;
// spec phm/supervision + phm/recovery; design D1/D6).
//
// Loads a JSON health manifest, pins itself to the configured infrastructure
// core under SCHED_FIFO, and runs one WaitSet loop that multiplexes every
// monitored process's checkpoint channel plus a periodic alive tick. On an
// Expired status the daemon records a fault and applies the process's
// configured recovery rule through the PHM→EM recovery channel.
//
// Usage: nexigen-phmd --manifest <path> [--core <mask>] [--priority <n>]
//                    [--fault-log <path>] [--help]
#include <csignal>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <optional>
#include <string>
#include <vector>

#include <nlohmann/json.hpp>

#include <nexigen/com/event.hpp>
#include <nexigen/com/node.hpp>
#include <nexigen/com/publisher.hpp>
#include <nexigen/com/service_name.hpp>
#include <nexigen/com/subscriber.hpp>
#include <nexigen/com/waitset.hpp>
#include <nexigen/core/affinity.hpp>
#include <nexigen/core/integrity/verified_file.hpp>
#include <nexigen/core/settings_io.hpp>
#include <nexigen/core/thread_management.hpp>
#include <nexigen/exec/application_state.hpp>
#include <nexigen/exec/process_lifecycle.hpp>
#include <nexigen/phm/checkpoint_claim.hpp>
#include <nexigen/phm/claim_registry.hpp>
#include <nexigen/phm/em_lifecycle.hpp>
#include <nexigen/phm/fault_kind.hpp>
#include <nexigen/phm/fault_log_verify.hpp>
#include <nexigen/phm/fault_storage.hpp>
#include <nexigen/phm/phm_manifest.hpp>
#include <nexigen/phm/posix_watchdog_device.hpp>
#include <nexigen/phm/recovery.hpp>
#include <nexigen/phm/recovery_ack.hpp>
#include <nexigen/phm/recovery_ack_tracker.hpp>
#include <nexigen/phm/security_event.hpp>
#include <nexigen/phm/security_event_kind.hpp>
#include <nexigen/phm/supervisor.hpp>
#include <nexigen/phm/watchdog.hpp>

namespace {

volatile std::sig_atomic_t g_stop = 0;

void stop_handler(int /*signal*/) {
    g_stop = 1;
}

void print_usage() {
    std::printf("nexigen-phmd - Platform Health Management daemon\n"
                "Usage: nexigen-phmd --manifest <path> [--manifest-sha256 <hex>]\n"
                "                    [--core <mask>] [--priority <n>] [--fault-log <path>]\n"
                "       nexigen-phmd --verify-fault-log <path>\n"
                "                    [--help]\n");
}

struct Options {
    std::string manifest_path{};
    std::string manifest_sha256{};
    std::string verify_fault_log{};
    std::uint64_t core_mask{0};
    std::int32_t priority{80};
    std::string fault_log{"/tmp/nexigen-phmd-faults.log"};
    std::string app_name{};
};

[[nodiscard]] bool take_value(int argc, char* argv[], int& index, std::string& target) {
    if (index + 1 >= argc) {
        return false;
    }
    target = argv[++index];
    return true;
}

/// @brief Handle the repeated `--flag <value>` string options.
[[nodiscard]] bool take_string_option(std::string_view argument, int argc, char* argv[], int& index,
                                      Options& options) {
    std::string* target = nullptr;
    if (argument == "--manifest") {
        target = &options.manifest_path;
    } else if (argument == "--manifest-sha256") {
        target = &options.manifest_sha256;
    } else if (argument == "--verify-fault-log") {
        target = &options.verify_fault_log;
    } else if (argument == "--fault-log") {
        target = &options.fault_log;
    } else if (argument == "--app-name") {
        target = &options.app_name;
    }
    return target != nullptr && take_value(argc, argv, index, *target);
}

enum class OptionScan { Handled, MissingValue, Unknown };

/// @brief Consume the numeric options; leaves `index` on the value.
[[nodiscard]] OptionScan scan_numeric_option(Options& options, std::string_view argument, int argc,
                                             char* argv[], int& index) {
    std::string text;
    if (argument == "--core") {
        if (!take_value(argc, argv, index, text)) {
            return OptionScan::MissingValue;
        }
        options.core_mask = std::strtoull(text.c_str(), nullptr, 0);
        return OptionScan::Handled;
    }
    if (argument == "--priority") {
        if (!take_value(argc, argv, index, text)) {
            return OptionScan::MissingValue;
        }
        options.priority = static_cast<std::int32_t>(std::strtol(text.c_str(), nullptr, 10));
        return OptionScan::Handled;
    }
    return OptionScan::Unknown;
}

/// @brief Whether `argument` is one of the string options.
[[nodiscard]] bool is_string_option(std::string_view argument) noexcept {
    return argument == "--manifest" || argument == "--manifest-sha256" ||
           argument == "--verify-fault-log" || argument == "--fault-log" ||
           argument == "--app-name";
}

[[nodiscard]] std::optional<int> parse_args(int argc, char* argv[], Options& options) {
    for (int index = 1; index < argc; ++index) {
        const std::string argument = argv[index];
        if (argument == "--help" || argument == "-h") {
            print_usage();
            return 0;
        }
        const OptionScan numeric = scan_numeric_option(options, argument, argc, argv, index);
        if (numeric == OptionScan::Handled) {
            continue;
        }
        if (numeric == OptionScan::MissingValue) {
            return 2;
        }
        if (is_string_option(argument)) {
            if (!take_string_option(argument, argc, argv, index, options)) {
                return 2;
            }
            continue;
        }
        std::fprintf(stderr, "nexigen-phmd: unknown argument '%s'\n", argument.c_str());
        return 2;
    }
    if (options.manifest_path.empty() && options.verify_fault_log.empty()) {
        std::fprintf(stderr, "nexigen-phmd: --manifest <path> or --verify-fault-log <path> is "
                             "required\n");
        return 2;
    }
    if (options.app_name.empty()) {
        if (const char* env = std::getenv("NEXIGEN_APP_NAME"); env != nullptr) {
            options.app_name = env;
        }
    }
    return std::nullopt;
}

// One monitored process: its checkpoint listener attached to the WaitSet.
struct MonitoredProcess {
    std::size_t index{0};
    nexigen::com::EventListener listener;
    nexigen::com::WaitSetGuard guard;
    nexigen::com::WaitSetAttachmentId attachment;
};

/// Every endpoint the monitoring loop touches. Guards are owned here so the
/// attachments outlive the loop.
struct Session {
    std::optional<nexigen::com::Node> node{};
    std::optional<nexigen::com::WaitSet> waitset{};
    std::vector<MonitoredProcess> monitored{};
    std::optional<nexigen::com::WaitSetGuard> tick_guard{};
    std::optional<nexigen::phm::RecoveryClient> recovery{};
    std::optional<nexigen::com::Subscriber<nexigen::exec::ProcessLifecycleEvent>>
        lifecycle_subscriber{};
    std::optional<nexigen::com::EventListener> lifecycle_listener{};
    std::optional<nexigen::com::WaitSetGuard> lifecycle_guard{};
    std::optional<nexigen::exec::ApplicationStateReporter> reporter{};
    std::optional<nexigen::com::Subscriber<nexigen::phm::CheckpointClaim>> claim_subscriber{};
    std::optional<nexigen::com::EventListener> claim_listener{};
    std::optional<nexigen::com::WaitSetGuard> claim_guard{};
    std::optional<nexigen::com::Publisher<nexigen::phm::SecurityEvent>> security_publisher{};
    std::optional<nexigen::com::EventNotifier> security_event{};
    std::optional<nexigen::com::Subscriber<nexigen::phm::RecoveryAck>> ack_subscriber{};
    std::optional<nexigen::com::EventListener> ack_listener{};
    std::optional<nexigen::com::WaitSetGuard> ack_guard{};
    /// Declared before the watchdog so the device outlives every feed.
    nexigen::phm::PosixWatchdogDevice watchdog_device{};
    std::optional<nexigen::phm::Watchdog> watchdog{};
};

/// Load the PHM manifest and require at least one supervised process.
[[nodiscard]] bool load_manifest(const Options& options, nexigen::phm::PhmManifest& manifest) {
    std::string text;
    if (options.manifest_sha256.empty()) {
        const auto read = nexigen::core::settings_io::read_text_file(options.manifest_path, text);
        if (read != nexigen::core::settings_io::FileRead::Ok) {
            std::fprintf(stderr, "nexigen-phmd: cannot open manifest '%s'\n",
                         options.manifest_path.c_str());
            return false;
        }
    } else {
        auto verified =
            nexigen::core::integrity::read_verified(options.manifest_path, options.manifest_sha256);
        if (!verified) {
            std::fprintf(stderr, "nexigen-phmd: manifest digest verification failed\n");
            return false;
        }
        text = std::move(*verified);
    }
    const nlohmann::json parsed = nlohmann::json::parse(text, nullptr, false);
    if (parsed.is_discarded()) {
        std::fprintf(stderr, "nexigen-phmd: manifest '%s' is not valid JSON\n",
                     options.manifest_path.c_str());
        return false;
    }
    from_json(parsed, manifest);
    if (manifest.processes.empty()) {
        std::fprintf(stderr, "nexigen-phmd: manifest declares no processes\n");
        return false;
    }
    std::string validation_error;
    if (const auto valid = nexigen::phm::validate_phm_manifest(manifest, validation_error);
        !valid) {
        std::fprintf(stderr, "nexigen-phmd: invalid manifest: %s\n", validation_error.c_str());
        return false;
    }
    return true;
}

/// Core isolation + SCHED_FIFO (REQ_PHM_009).
[[nodiscard]] bool configure_realtime(const Options& options) {
    if (options.core_mask != 0U) {
        const auto pinned = nexigen::core::affinity::pin_self(options.core_mask);
        if (!pinned) {
            std::fprintf(stderr, "nexigen-phmd: core pinning failed\n");
            return false;
        }
    }
    nexigen::core::ThreadSpec spec;
    (void)spec.set_name("phm-monitor");
    if (options.core_mask != 0U) {
        spec.set_cpus(nexigen::core::CpuSet::from_core_mask(options.core_mask));
    }
    const auto scheduled =
        spec.set_scheduler(nexigen::core::SchedulingPolicy::FIFO, options.priority);
    if (!scheduled) {
        std::fprintf(stderr, "nexigen-phmd: invalid scheduler config\n");
        return false;
    }
    nexigen::core::ThreadApplyReport report;
    const auto applied = nexigen::core::apply_thread_spec_self(spec, report);
    if (!applied) {
        std::fprintf(stderr, "nexigen-phmd: applying thread spec failed\n");
        return false;
    }
    return true;
}

/// One Node and one WaitSet shared by every monitored channel.
[[nodiscard]] bool open_transport(Session& session) {
    auto node = nexigen::com::Node::create();
    if (!node) {
        std::fprintf(stderr, "nexigen-phmd: node creation failed\n");
        return false;
    }
    session.node.emplace(std::move(*node));

    auto waitset = nexigen::com::WaitSet::create_without_signal_handling();
    if (!waitset) {
        std::fprintf(stderr, "nexigen-phmd: waitset creation failed\n");
        return false;
    }
    session.waitset.emplace(std::move(*waitset));
    return true;
}

/// Attach one listener per supervised process and track the shortest cycle.
[[nodiscard]] bool attach_processes(Session& session, const nexigen::phm::PhmManifest& manifest,
                                    std::uint64_t& min_cycle_ms) {
    for (std::size_t i = 0; i < manifest.processes.size(); ++i) {
        const auto name =
            nexigen::com::ServiceName::create(manifest.processes[i].service_name.c_str());
        if (!name) {
            std::fprintf(stderr, "nexigen-phmd: bad service name for '%s'\n",
                         manifest.processes[i].name.c_str());
            return false;
        }
        auto listener = nexigen::com::EventListener::create(*session.node, *name);
        if (!listener) {
            std::fprintf(stderr, "nexigen-phmd: cannot open channel for '%s'\n",
                         manifest.processes[i].name.c_str());
            return false;
        }
        auto guard = session.waitset->attach_notification(*listener);
        if (!guard) {
            std::fprintf(stderr, "nexigen-phmd: cannot attach channel for '%s'\n",
                         manifest.processes[i].name.c_str());
            return false;
        }
        const std::uint64_t cycle = manifest.processes[i].alive_reference_cycle_ms;
        if (min_cycle_ms == 0U || cycle < min_cycle_ms) {
            min_cycle_ms = cycle;
        }
        // The attachment id must be captured BEFORE the guard moves.
        auto attachment = nexigen::com::WaitSetAttachmentId::from_guard(*guard);
        MonitoredProcess entry{i, std::move(*listener), std::move(*guard), std::move(attachment)};
        session.monitored.push_back(std::move(entry));
    }
    return true;
}

/// One shared alive tick at the shortest configured cycle.
[[nodiscard]] bool attach_alive_tick(Session& session, std::uint64_t min_cycle_ms) {
    const auto tick_interval = std::chrono::milliseconds(min_cycle_ms == 0U ? 100U : min_cycle_ms);
    auto tick_guard = session.waitset->attach_interval(tick_interval);
    if (!tick_guard) {
        std::fprintf(stderr, "nexigen-phmd: cannot attach alive tick\n");
        return false;
    }
    session.tick_guard.emplace(std::move(*tick_guard));
    return true;
}

/// Optional recovery channel toward the SM; a failed creation leaves it absent.
void open_recovery(Session& session) {
    const auto recovery_name = nexigen::com::ServiceName::create("Nexigen/PHM/Recovery");
    const auto sm_notification_name = nexigen::com::ServiceName::create("Nexigen/SM/Recovery");
    const auto sm_event_name = nexigen::com::ServiceName::create("Nexigen/SM/RecoveryEvent");
    if (!recovery_name || !sm_notification_name || !sm_event_name) {
        return;
    }
    auto recovery = nexigen::phm::RecoveryClient::create(*session.node, *recovery_name,
                                                         *sm_notification_name, *sm_event_name);
    if (recovery) {
        session.recovery.emplace(std::move(*recovery));
    }
}

/// Open the State Manager acknowledgement channel; absence disables ack
/// matching, so every escalation times out into the watchdog path.
void open_ack(Session& session) {
    const auto sample_name = nexigen::com::ServiceName::create("Nexigen/SM/RecoveryAck");
    const auto event_name = nexigen::com::ServiceName::create("Nexigen/SM/RecoveryAckEvent");
    if (!sample_name || !event_name) {
        return;
    }
    auto subscriber = nexigen::com::Subscriber<nexigen::phm::RecoveryAck>::create(
        *session.node, *sample_name, 64, 64);
    auto listener = nexigen::com::EventListener::create(*session.node, *event_name);
    if (!subscriber || !listener) {
        return;
    }
    auto guard = session.waitset->attach_notification(*listener);
    if (!guard) {
        return;
    }
    session.ack_subscriber.emplace(std::move(*subscriber));
    session.ack_listener.emplace(std::move(*listener));
    session.ack_guard.emplace(std::move(*guard));
}

/// Drain acknowledgements and stop feeding when one reports a refusal.
void drain_acks(Session& session, nexigen::phm::RecoveryAckTracker& pending,
                bool& watchdog_reaction) {
    if (!session.ack_subscriber) {
        return;
    }
    for (;;) {
        auto received = session.ack_subscriber->receive();
        if (!received || !received->has_value()) {
            return;
        }
        const nexigen::phm::RecoveryAck& ack = (*received)->payload();
        const auto decision = pending.on_ack(ack);
        if (!decision.matched) {
            continue; // Stale acknowledgement for a recovery we no longer track.
        }
        if (decision.watchdog_reaction) {
            watchdog_reaction = true;
            std::fprintf(stderr, "nexigen-phmd: recovery rejected (%s) -> watchdog\n",
                         std::string(nexigen::phm::to_string(ack.reason)).c_str());
        }
    }
}

/// Expire every acknowledgement whose budget elapsed.
void expire_recoveries(nexigen::phm::RecoveryAckTracker& pending,
                       nexigen::phm::Supervisor::Clock::time_point now, bool& watchdog_reaction) {
    if (pending.expire(now)) {
        watchdog_reaction = true;
        std::fprintf(stderr, "nexigen-phmd: recovery not acknowledged -> watchdog\n");
    }
}

/// Optional EM lifecycle channel; absent when the transport cannot open it.
void open_lifecycle(Session& session) {
    const auto sample_name = nexigen::com::ServiceName::create("Nexigen/PHM/ExecutionState");
    const auto event_name = nexigen::com::ServiceName::create("Nexigen/PHM/ExecutionStateEvent");
    if (!sample_name || !event_name) {
        return;
    }
    auto subscriber = nexigen::com::Subscriber<nexigen::exec::ProcessLifecycleEvent>::create(
        *session.node, *sample_name, 64, 64);
    auto listener = nexigen::com::EventListener::create(*session.node, *event_name);
    if (!subscriber || !listener) {
        return;
    }
    auto guard = session.waitset->attach_notification(*listener);
    if (!guard) {
        return;
    }
    session.lifecycle_subscriber.emplace(std::move(*subscriber));
    session.lifecycle_listener.emplace(std::move(*listener));
    session.lifecycle_guard.emplace(std::move(*guard));
}

/// Map the EM vocabulary onto the supervisor vocabulary.
[[nodiscard]] nexigen::phm::EmLifecycleState
to_phm_lifecycle(nexigen::exec::ProcessLifecycleState state) noexcept {
    using nexigen::exec::ProcessLifecycleState;
    switch (state) {
        case ProcessLifecycleState::Running:     return nexigen::phm::EmLifecycleState::Running;
        case ProcessLifecycleState::Terminating: return nexigen::phm::EmLifecycleState::Terminating;
        case ProcessLifecycleState::Terminated:  return nexigen::phm::EmLifecycleState::Terminated;
        case ProcessLifecycleState::Failed:      return nexigen::phm::EmLifecycleState::Failed;
    }
    return nexigen::phm::EmLifecycleState::Unknown;
}

/// Drain every pending lifecycle sample and apply it by process name.
///
/// A `Failed` sample means the Execution Manager spent the restart budget: the
/// process becomes `Exhausted` and is escalated once, so no further restart is
/// requested for it.
void drain_lifecycle(Session& session, nexigen::phm::Supervisor& supervisor,
                     const nexigen::phm::PhmManifest& manifest,
                     nexigen::phm::ClaimRegistry& registry, nexigen::phm::FaultStorage& faults,
                     nexigen::phm::RecoveryAckTracker& pending, bool& watchdog_reaction) {
    if (!session.lifecycle_subscriber) {
        return;
    }
    for (;;) {
        auto received = session.lifecycle_subscriber->receive();
        if (!received) {
            return;
        }
        if (!received->has_value()) {
            return;
        }
        const nexigen::exec::ProcessLifecycleEvent& event = (*received)->payload();
        const std::string_view app = nexigen::phm::bounded_name(event.app, sizeof(event.app));
        const std::string_view group =
            nexigen::phm::bounded_name(event.function_group, sizeof(event.function_group));
        for (std::size_t i = 0; i < manifest.processes.size(); ++i) {
            if (manifest.processes[i].name != app) {
                continue;
            }
            supervisor.on_execution_state(i, to_phm_lifecycle(event.state), group);
            if (event.state != nexigen::exec::ProcessLifecycleState::Running) {
                // A stopping process ends its incarnation: the next one must
                // present a fresh claim before its checkpoints are trusted.
                registry.reset(i);
            }
            if (event.state == nexigen::exec::ProcessLifecycleState::Failed) {
                const auto now = nexigen::phm::Supervisor::Clock::now();
                const auto timestamp = static_cast<std::uint64_t>(now.time_since_epoch().count());
                faults.record(nexigen::phm::FaultRecord{
                    supervisor.last_fault(i), static_cast<std::uint32_t>(i), 0U, timestamp});
                if (supervisor.function_group(i).empty()) {
                    faults.record(nexigen::phm::FaultRecord{nexigen::phm::FaultKind::UnknownGroup,
                                                            static_cast<std::uint32_t>(i), 0U,
                                                            timestamp});
                    std::fprintf(stderr,
                                 "nexigen-phmd: '%s' exhausted with unknown function group -> "
                                 "watchdog\n",
                                 manifest.processes[i].name.c_str());
                    watchdog_reaction = true;
                } else if (session.recovery) {
                    const auto escalated =
                        session.recovery->notify_sm_recovery(supervisor.function_group(i));
                    std::fprintf(stderr, "nexigen-phmd: '%s' exhausted -> escalation\n",
                                 manifest.processes[i].name.c_str());
                    if (escalated) {
                        const auto deadline =
                            now + std::chrono::milliseconds(manifest.recovery_timeout_ms);
                        static_cast<void>(pending.expect(session.recovery->last_request_id(),
                                                         supervisor.function_group(i), deadline));
                    } else {
                        watchdog_reaction = true;
                    }
                }
            }
            break;
        }
    }
}

/// Open the identity-claim channel and the security-event channel.
void open_claims(Session& session) {
    const auto claim_name = nexigen::com::ServiceName::create("Nexigen/PHM/Claim");
    const auto claim_event_name = nexigen::com::ServiceName::create("Nexigen/PHM/ClaimEvent");
    const auto security_name = nexigen::com::ServiceName::create("Nexigen/PHM/Security");
    const auto security_event_name = nexigen::com::ServiceName::create("Nexigen/PHM/SecurityEvent");
    if (claim_name && claim_event_name) {
        // The publisher keeps the transport default service maximum (2), so the
        // subscriber must not request a larger buffer: the transport rejects a
        // service whose settings disagree. Claims are rare, so the default
        // depth is enough.
        auto subscriber = nexigen::com::Subscriber<nexigen::phm::CheckpointClaim>::create(
            *session.node, *claim_name);
        auto listener = nexigen::com::EventListener::create(*session.node, *claim_event_name);
        if (!subscriber || !listener) {
            std::fprintf(stderr, "nexigen-phmd: identity claim channel unavailable\n");
        } else {
            auto guard = session.waitset->attach_notification(*listener);
            if (!guard) {
                std::fprintf(stderr, "nexigen-phmd: cannot attach the identity claim event\n");
            } else {
                session.claim_subscriber.emplace(std::move(*subscriber));
                session.claim_listener.emplace(std::move(*listener));
                session.claim_guard.emplace(std::move(*guard));
            }
        }
    }
    if (security_name && security_event_name) {
        auto publisher = nexigen::com::Publisher<nexigen::phm::SecurityEvent>::create(
            *session.node, *security_name, 64);
        auto event = nexigen::com::EventNotifier::create(*session.node, *security_event_name);
        if (!publisher || !event) {
            std::fprintf(stderr, "nexigen-phmd: security event channel unavailable\n");
        } else {
            session.security_publisher.emplace(std::move(*publisher));
            session.security_event.emplace(std::move(*event));
        }
    }
}

/// Publish one security finding and wake its consumers.
void publish_security_event(Session& session, nexigen::phm::SecurityEventKind kind,
                            std::string_view name, std::uint32_t pid, std::uint32_t uid) {
    if (!session.security_publisher) {
        return;
    }
    nexigen::phm::SecurityEvent sample{};
    sample.kind = kind;
    std::snprintf(sample.process, sizeof(sample.process), "%.*s", static_cast<int>(name.size()),
                  name.data());
    sample.pid = pid;
    sample.uid = uid;
    if (session.security_publisher->send_copy(sample)) {
        if (session.security_event) {
            static_cast<void>(session.security_event->notify());
        }
    }
}

/// Drain pending claims and verify them against the manifest expectations.
void drain_claims(Session& session, nexigen::phm::ClaimRegistry& registry) {
    if (!session.claim_subscriber) {
        return;
    }
    for (;;) {
        auto received = session.claim_subscriber->receive();
        if (!received) {
            return;
        }
        if (!received->has_value()) {
            return;
        }
        const nexigen::phm::CheckpointClaim& claim = (*received)->payload();
        if (const auto rejection = registry.apply(claim); rejection.has_value()) {
            publish_security_event(session, *rejection,
                                   nexigen::phm::bounded_name(claim.name, sizeof(claim.name)),
                                   claim.pid, claim.uid);
        }
    }
}

/// Dispatch one WaitSet notification: lifecycle, alive tick, or per-process
/// checkpoints.
void handle_attachment(Session& session, nexigen::phm::Supervisor& supervisor,
                       const nexigen::phm::PhmManifest& manifest,
                       const nexigen::com::WaitSetAttachmentId& attachment,
                       nexigen::phm::ClaimRegistry& registry, nexigen::phm::FaultStorage& faults,
                       nexigen::phm::RecoveryAckTracker& pending, bool& watchdog_reaction) {
    const auto now = nexigen::phm::Supervisor::Clock::now();
    if (session.ack_guard && attachment.has_event_from(*session.ack_guard)) {
        drain_acks(session, pending, watchdog_reaction);
    }
    if (session.claim_guard && attachment.has_event_from(*session.claim_guard)) {
        drain_claims(session, registry);
    }
    if (session.lifecycle_guard && attachment.has_event_from(*session.lifecycle_guard)) {
        drain_lifecycle(session, supervisor, manifest, registry, faults, pending,
                        watchdog_reaction);
    }
    if (attachment.has_event_from(*session.tick_guard)) {
        for (std::size_t i = 0; i < session.monitored.size(); ++i) {
            supervisor.on_alive_tick(i, now);
        }
    }
    for (MonitoredProcess& process : session.monitored) {
        if (!attachment.has_event_from(process.guard)) {
            continue;
        }
        if (!registry.accepts(process.index)) {
            // Identity is not proven yet: drain and drop, without feeding the
            // supervisor (spec phm/identity, "Checkpoint gating").
            static_cast<void>(process.listener.try_wait([](nexigen::com::EventId) {}));
            continue;
        }
        const auto drained = process.listener.try_wait([&](nexigen::com::EventId id) {
            supervisor.on_checkpoint(process.index, static_cast<std::uint32_t>(id.as_value()), now);
        });
        (void)drained;
    }
}

/// React to a fresh transition into Expired: journal the fault and apply the
/// configured recovery rule.
void apply_recovery(Session& session, nexigen::phm::Supervisor& supervisor,
                    nexigen::phm::FaultStorage& faults, const nexigen::phm::PhmManifest& manifest,
                    std::vector<nexigen::phm::SupervisionStatus>& seen,
                    nexigen::phm::RecoveryAckTracker& pending, bool& watchdog_reaction) {
    for (std::size_t i = 0; i < manifest.processes.size(); ++i) {
        const nexigen::phm::SupervisionStatus status = supervisor.status(i);
        if (status == nexigen::phm::SupervisionStatus::Expired && seen[i] != status) {
            const auto now = nexigen::phm::Supervisor::Clock::now();
            const auto timestamp = static_cast<std::uint64_t>(now.time_since_epoch().count());
            faults.record(nexigen::phm::FaultRecord{supervisor.last_fault(i),
                                                    static_cast<std::uint32_t>(i), 0U, timestamp});
            const std::string_view group = supervisor.function_group(i);
            if (group.empty()) {
                // Escalating to a process name would be dropped silently by the
                // State Manager, so the fault is journaled and the watchdog path
                // takes over instead.
                faults.record(nexigen::phm::FaultRecord{nexigen::phm::FaultKind::UnknownGroup,
                                                        static_cast<std::uint32_t>(i), 0U,
                                                        timestamp});
                std::fprintf(stderr,
                             "nexigen-phmd: '%s' expired with unknown function group -> "
                             "watchdog\n",
                             manifest.processes[i].name.c_str());
                watchdog_reaction = true;
            } else if (session.recovery) {
                const auto stop_feeding = [&session] {
                    if (session.watchdog) {
                        session.watchdog->stop_feeding();
                    }
                };
                const auto dispatched = session.recovery->apply_rule(
                    manifest.processes[i], group, std::chrono::milliseconds(500), stop_feeding);
                std::fprintf(stderr, "nexigen-phmd: '%s' expired -> recovery dispatched\n",
                             manifest.processes[i].name.c_str());
                if (dispatched && session.recovery->last_action() ==
                                      nexigen::phm::RecoveryAction::SwitchFunctionGroupState) {
                    const auto deadline =
                        now + std::chrono::milliseconds(manifest.recovery_timeout_ms);
                    static_cast<void>(
                        pending.expect(session.recovery->last_request_id(), group, deadline));
                }
            } else {
                watchdog_reaction = true;
            }
        }
        seen[i] = status;
    }
}

/// Monitoring loop (REQ_PHM_012): sleep in the kernel, wake on a checkpoint or
/// on the alive tick.
void run_loop(Session& session, nexigen::phm::Supervisor& supervisor,
              nexigen::phm::FaultStorage& faults, const nexigen::phm::PhmManifest& manifest,
              std::vector<nexigen::phm::SupervisionStatus>& seen,
              nexigen::phm::ClaimRegistry& registry) {
    nexigen::phm::RecoveryAckTracker pending{};
    while (g_stop == 0) {
        bool watchdog_reaction = false;
        const auto waited = session.waitset->wait_and_process_once_with_timeout(
            [&](nexigen::com::WaitSetAttachmentId attachment) -> nexigen::com::CallbackProgression {
                handle_attachment(session, supervisor, manifest, attachment, registry, faults,
                                  pending, watchdog_reaction);
                return nexigen::com::CallbackProgression::Continue;
            },
            std::chrono::milliseconds(200));

        if (!waited && waited.error() != nexigen::com::Error::InvalidArgument) {
            // Empty-set/NoAttachments is impossible here (tick attached);
            // any other run error is a stop condition.
            std::fprintf(stderr, "nexigen-phmd: wait failed\n");
        }

        // The wake events are hints, not the source of truth: a lost event must
        // not delay a lifecycle sample or hide an acknowledgement. Draining is
        // non-blocking and cheap, so every cycle re-reads them.
        drain_acks(session, pending, watchdog_reaction);
        drain_claims(session, registry);
        drain_lifecycle(session, supervisor, manifest, registry, faults, pending,
                        watchdog_reaction);

        apply_recovery(session, supervisor, faults, manifest, seen, pending, watchdog_reaction);
        expire_recoveries(pending, nexigen::phm::Supervisor::Clock::now(), watchdog_reaction);
        if (watchdog_reaction && session.watchdog) {
            session.watchdog->stop_feeding();
        }

        // One feed decision per cycle: healthy only when no armed process is
        // Expired. A suspended (Terminated) process keeps its Ok status.
        bool any_expired = false;
        for (std::size_t i = 0; i < manifest.processes.size(); ++i) {
            if (supervisor.status(i) == nexigen::phm::SupervisionStatus::Expired) {
                any_expired = true;
                break;
            }
        }
        if (session.watchdog) {
            session.watchdog->tick(nexigen::phm::Watchdog::Clock::now(), any_expired);
        }
    }
}

/// Scan a fault log and report the counters; non-zero when a record is corrupt.
[[nodiscard]] int verify_log_mode(const std::string& path) {
    nexigen::phm::FaultLogReport report{};
    const auto scanned = nexigen::phm::verify_fault_log(path, report);
    if (!scanned) {
        std::fprintf(stderr, "nexigen-phmd: cannot scan '%s'\n", path.c_str());
        return 1;
    }
    std::printf("nexigen-phmd: %s\n", report.to_string().c_str());
    return report.corrupted == 0U ? 0 : 1;
}

[[nodiscard]] int run_phmd(const Options& options) {
    if (!options.verify_fault_log.empty()) {
        return verify_log_mode(options.verify_fault_log);
    }
    nexigen::phm::PhmManifest manifest;
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

    std::uint64_t min_cycle_ms = 0;
    if (!attach_processes(session, manifest, min_cycle_ms)) {
        return 1;
    }
    if (!attach_alive_tick(session, min_cycle_ms)) {
        return 1;
    }

    nexigen::phm::Supervisor supervisor(manifest);
    const nexigen::core::ThreadSpec storage_spec =
        nexigen::core::effective_thread_spec(manifest.fault_storage_thread, manifest.bg_thread);
    nexigen::phm::FaultStorage faults(options.fault_log, storage_spec);
    if (!faults.startup_status()) {
        std::fprintf(
            stderr, "nexigen-phmd: fault storage thread not started (%s)\n",
            std::string(nexigen::core::to_string(faults.startup_status().error())).c_str());
        return 1;
    }
    std::vector<nexigen::phm::SupervisionStatus> seen(manifest.processes.size(),
                                                      nexigen::phm::SupervisionStatus::Ok);
    nexigen::phm::ClaimRegistry registry(manifest);

    open_recovery(session);
    open_lifecycle(session);
    open_claims(session);
    open_ack(session);

    auto watchdog = nexigen::phm::Watchdog::create(session.watchdog_device, manifest.watchdog);
    if (!watchdog) {
        std::fprintf(stderr, "nexigen-phmd: cannot open watchdog device '%s'\n",
                     manifest.watchdog.device.empty() ? "/dev/watchdog"
                                                      : manifest.watchdog.device.c_str());
        return 1;
    }
    session.watchdog.emplace(std::move(*watchdog));
    if (session.watchdog->enabled()) {
        std::printf("nexigen-phmd: watchdog device=%s timeout_ms=%u feed_period_ms=%u\n",
                    session.watchdog->device_path().c_str(), session.watchdog->timeout_ms(),
                    manifest.watchdog.feed_period_ms);
        std::fflush(stdout);
    }
    if (!options.app_name.empty()) {
        auto reporter =
            nexigen::exec::ApplicationStateReporter::create(*session.node, options.app_name);
        if (reporter) {
            session.reporter.emplace(std::move(*reporter));
        }
    }

    std::signal(SIGINT, stop_handler);
    std::signal(SIGTERM, stop_handler);
    if (session.reporter) {
        (void)session.reporter->report(nexigen::exec::AppState::Running);
    }
    std::printf("nexigen-phmd: supervising %zu process(es)\n", manifest.processes.size());
    std::fflush(stdout);

    run_loop(session, supervisor, faults, manifest, seen, registry);

    if (session.watchdog) {
        session.watchdog->disarm();
    }
    std::printf("nexigen-phmd: stopping\n");
    return 0;
}

} // namespace

auto main(int argc, char* argv[]) -> int {
    Options options;
    const std::optional<int> parsed = parse_args(argc, argv, options);
    if (parsed.has_value()) {
        return *parsed;
    }
    return run_phmd(options);
}
