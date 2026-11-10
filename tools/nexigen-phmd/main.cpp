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
    std::printf(
        "nexigen-phmd - Platform Health Management daemon\n"
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

[[nodiscard]] int run_phmd(const Options& options) {
    // 1. Manifest.
    std::ifstream input(options.manifest_path);
    if (!input.is_open()) {
        std::fprintf(stderr, "nexigen-phmd: cannot open manifest '%s'\n",
                     options.manifest_path.c_str());
        return 1;
    }
    const nlohmann::json parsed = nlohmann::json::parse(input, nullptr, false);
    if (parsed.is_discarded()) {
        std::fprintf(stderr, "nexigen-phmd: manifest '%s' is not valid JSON\n",
                     options.manifest_path.c_str());
        return 1;
    }
    nexigen::phm::PhmManifest manifest;
    from_json(parsed, manifest);
    if (manifest.processes.empty()) {
        std::fprintf(stderr, "nexigen-phmd: manifest declares no processes\n");
        return 1;
    }

    // 2. Core isolation + SCHED_FIFO (REQ_PHM_009).
    if (options.core_mask != 0U) {
        const auto pinned = nexigen::core::affinity::pin_self(options.core_mask);
        if (!pinned) {
            std::fprintf(stderr, "nexigen-phmd: core pinning failed\n");
            return 1;
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
        return 1;
    }
    nexigen::core::ThreadApplyReport report;
    const auto applied = nexigen::core::apply_thread_spec_self(spec, report);
    if (!applied) {
        std::fprintf(stderr, "nexigen-phmd: applying thread spec failed\n");
        return 1;
    }

    // 3. Transport: one Node, one WaitSet, one listener per process.
    auto node = nexigen::com::Node::create();
    if (!node) {
        std::fprintf(stderr, "nexigen-phmd: node creation failed\n");
        return 1;
    }
    auto waitset = nexigen::com::WaitSet::create_without_signal_handling();
    if (!waitset) {
        std::fprintf(stderr, "nexigen-phmd: waitset creation failed\n");
        return 1;
    }

    std::vector<MonitoredProcess> monitored;
    std::uint64_t min_cycle_ms = 0;
    for (std::size_t i = 0; i < manifest.processes.size(); ++i) {
        const auto name =
            nexigen::com::ServiceName::create(manifest.processes[i].service_name.c_str());
        if (!name) {
            std::fprintf(stderr, "nexigen-phmd: bad service name for '%s'\n",
                         manifest.processes[i].name.c_str());
            return 1;
        }
        auto listener = nexigen::com::EventListener::create(*node, *name);
        if (!listener) {
            std::fprintf(stderr, "nexigen-phmd: cannot open channel for '%s'\n",
                         manifest.processes[i].name.c_str());
            return 1;
        }
        auto guard = waitset->attach_notification(*listener);
        if (!guard) {
            std::fprintf(stderr, "nexigen-phmd: cannot attach channel for '%s'\n",
                         manifest.processes[i].name.c_str());
            return 1;
        }
        const std::uint64_t cycle = manifest.processes[i].alive_reference_cycle_ms;
        if (min_cycle_ms == 0U || cycle < min_cycle_ms) {
            min_cycle_ms = cycle;
        }
        // The attachment id must be captured BEFORE the guard moves.
        auto attachment = nexigen::com::WaitSetAttachmentId::from_guard(*guard);
        MonitoredProcess entry{i, std::move(*listener), std::move(*guard),
                               std::move(attachment)};
        monitored.push_back(std::move(entry));
    }

    // One shared alive tick at the shortest configured cycle.
    const auto tick_interval = std::chrono::milliseconds(
        min_cycle_ms == 0U ? 100U : min_cycle_ms);
    auto tick_guard = waitset->attach_interval(tick_interval);
    if (!tick_guard) {
        std::fprintf(stderr, "nexigen-phmd: cannot attach alive tick\n");
        return 1;
    }

    nexigen::phm::Supervisor supervisor(manifest);
    nexigen::phm::FaultStorage faults(options.fault_log);
    std::vector<nexigen::phm::SupervisionStatus> seen(manifest.processes.size(),
                                                      nexigen::phm::SupervisionStatus::Ok);

    const auto recovery_name =
        nexigen::com::ServiceName::create("Nexigen/PHM/Recovery");
    auto recovery =
        recovery_name ? nexigen::phm::RecoveryClient::create(*node, *recovery_name)
                      : nexigen::com::Result<nexigen::phm::RecoveryClient>{
                            std::unexpected(nexigen::com::Error::InvalidArgument)};

    std::signal(SIGINT, stop_handler);
    std::signal(SIGTERM, stop_handler);
    std::printf("nexigen-phmd: supervising %zu process(es)\n", manifest.processes.size());
    std::fflush(stdout);

    // 4. Monitoring loop (REQ_PHM_012): sleep in the kernel, wake on a
    // checkpoint or on the alive tick.
    while (g_stop == 0) {
        const auto waited = waitset->wait_and_process_once_with_timeout(
            [&](nexigen::com::WaitSetAttachmentId attachment) -> nexigen::com::CallbackProgression {
                const auto now = nexigen::phm::Supervisor::Clock::now();
                if (attachment.has_event_from(*tick_guard)) {
                    for (std::size_t i = 0; i < monitored.size(); ++i) {
                        supervisor.on_alive_tick(i, now);
                    }
                }
                for (MonitoredProcess& process : monitored) {
                    if (attachment.has_event_from(process.guard)) {
                        const auto drained =
                            process.listener.try_wait([&](nexigen::com::EventId id) {
                                supervisor.on_checkpoint(
                                    process.index,
                                    static_cast<std::uint32_t>(id.as_value()), now);
                            });
                        (void)drained;
                    }
                }
                return nexigen::com::CallbackProgression::Continue;
            },
            std::chrono::milliseconds(200));

        if (!waited && waited.error() != nexigen::com::Error::InvalidArgument) {
            // Empty-set/NoAttachments is impossible here (tick attached);
            // any other run error is a stop condition.
            std::fprintf(stderr, "nexigen-phmd: wait failed\n");
        }

        // 5. Recovery: react to a fresh transition into Expired.
        for (std::size_t i = 0; i < manifest.processes.size(); ++i) {
            const nexigen::phm::SupervisionStatus status = supervisor.status(i);
            if (status == nexigen::phm::SupervisionStatus::Expired && seen[i] != status) {
                const auto now = nexigen::phm::Supervisor::Clock::now();
                const auto timestamp =
                    static_cast<std::uint64_t>(now.time_since_epoch().count());
                faults.record(nexigen::phm::FaultRecord{nexigen::phm::FaultKind::AliveFailure,
                                                        static_cast<std::uint32_t>(i), 0U,
                                                        timestamp});
                if (recovery) {
                    (void)recovery->apply_rule(manifest.processes[i],
                                               std::chrono::milliseconds(500));
                }
                std::fprintf(stderr, "nexigen-phmd: '%s' expired -> recovery\n",
                             manifest.processes[i].name.c_str());
            }
            seen[i] = status;
        }
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
