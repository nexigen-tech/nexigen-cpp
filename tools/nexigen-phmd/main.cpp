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
#include <nexigen/com/service_name.hpp>
#include <nexigen/com/waitset.hpp>
#include <nexigen/core/affinity.hpp>
#include <nexigen/core/thread_management.hpp>
#include <nexigen/phm/fault_kind.hpp>
#include <nexigen/phm/fault_storage.hpp>
#include <nexigen/phm/phm_manifest.hpp>
#include <nexigen/phm/recovery.hpp>
#include <nexigen/phm/supervisor.hpp>

namespace {

volatile std::sig_atomic_t g_stop = 0;

void stop_handler(int /*signal*/) {
    g_stop = 1;
}

void print_usage() {
    std::printf("nexigen-phmd - Platform Health Management daemon\n"
                "Usage: nexigen-phmd --manifest <path> [--core <mask>] [--priority <n>]\n"
                "                    [--fault-log <path>] [--help]\n");
}

struct Options {
    std::string manifest_path{};
    std::uint64_t core_mask{0};
    std::int32_t priority{80};
    std::string fault_log{"/tmp/nexigen-phmd-faults.log"};
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
        if (argument == "--manifest") {
            if (!take_value(argc, argv, index, options.manifest_path)) {
                return 2;
            }
            continue;
        }
        if (argument == "--core") {
            std::string text;
            if (!take_value(argc, argv, index, text)) {
                return 2;
            }
            options.core_mask = std::strtoull(text.c_str(), nullptr, 0);
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
        if (argument == "--fault-log") {
            if (!take_value(argc, argv, index, options.fault_log)) {
                return 2;
            }
            continue;
        }
        std::fprintf(stderr, "nexigen-phmd: unknown argument '%s'\n", argument.c_str());
        return 2;
    }
    if (options.manifest_path.empty()) {
        std::fprintf(stderr, "nexigen-phmd: --manifest <path> is required\n");
        return 2;
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
};

/// Load the PHM manifest and require at least one supervised process.
[[nodiscard]] bool load_manifest(const Options& options, nexigen::phm::PhmManifest& manifest) {
    std::ifstream input(options.manifest_path);
    if (!input.is_open()) {
        std::fprintf(stderr, "nexigen-phmd: cannot open manifest '%s'\n",
                     options.manifest_path.c_str());
        return false;
    }
    const nlohmann::json parsed = nlohmann::json::parse(input, nullptr, false);
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

/// Dispatch one WaitSet notification: alive tick or per-process checkpoints.
void handle_attachment(Session& session, nexigen::phm::Supervisor& supervisor,
                       const nexigen::com::WaitSetAttachmentId& attachment) {
    const auto now = nexigen::phm::Supervisor::Clock::now();
    if (attachment.has_event_from(*session.tick_guard)) {
        for (std::size_t i = 0; i < session.monitored.size(); ++i) {
            supervisor.on_alive_tick(i, now);
        }
    }
    for (MonitoredProcess& process : session.monitored) {
        if (attachment.has_event_from(process.guard)) {
            const auto drained = process.listener.try_wait([&](nexigen::com::EventId id) {
                supervisor.on_checkpoint(process.index, static_cast<std::uint32_t>(id.as_value()),
                                         now);
            });
            (void)drained;
        }
    }
}

/// React to a fresh transition into Expired: journal the fault and apply the
/// configured recovery rule.
void apply_recovery(Session& session, nexigen::phm::Supervisor& supervisor,
                    nexigen::phm::FaultStorage& faults, const nexigen::phm::PhmManifest& manifest,
                    std::vector<nexigen::phm::SupervisionStatus>& seen) {
    for (std::size_t i = 0; i < manifest.processes.size(); ++i) {
        const nexigen::phm::SupervisionStatus status = supervisor.status(i);
        if (status == nexigen::phm::SupervisionStatus::Expired && seen[i] != status) {
            const auto now = nexigen::phm::Supervisor::Clock::now();
            const auto timestamp = static_cast<std::uint64_t>(now.time_since_epoch().count());
            faults.record(nexigen::phm::FaultRecord{nexigen::phm::FaultKind::AliveFailure,
                                                    static_cast<std::uint32_t>(i), 0U, timestamp});
            if (session.recovery) {
                (void)session.recovery->apply_rule(manifest.processes[i],
                                                   std::chrono::milliseconds(500));
            }
            std::fprintf(stderr, "nexigen-phmd: '%s' expired -> recovery\n",
                         manifest.processes[i].name.c_str());
        }
        seen[i] = status;
    }
}

/// Monitoring loop (REQ_PHM_012): sleep in the kernel, wake on a checkpoint or
/// on the alive tick.
void run_loop(Session& session, nexigen::phm::Supervisor& supervisor,
              nexigen::phm::FaultStorage& faults, const nexigen::phm::PhmManifest& manifest,
              std::vector<nexigen::phm::SupervisionStatus>& seen) {
    while (g_stop == 0) {
        const auto waited = session.waitset->wait_and_process_once_with_timeout(
            [&](nexigen::com::WaitSetAttachmentId attachment) -> nexigen::com::CallbackProgression {
                handle_attachment(session, supervisor, attachment);
                return nexigen::com::CallbackProgression::Continue;
            },
            std::chrono::milliseconds(200));

        if (!waited && waited.error() != nexigen::com::Error::InvalidArgument) {
            // Empty-set/NoAttachments is impossible here (tick attached);
            // any other run error is a stop condition.
            std::fprintf(stderr, "nexigen-phmd: wait failed\n");
        }

        apply_recovery(session, supervisor, faults, manifest, seen);
    }
}

[[nodiscard]] int run_phmd(const Options& options) {
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

    open_recovery(session);

    std::signal(SIGINT, stop_handler);
    std::signal(SIGTERM, stop_handler);
    std::printf("nexigen-phmd: supervising %zu process(es)\n", manifest.processes.size());
    std::fflush(stdout);

    run_loop(session, supervisor, faults, manifest, seen);

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
