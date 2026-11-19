// nexigen-chronod — the platform time-base daemon (change
// add-time-base-service; spec chrono/time-base).
//
// Forms the single monotonic scale: it samples CLOCK_MONOTONIC_RAW, publishes
// the canonical and (when validated) TSC domain rows into a POSIX shared-memory
// block under a seqlock, appends every change to the history stream, and
// refreshes the heartbeat once per interval. It is deliberately built on
// nexigen::chrono directly, never on app::Infrastructure, because applications
// depend on this daemon and must not be part of its bootstrap.
#include <atomic>
#include <chrono>
#include <csignal>
#include <cstdint>
#include <cstdio>
#include <fstream>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <thread>
#include <vector>

#include <sys/mman.h>

#include <nexigen/chrono/daemon_runtime.hpp>
#include <nexigen/chrono/detail/time_base_segment.hpp>
#include <nexigen/chrono/json_file_sink_config.hpp>
#include <nexigen/chrono/null_sink.hpp>
#include <nexigen/chrono/phc_cross_timestamp.hpp>
#include <nexigen/chrono/provider.hpp>
#include <nexigen/chrono/time_base_daemon_config.hpp>
#include <nexigen/chrono/time_base_history_writer.hpp>
#include <nexigen/chrono/time_base_host_domain.hpp>
#include <nexigen/chrono/time_base_publisher.hpp>
#include <nexigen/chrono/timestamp.hpp>
#include <nexigen/recorder/session_writer.hpp>
#include <nexigen/recorder/time_base_session_bridge.hpp>
#include <nexigen/core/result.hpp>
#include <nexigen/core/thread_management.hpp>

namespace {

std::sig_atomic_t g_stop = 0;

void handle_signal(int) {
    g_stop = 1;
}

void print_usage() {
    std::printf("usage: nexigen-chronod [--settings <path>] [--segment <name>] "
                "[--history <path>] [--once]\n"
                "  --settings  daemon configuration JSON (defaults when omitted)\n"
                "  --segment   POSIX shared-memory name (default from configuration)\n"
                "  --history   history JSONL path (default from configuration)\n"
                "  --once      publish one generation and exit (smoke tests)\n");
}

std::string read_boot_uuid() {
    std::ifstream file{"/proc/sys/kernel/random/boot_id"};
    std::string uuid;
    if (file.is_open()) {
        std::getline(file, uuid);
    }
    return uuid;
}

std::uint64_t boot_token_from_uuid(std::string_view uuid) noexcept {
    std::uint64_t token = 0;
    int nibbles = 0;
    for (const char character : uuid) {
        if (character == '-') {
            continue;
        }
        int value = -1;
        if (character >= '0' && character <= '9') {
            value = character - '0';
        } else if (character >= 'a' && character <= 'f') {
            value = character - 'a' + 10;
        } else if (character >= 'A' && character <= 'F') {
            value = character - 'A' + 10;
        } else {
            break;
        }
        token = (token << 4U) | static_cast<std::uint64_t>(value);
        if (++nibbles == 16) {
            break;
        }
    }
    return token;
}

struct Options {
    std::string settings{};
    std::string segment{};
    std::string history{};
    bool once{false};
    bool help{false};
    bool valid{true};
};

Options parse_args(int argc, char** argv) {
    Options options{};
    for (int i = 1; i < argc; ++i) {
        const std::string_view argument = argv[i];
        if (argument == "--help" || argument == "-h") {
            options.help = true;
        } else if (argument == "--once") {
            options.once = true;
        } else if (argument == "--settings" && i + 1 < argc) {
            options.settings = argv[++i];
        } else if (argument == "--segment" && i + 1 < argc) {
            options.segment = argv[++i];
        } else if (argument == "--history" && i + 1 < argc) {
            options.history = argv[++i];
        } else {
            options.valid = false;
        }
    }
    return options;
}

/// @brief Apply the RT thread spec and lock the daemon's memory.
///
/// @param config Daemon configuration carrying the thread specification.
/// @param report Receives the observed runtime report.
/// @return `false` when the thread specification could not be applied.
[[nodiscard]] bool apply_runtime(const nexigen::chrono::TimeBaseDaemonConfig& config,
                                 nexigen::chrono::DaemonRuntimeReport& report) {
    using namespace nexigen;
    chrono::DaemonRuntimeConfig runtime{};
    runtime.thread = config.main_thread;
    runtime.lock_memory = true;
    const core::Status status = chrono::apply_daemon_runtime(runtime, report);
    if (!status) {
        std::fprintf(stderr, "nexigen-chronod: thread spec failed: %s\n",
                     std::string(core::to_string(status.error())).c_str());
        return false;
    }
    if (!report.memory_locked) {
        std::fprintf(stderr, "nexigen-chronod: mlockall failed; continuing\n");
    }
    return true;
}

/// @brief Publish at a fixed cadence until a stop signal arrives.
///
/// @param publish_once Callable performing one publication.
/// @param interval Publication cadence.
template <typename Publish>
void run_loop(Publish& publish_once, std::chrono::nanoseconds interval) {
    ::timespec next{};
    static_cast<void>(::clock_gettime(CLOCK_MONOTONIC, &next));
    const long interval_sec = static_cast<long>(interval.count() / 1'000'000'000LL);
    const long interval_rem = static_cast<long>(interval.count() % 1'000'000'000LL);
    while (g_stop == 0) {
        publish_once();
        next.tv_sec += interval_sec;
        next.tv_nsec += interval_rem;
        while (next.tv_nsec >= 1'000'000'000L) {
            next.tv_nsec -= 1'000'000'000L;
            ++next.tv_sec;
        }
        while (::clock_nanosleep(CLOCK_MONOTONIC, TIMER_ABSTIME, &next, nullptr) == EINTR) {}
    }
}

} // namespace

/// @brief Resolve the daemon configuration from options and a settings file.
///
/// @param options Parsed command line.
/// @param config Destination configuration with defaults pre-filled.
/// @return `0` on success, `1` when the settings file is unreadable.
[[nodiscard]] int load_config(const Options& options,
                              nexigen::chrono::TimeBaseDaemonConfig& config) {
    using namespace nexigen;
    if (!options.settings.empty()) {
        const core::Status loaded = chrono::load_time_base_daemon_config(options.settings, config);
        if (!loaded) {
            std::fprintf(stderr, "nexigen-chronod: settings: %s\n",
                         std::string(core::to_string(loaded.error())).c_str());
            return 1;
        }
    }
    if (!options.segment.empty()) {
        config.segment_name = options.segment;
    }
    if (!options.history.empty()) {
        config.history_path = options.history;
    }
    return 0;
}

/// @brief Stable storage for every backend the publisher borrows.
struct DeviceStorage {
    std::vector<std::unique_ptr<nexigen::chrono::PhcCrossTimestampClock>> phc{};
    std::vector<std::unique_ptr<nexigen::chrono::SimulatedTimeBaseDevice>> simulated{};
    std::vector<std::unique_ptr<nexigen::chrono::RegisterPairDevice>> register_pairs{};
};

/// @brief Open one declared PTP hardware clock and bind it.
///
/// @param publisher Publisher that receives the backend.
/// @param domain Declaration.
/// @param device_id Domain id to assign.
/// @param storage Backend storage.
/// @return `true` when the domain was bound.
[[nodiscard]] bool bind_phc(nexigen::chrono::TimeBasePublisher& publisher,
                            const nexigen::chrono::HardwareDomainConfig& domain,
                            std::uint16_t device_id, DeviceStorage& storage) {
    using namespace nexigen;
    auto clock = chrono::PhcCrossTimestampClock::open(domain.access);
    if (!clock) {
        std::fprintf(stderr, "nexigen-chronod: domain %s: cannot open %s: %s\n",
                     domain.name.c_str(), domain.access.c_str(),
                     std::string(core::to_string(clock.error())).c_str());
        return false;
    }
    storage.phc.push_back(std::make_unique<chrono::PhcCrossTimestampClock>(std::move(*clock)));
    if (!publisher.bind_device(storage.phc.back()->backend(device_id, 1U, domain.name))) {
        std::fprintf(stderr, "nexigen-chronod: domain %s: backend binding failed\n",
                     domain.name.c_str());
        return false;
    }
    return true;
}

/// @brief Create one simulated register-pair domain and bind it.
///
/// @param publisher Publisher that receives the backend.
/// @param domain Declaration.
/// @param device_id Domain id to assign.
/// @param storage Backend storage.
/// @return `true` when the domain was bound.
[[nodiscard]] bool bind_simulated_register_pair(nexigen::chrono::TimeBasePublisher& publisher,
                                                const nexigen::chrono::HardwareDomainConfig& domain,
                                                std::uint16_t device_id, DeviceStorage& storage) {
    using namespace nexigen;
    storage.simulated.push_back(std::make_unique<chrono::SimulatedTimeBaseDevice>(
        device_id, 1U, 1ULL << 32U, static_cast<std::uint64_t>(device_id) * 1'000U, 50U));
    storage.register_pairs.push_back(std::make_unique<chrono::RegisterPairDevice>(
        storage.simulated.back()->register_pair_backend()));
    if (!publisher.bind_device(storage.register_pairs.back()->backend())) {
        std::fprintf(stderr, "nexigen-chronod: domain %s: backend binding failed\n",
                     domain.name.c_str());
        return false;
    }
    return true;
}

/// @brief Bind every declared hardware domain to its own backend.
///
/// PHC domains open their `/dev/ptpN`; register-pair domains bind a simulated
/// D1-D3 device when `simulate` is set and otherwise report that no vendor
/// module is present. Manual-pair domains need a vendor module by definition.
///
/// @param publisher Publisher that receives the backends.
/// @param config Declared domains.
/// @param storage Backend storage; it must outlive the publisher.
/// @return Number of bound domains.
[[nodiscard]] std::uint32_t
bind_declared_devices(nexigen::chrono::TimeBasePublisher& publisher,
                      const nexigen::chrono::TimeBaseDaemonConfig& config, DeviceStorage& storage) {
    using namespace nexigen;
    storage.phc.reserve(config.domains.size());
    storage.simulated.reserve(config.domains.size());
    storage.register_pairs.reserve(config.domains.size());
    std::uint16_t next_device_id = chrono::device_domain_base;
    std::uint32_t bound = 0U;
    for (const chrono::HardwareDomainConfig& domain : config.domains) {
        bool ok = false;
        if (domain.mode == chrono::DeviceAccessMode::PhcCrossTimestamp) {
            ok = bind_phc(publisher, domain, next_device_id, storage);
        } else if (domain.mode == chrono::DeviceAccessMode::RegisterPair && domain.simulate) {
            ok = bind_simulated_register_pair(publisher, domain, next_device_id, storage);
        } else {
            std::fprintf(stderr,
                         "nexigen-chronod: domain %s: %s backend is not available in this build\n",
                         domain.name.c_str(), std::string(chrono::to_string(domain.mode)).c_str());
        }
        if (ok) {
            ++next_device_id;
            ++bound;
        }
    }
    return bound;
}

/// @brief Assemble the daemon runtime service flags from the observed report.
///
/// @param config Configuration carrying the sync policy.
/// @param report Runtime report produced by `apply_runtime`.
/// @return Flags for the publisher.
[[nodiscard]] nexigen::chrono::ServiceFlags
runtime_flags(const nexigen::chrono::TimeBaseDaemonConfig& config,
              const nexigen::chrono::DaemonRuntimeReport& report) {
    return nexigen::chrono::runtime_service_flags(config.sync_mode, report);
}

/// @brief Open the optional recorder session and bind its bridge.
///
/// @param config Daemon configuration carrying the session root.
/// @param boot_uuid Full kernel boot UUID.
/// @param boot_token 64-bit boot token.
/// @param writer Receives the opened session writer.
/// @param bridge Receives the bridge bound to the writer.
void setup_session_recording(const nexigen::chrono::TimeBaseDaemonConfig& config,
                             std::string_view boot_uuid, std::uint64_t boot_token,
                             std::optional<nexigen::recorder::SessionWriter>& writer,
                             std::optional<nexigen::recorder::TimeBaseSessionBridge>& bridge) {
    using namespace nexigen;
    if (config.record_session_path.empty()) {
        return;
    }
    recorder::StorageSettings storage{};
    storage.session_root = config.record_session_path;
    storage.session_id = "chronod";
    storage.boot_uuid = std::string(boot_uuid);
    storage.time_base_epoch = boot_token;
    char token_text[17]{};
    std::snprintf(token_text, sizeof(token_text), "%016llx",
                  static_cast<unsigned long long>(boot_token));
    storage.boot_token = token_text;
    auto created = recorder::SessionWriter::create(storage);
    if (!created) {
        std::fprintf(stderr, "nexigen-chronod: session %s: %s\n",
                     config.record_session_path.c_str(),
                     std::string(core::to_string(created.error())).c_str());
        return;
    }
    writer.emplace(std::move(*created));
    bridge.emplace(*writer, boot_uuid, boot_token);
}

/// @brief Record one published block through the optional bridge.
///
/// @param bridge Bridge, or an empty optional when recording is disabled.
/// @param segment Published segment.
void record_published_block(std::optional<nexigen::recorder::TimeBaseSessionBridge>& bridge,
                            nexigen::chrono::detail::TimeBaseSegment& segment) {
    using namespace nexigen;
    if (!bridge.has_value()) {
        return;
    }
    chrono::TimeBaseBlock published{};
    if (chrono::detail::TimeBaseReader{*segment.block()}.read(published)) {
        static_cast<void>(bridge->record(published));
    }
}

/// @brief Close the optional recorder session.
///
/// @param writer Session writer, or an empty optional when recording is disabled.
void close_session(std::optional<nexigen::recorder::SessionWriter>& writer) {
    if (writer.has_value()) {
        static_cast<void>(writer->close());
    }
}

int main(int argc, char** argv) {
    using namespace nexigen;

    const Options options = parse_args(argc, argv);
    if (options.help) {
        print_usage();
        return 0;
    }
    if (!options.valid) {
        print_usage();
        return 2;
    }

    chrono::TimeBaseDaemonConfig config{};
    const int config_status = load_config(options, config);
    if (config_status != 0) {
        return config_status;
    }

    const std::string boot_uuid = read_boot_uuid();
    const std::uint64_t boot_token = boot_token_from_uuid(boot_uuid);

    // Open the heap-allocating writers before `mlockall`, so their buffers are
    // part of the locked set instead of failing against RLIMIT_MEMLOCK later.
    // The history is append-only across daemon restarts: the earlier entries
    // must stay valid for their intervals (spec "History survives a daemon
    // restart").
    chrono::TimeBaseHistoryWriter history{chrono::JsonFileSinkConfig{config.history_path, true}};
    if (!history.is_open()) {
        std::fprintf(stderr, "nexigen-chronod: history %s could not be opened; continuing\n",
                     config.history_path.c_str());
    }
    std::optional<recorder::SessionWriter> session_writer{};
    std::optional<recorder::TimeBaseSessionBridge> session_bridge{};
    setup_session_recording(config, boot_uuid, boot_token, session_writer, session_bridge);

    chrono::DaemonRuntimeReport runtime_report{};
    if (!apply_runtime(config, runtime_report)) {
        close_session(session_writer);
        return 1;
    }

    auto created = chrono::detail::TimeBaseSegment::create(config.segment_name, boot_token);
    if (!created) {
        std::fprintf(stderr, "nexigen-chronod: segment %s: %s\n", config.segment_name.c_str(),
                     std::string(core::to_string(created.error())).c_str());
        close_session(session_writer);
        return 1;
    }
    chrono::detail::TimeBaseSegment segment = std::move(*created);

    chrono::HostClockState clock_state{};
    const chrono::HostClockSource source = chrono::production_host_clock(clock_state);
    chrono::TimeBasePublisher publisher{segment,        config,    source,
                                        history.sink(), boot_uuid, boot_token};

    // Bind every PHC-backed declared domain to its own backend. Register and
    // manual-pair domains need a vendor device module; when none is present the
    // declared domain stays unbound and the publisher reports `Degraded`.
    DeviceStorage device_storage{};
    static_cast<void>(bind_declared_devices(publisher, config, device_storage));

    publisher.measure(std::chrono::milliseconds{2});
    publisher.set_sync_mode(config.sync_mode);
    publisher.set_service_flags(runtime_flags(config, runtime_report));

    // The platform's single clock authority lives in this process. Application
    // processes hold no provider: they attach to the block below.
    chrono::NullSink utc_sink{};
    chrono::Provider<> provider{chrono::ProviderConfig{}, utc_sink};

    auto publish_once = [&]() {
        const core::Result<chrono::Timestamp> now = chrono::Timestamp::now();
        const std::uint64_t canonical =
            now ? now->nanoseconds() : (publisher.generation() * 1'000'000'000ULL);
        const std::int64_t utc_offset = provider.utc_offset();
        const std::uint64_t horizon =
            static_cast<std::uint64_t>(config.publish_interval.count() * 2);
        publisher.set_utc_calibration(utc_offset, 1'000'000ULL, canonical + horizon);
        publisher.publish(canonical);
        record_published_block(session_bridge, segment);
    };

    if (options.once) {
        publish_once();
        static_cast<void>(history.flush());
        close_session(session_writer);
        std::printf("nexigen-chronod: one generation published (status=%s)\n",
                    std::string(chrono::to_string(publisher.status())).c_str());
        return 0;
    }

    static_cast<void>(std::signal(SIGINT, handle_signal));
    static_cast<void>(std::signal(SIGTERM, handle_signal));

    std::printf("nexigen-chronod: publishing every %lld ns to %s (boot=%s)\n",
                static_cast<long long>(config.publish_interval.count()),
                config.segment_name.c_str(), boot_uuid.c_str());

    run_loop(publish_once, config.publish_interval);

    const core::Result<chrono::Timestamp> final_now = chrono::Timestamp::now();
    publisher.mark_lost(final_now ? final_now->nanoseconds() : 0U);
    record_published_block(session_bridge, segment);
    static_cast<void>(history.flush());
    close_session(session_writer);
    std::printf("nexigen-chronod: stopped; final status=%s\n",
                std::string(chrono::to_string(publisher.status())).c_str());
    return 0;
}
