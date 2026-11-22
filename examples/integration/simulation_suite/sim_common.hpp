// nexigen::integration simulation_suite — shared application scaffold
// (change add-simulation-suite-example; spec examples/simulation-suite).
//
// Everything the three simulation applications need in common: the telemetry
// policy that wires the iceoryx2 sinks, the standalone settings bootstrap that
// makes each binary runnable on its own, the command-line preparation shared by
// the examples, and the platform link that reports readiness to the Execution
// Manager, publishes checkpoints to PlatformHealthManagement, and reads the
// State Manager's published Function Group state.
#pragma once

#include <array>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include <nexigen/app/infrastructure.hpp>
#include "../../app/time_base_fixture.hpp"
#include <nexigen/chrono/null_sink.hpp>
#include <nexigen/com/error.hpp>
#include <nexigen/com/node.hpp>
#include <nexigen/com/service_name.hpp>
#include <nexigen/core/result.hpp>
#include <nexigen/core/thread_management.hpp>
#include <nexigen/exec/application_state.hpp>
#include <nexigen/logging/iceoryx2_logger.hpp>
#include <nexigen/logging/iceoryx2_logger_config.hpp>
#include <nexigen/metrics/iceoryx2_metric_sink.hpp>
#include <nexigen/metrics/iceoryx2_metric_sink_config.hpp>
#include <nexigen/phm/supervised_entity.hpp>
#include <nexigen/tracing/iceoryx2_trace_sink.hpp>
#include <nexigen/tracing/iceoryx2_trace_sink_config.hpp>

namespace simulation_suite {

namespace app = nexigen::app;
namespace core = nexigen::core;
namespace chrono = nexigen::chrono;
namespace com = nexigen::com;
namespace exec = nexigen::exec;
namespace logging = nexigen::logging;
namespace metrics = nexigen::metrics;
namespace phm = nexigen::phm;
namespace tracing = nexigen::tracing;

/// @brief Function Group every simulation application belongs to.
inline constexpr std::string_view k_function_group = "Simulation";

/// @brief Service the generator publishes and both receivers subscribe to.
inline constexpr std::string_view k_vector_service = "Nexigen/Simulation/Vectors";

/// @brief Event the generator rings after each publish.
///
/// Receivers block on it instead of polling, so the measured latency reflects
/// the transport and the scheduler rather than a fixed sleep period.
inline constexpr std::string_view k_vector_event_service = "Nexigen/Simulation/VectorsEvent";

/// @brief Directory the standalone (CTest) mode writes its settings into.
inline constexpr std::string_view k_standalone_settings_dir = "./nexigen-simulation-settings";

/// @brief Directory the standalone (CTest) mode writes its output into.
inline constexpr std::string_view k_standalone_output_dir = "./nexigen-simulation-output";

/// @brief Default bounded workload used by the CTest smoke runs.
inline constexpr std::uint64_t k_default_worker_seconds = 2;

/// @brief Settings contents written for the standalone mode.
inline constexpr std::string_view k_logging_settings = R"({
  "initial_log_level": "debug",
  "spsc_ring_buffer_capacity": 256,
  "message_capacity": 128,
  "max_producer_threads": 32
}
)";
inline constexpr std::string_view k_metrics_settings = R"({
  "collect_interval_ns": 1000000000,
  "max_counters": 16,
  "max_gauges": 16
}
)";
inline constexpr std::string_view k_tracing_settings = R"({
  "max_tracers": 4
}
)";
inline constexpr std::string_view k_chrono_settings = R"({
  "collect_interval_ns": 1000000000
}
)";

/// @brief Monotonic nanoseconds shared by the send and receive timestamps.
///
/// `steady_clock` is `CLOCK_MONOTONIC` on Linux and therefore comparable across
/// processes on the same machine, which is what the latency measurement needs.
///
/// @return Nanoseconds since the `steady_clock` epoch.
[[nodiscard]] inline std::uint64_t now_ns() {
    return static_cast<std::uint64_t>(std::chrono::duration_cast<std::chrono::nanoseconds>(
                                          std::chrono::steady_clock::now().time_since_epoch())
                                          .count());
}

/// @brief Read one environment variable.
///
/// @param name Variable name.
/// @return The value, or `std::nullopt` when unset.
[[nodiscard]] inline std::optional<std::string_view> env(std::string_view name) {
    // getenv() needs a NUL-terminated key; the view itself usually points at a
    // literal, but the copy keeps the helper safe for any caller.
    const std::string owned(name);
    const char* value = std::getenv(owned.c_str());
    if (value == nullptr) {
        return std::nullopt;
    }
    return std::string_view{value};
}

/// @brief Read one environment variable as an unsigned integer.
///
/// @param name Variable name.
/// @param fallback Value used when unset or malformed.
/// @return The parsed value, or `fallback`.
[[nodiscard]] inline std::uint64_t env_u64(std::string_view name, std::uint64_t fallback) {
    const std::optional<std::string_view> value = env(name);
    if (!value.has_value() || value->empty()) {
        return fallback;
    }
    std::uint64_t out = 0;
    for (const char c : *value) {
        if (c < '0' || c > '9') {
            return fallback;
        }
        out = out * 10U + static_cast<std::uint64_t>(c - '0');
    }
    return out;
}

/// @brief Print one diagnostic line prefixed with the executable tag.
///
/// @param tag Executable tag, for example `ex_sim_generator`.
/// @param message Text to print.
inline void print_error(std::string_view tag, std::string_view message) {
    std::fprintf(stderr, "%.*s: %s\n", static_cast<int>(tag.size()), tag.data(),
                 std::string(message).c_str());
}

/// @brief Render a core error code.
///
/// @param error Error to render.
/// @return The canonical name.
[[nodiscard]] inline std::string_view render_error(core::ErrorCode error) noexcept {
    return core::to_string(error);
}

/// @brief Render a transport error.
///
/// @param error Error to render.
/// @return The canonical name.
[[nodiscard]] inline std::string_view render_error(com::Error error) noexcept {
    return com::to_string(error);
}

/// @brief Canonical name of a published Function Group state.
///
/// Returns a view over a string literal, so the value is safe to pass to the
/// deferred logger, which packs the argument rather than copying it.
///
/// @param state Published state name.
/// @return The literal spelling, or `"<unpublished>"` for an empty state.
[[nodiscard]] inline std::string_view state_name(std::string_view state) noexcept {
    if (state == "Running") {
        return "Running";
    }
    if (state == "SafeState") {
        return "SafeState";
    }
    if (state == "Off") {
        return "Off";
    }
    return "<unpublished>";
}

/// @brief Move the calling thread onto `SCHED_FIFO`.
///
/// Opt-in and best-effort: a host without `RLIMIT_RTPRIO` or `CAP_SYS_NICE`
/// rejects the call, and the application keeps running on `SCHED_OTHER`.
///
/// @param priority Real-time priority in the platform-supported range.
/// @return Empty status when the policy was applied, otherwise the mapped
///         error code.
/// @note Spec `examples/simulation-suite`, requirement "Event-driven receive".
[[nodiscard]] inline core::Status apply_realtime_priority(std::int32_t priority) noexcept {
    core::ThreadSpec spec;
    static_cast<void>(spec.set_name("sim-rt"));
    const core::Status scheduled = spec.set_scheduler(core::SchedulingPolicy::FIFO, priority);
    if (!scheduled) {
        return scheduled;
    }
    core::ThreadApplyReport report;
    const core::Status applied = core::apply_thread_spec_self(spec, report);
    if (!applied) {
        return applied;
    }
    // The optional apply mode reports a refused policy through the report
    // rather than the status, so a host without RT privileges still returns a
    // success status while the thread stays on SCHED_OTHER.
    if ((report.failed() & core::ThreadApplyReport::scheduler_mask) != 0U) {
        return std::unexpected(report.first_error());
    }
    return {};
}

/// @brief Whether a published Function Group state permits work.
///
/// An empty state means the State Manager has not published the group yet, which
/// is the standalone smoke-run case.
///
/// @param state Published state name.
/// @return `true` when the application may publish or print.
[[nodiscard]] inline bool state_allows_run(std::string_view state) noexcept {
    return state.empty() || state == "Running";
}

/// @brief Print a failed status and return the conventional failure code.
///
/// @tparam Result `core::Status`-like value providing `error()`.
/// @param tag Executable tag.
/// @param action Action that failed.
/// @param status The failed status.
/// @return Always 1.
template <class Result>
inline int fail_status(std::string_view tag, std::string_view action, const Result& status) {
    print_error(tag, std::string(action) + " failed: " + std::string(render_error(status.error())));
    return 1;
}

/// @brief Write one settings file.
///
/// @param path Destination path.
/// @param contents File contents.
/// @return `true` when the file was written completely.
[[nodiscard]] inline bool write_settings_file(std::string_view tag, std::string_view path,
                                              std::string_view contents) {
    std::ofstream file{std::string(path), std::ios::out | std::ios::trunc};
    if (!file) {
        print_error(tag, "cannot open settings file: " + std::string(path));
        return false;
    }
    file << contents;
    file.close();
    return !file.fail();
}

/// @brief Prepare the standalone settings and output directories.
///
/// @param tag Executable tag.
/// @return `true` when every settings file was written.
[[nodiscard]] inline bool prepare_standalone(std::string_view tag) {
    // CTest starts each binary without a suite manifest. Materialise a private
    // settings directory and a clean output directory so the smoke run behaves
    // like a normal application instead of failing the "one settings source"
    // rule. The directory is deliberately relative to the CTest working dir.
    std::error_code ec;
    std::filesystem::remove_all(std::filesystem::path(k_standalone_output_dir), ec);
    std::filesystem::create_directories(std::filesystem::path(k_standalone_output_dir), ec);
    std::filesystem::create_directories(std::filesystem::path(k_standalone_settings_dir), ec);
    const std::pair<std::string_view, std::string_view> files[] = {
        {"logging.json", k_logging_settings},
        {"metrics.json", k_metrics_settings},
        {"tracing.json", k_tracing_settings},
        {"chrono.json", k_chrono_settings},
    };
    for (const auto& [name, contents] : files) {
        const std::string path = std::string(k_standalone_settings_dir) + "/" + std::string(name);
        if (!write_settings_file(tag, path, contents)) {
            return false;
        }
    }
    return true;
}

/// @brief Whether the caller already supplied a settings directory.
///
/// @param args Command-line tokens.
/// @return `true` when a settings option is present before `--`.
[[nodiscard]] inline bool has_directory_option(std::span<const std::string_view> args) {
    for (const std::string_view token : args) {
        if (token == "--") {
            break;
        }
        if (token.starts_with("-gs") || token.starts_with("--global-settings") ||
            token.starts_with("-ls") || token.starts_with("--local-settings")) {
            return true;
        }
    }
    return false;
}

/// @brief Build the argument vector and parse it, adding standalone settings
///        when the caller supplied none.
///
/// @param argc Argument count from `main`.
/// @param argv Argument vector from `main`.
/// @param args Backing storage for the parsed tokens; must outlive `options`.
/// @param tag Executable tag.
/// @param options Receives the parsed options.
/// @return An exit code for the caller when it should stop, or `std::nullopt`.
[[nodiscard]] inline std::optional<int> prepare_command_line(int argc, char* argv[],
                                                             std::vector<std::string_view>& args,
                                                             std::string_view tag,
                                                             app::CommandLineOptions& options) {
    args.reserve(static_cast<std::size_t>(argc > 0 ? argc - 1 : 0) + 2);
    for (int i = 1; i < argc; ++i) {
        args.emplace_back(argv[i]);
    }
    bool help_requested = false;
    for (const std::string_view token : args) {
        if (token == "-h" || token == "--help") {
            help_requested = true;
            break;
        }
    }
    if (!help_requested && !has_directory_option(args)) {
        // Standalone mode: generate defaults and feed them through the normal
        // CLI path, so the example exercises the same settings loader as a
        // suite-launched application.
        if (!prepare_standalone(tag)) {
            return 1;
        }
        args.emplace_back("--local-settings");
        args.emplace_back(k_standalone_settings_dir);
    }
    const app::CommandLineResult parsed =
        app::parse_command_line(std::span<const std::string_view>{args});
    if (!parsed.has_value()) {
        for (const auto& diagnostic : parsed.error().diagnostics) {
            std::fprintf(stderr, "%.*s: %s: %s\n", static_cast<int>(tag.size()), tag.data(),
                         diagnostic.option.c_str(), diagnostic.message.c_str());
        }
        const std::string_view usage = app::command_line_usage();
        static_cast<void>(std::fwrite(usage.data(), 1, usage.size(), stderr));
        return 2;
    }
    options = *parsed;
    if (options.help) {
        const std::string_view usage = app::command_line_usage();
        static_cast<void>(std::fwrite(usage.data(), 1, usage.size(), stdout));
        return 0;
    }
    return std::nullopt;
}

/// @brief Initialize the infrastructure, printing every settings diagnostic.
///
/// @param infra Infrastructure to initialize.
/// @param options Parsed command line.
/// @param loaded Receives the loaded settings.
/// @param tag Executable tag.
/// @return The initialization status.
template <class Infrastructure>
[[nodiscard]] core::Status initialize_infrastructure(Infrastructure& infra,
                                                     const app::CommandLineOptions& options,
                                                     app::Settings& loaded, std::string_view tag) {
    // The example owns a local time-base segment instead of requiring a
    // separate nexigen-chronod process; the same segment is installed once for
    // the whole smoke run. In a real deployment this would be the daemon's
    // published segment name.
    static nexigen::examples::LocalTimeBase time_base{"sim"};
    const core::Status loaded_status = app::load_app_settings(options, loaded);
    loaded.chrono.segment_name = time_base.name();
    if (!loaded_status) {
        for (const auto& diagnostic : loaded.logging.diagnostics) {
            print_error(tag, "logging: " + diagnostic.to_string());
        }
        for (const auto& diagnostic : loaded.metrics.diagnostics) {
            print_error(tag, "metrics: " + diagnostic.to_string());
        }
        for (const auto& diagnostic : loaded.tracing.diagnostics) {
            print_error(tag, "tracing: " + diagnostic.to_string());
        }
        for (const auto& diagnostic : loaded.chrono.diagnostics) {
            print_error(tag, "chrono: " + diagnostic.to_string());
        }
        return loaded_status;
    }
    const core::Status initialized = infra.initialize(loaded);
    if (initialized) {
        return initialized;
    }
    for (const auto& diagnostic : loaded.logging.diagnostics) {
        print_error(tag, "logging: " + diagnostic.to_string());
    }
    for (const auto& diagnostic : loaded.metrics.diagnostics) {
        print_error(tag, "metrics: " + diagnostic.to_string());
    }
    for (const auto& diagnostic : loaded.tracing.diagnostics) {
        print_error(tag, "tracing: " + diagnostic.to_string());
    }
    for (const auto& diagnostic : loaded.chrono.diagnostics) {
        print_error(tag, "chrono: " + diagnostic.to_string());
    }
    return initialized;
}

/// @brief Slot holding the one node this application shares everywhere.
///
/// iceoryx2 caps the number of nodes per machine (20 by default), and a
/// full-stack demonstration with three daemons plus three supervisors is close
/// to that ceiling. Each application therefore creates exactly one node before
/// the infrastructure starts and lends it to the metric and trace sinks as well
/// as to its own endpoints. The logger still owns its internal node.
///
/// @return The installed node, or `nullptr` before `install_shared_node`.
[[nodiscard]] inline com::Node*& shared_node_slot() noexcept {
    static com::Node* node = nullptr;
    return node;
}

/// @brief Install the application node the policy will borrow.
///
/// @param node Node that outlives the infrastructure.
inline void install_shared_node(com::Node& node) noexcept {
    shared_node_slot() = &node;
}

/// @brief The node the metric and trace sinks borrow.
///
/// @return The installed node, or `nullptr` when `install_shared_node` has not
///         run yet.
[[nodiscard]] inline com::Node* telemetry_node() noexcept {
    return shared_node_slot();
}

/// @brief Telemetry policy shared by the three applications.
///
/// The manifest `app_id` is authoritative in suite mode and is copied into every
/// log, metric, and trace frame. The four characters are the standalone fallback
/// used only when the application is started without `--app-id`.
///
/// @tparam A First fallback tag character.
/// @tparam B Second fallback tag character.
/// @tparam C Third fallback tag character.
/// @tparam D Fourth fallback tag character.
template <char A, char B, char C, char D> struct SimTelemetryPolicy {
    /// Three-argument records (index, latency, trace id) need a wider inline blob
    /// than the 128-byte default.
    using Logger = logging::Iceoryx2Logger<128, 256, 32>;
    using MetricsSink = metrics::Iceoryx2MetricSink;
    using TraceSink = tracing::Iceoryx2TraceSink;
    using Sink = chrono::NullSink;

    /// @brief Standalone fallback tag; the manifest `app_id` wins when set.
    static constexpr char k_tag[5] = {A, B, C, D, '\0'};

    /// @brief Emplace the iceoryx2 logger.
    static core::Status emplace_logger(std::optional<Logger>& out,
                                       const logging::Settings& settings,
                                       const app::TelemetryIdentity& identity) noexcept {
        logging::Iceoryx2LoggerConfig config{};
        config.service_name = "Nexigen/Log/nexigen";
        config.app_id =
            identity.app_id.empty() ? core::AppId::from_literal(k_tag) : identity.app_id;
        config.initial_level = settings.options.initial_log_level;
        config.worker_thread = settings.options.worker_thread;
        config.worker_poll_interval_us = settings.options.worker_poll_interval_us;
        out.emplace();
        return out->initialize(config);
    }

    /// @brief Start the iceoryx2 metric sink.
    static core::Status emplace_metrics_sink(std::optional<MetricsSink>& out,
                                             const metrics::Settings&,
                                             const app::TelemetryIdentity& identity) noexcept {
        com::Node* node = telemetry_node();
        if (node == nullptr) {
            return std::unexpected(core::ErrorCode::SystemError);
        }
        metrics::Iceoryx2MetricSinkConfig config{};
        config.service_name = "nexigen/metrics/stream";
        config.app_id =
            identity.app_id.empty() ? core::AppId::from_literal(k_tag) : identity.app_id;
        out.emplace(config, node);
        return out->start();
    }

    /// @brief Emplace the iceoryx2 trace sink.
    static core::Status emplace_trace_sink(std::optional<TraceSink>& out, const tracing::Settings&,
                                           const app::TelemetryIdentity& identity) noexcept {
        com::Node* node = telemetry_node();
        if (node == nullptr) {
            return std::unexpected(core::ErrorCode::SystemError);
        }
        tracing::Iceoryx2TraceSinkConfig config{};
        config.service_name = "nexigen/trace/stream";
        config.app_id =
            identity.app_id.empty() ? core::AppId::from_literal(k_tag) : identity.app_id;
        out.emplace(config, node);
        return {};
    }

    /// @brief Emplace the chrono sink (the example does not calibrate clocks).
};

/// @brief The three platform channels every simulation application owns.
///
/// Owns the readiness reporter towards the Execution Manager, the supervised
/// entity that publishes checkpoints towards PlatformHealthManagement, and the
/// State Manager Function Group listener used to honour `Off` and `SafeState`.
///
/// @note Spec `examples/simulation-suite`, requirement "Platform integration of
///       every application".
class PlatformLink {
public:
    /// @brief Open every platform channel for one application.
    ///
    /// @param node Node owning the transport; must outlive the link.
    /// @param app_name Application name; must match the suite manifest entry.
    /// @param checkpoint_service Service the daemon listens on for this app.
    /// @param health_service Service used for application health reports.
    /// @return The link, or the first transport error.
    [[nodiscard]] static com::Result<PlatformLink>
    create(com::Node& node, std::string_view app_name, std::string_view checkpoint_service,
           std::string_view health_service) noexcept {
        auto reporter = exec::ApplicationStateReporter::create(node, app_name);
        if (!reporter) {
            return std::unexpected(reporter.error());
        }
        auto checkpoint_name = com::ServiceName::create(std::string(checkpoint_service).c_str());
        if (!checkpoint_name) {
            return std::unexpected(checkpoint_name.error());
        }
        auto health_name = com::ServiceName::create(std::string(health_service).c_str());
        if (!health_name) {
            return std::unexpected(health_name.error());
        }
        auto entity = phm::SupervisedEntity::create(node, app_name, *checkpoint_name, *health_name);
        if (!entity) {
            return std::unexpected(entity.error());
        }
        return PlatformLink{std::move(*reporter), std::move(*entity)};
    }

    PlatformLink(const PlatformLink&) = delete;
    PlatformLink& operator=(const PlatformLink&) = delete;
    PlatformLink(PlatformLink&&) noexcept = default;
    PlatformLink& operator=(PlatformLink&&) noexcept = default;

    /// @brief Report readiness to the Execution Manager.
    ///
    /// @return The transport status.
    [[nodiscard]] com::Status report_ready() noexcept {
        return reporter_.report(exec::AppState::Running);
    }

    /// @brief Publish one checkpoint towards PlatformHealthManagement.
    ///
    /// @param checkpoint_id Checkpoint id configured in the health manifest.
    /// @return The transport status.
    [[nodiscard]] com::Status checkpoint(std::uint32_t checkpoint_id) noexcept {
        return entity_.report_checkpoint(checkpoint_id);
    }

    /// @brief Read the published Function Group state.
    ///
    /// @return The state name, or an empty view when the State Manager has not
    ///         published the group yet.
    /// @note Spec `examples/simulation-suite`, requirement "Simulation Function
    ///       Group with three applications".
    [[nodiscard]] std::string_view group_state() noexcept {
        // The Field stores a fixed-size name; copy it into the link's buffer so
        // the returned view stays valid after the sample is released. An
        // unpublished group yields an empty view, which the caller treats as
        // the standalone "no State Manager yet" case.
        auto state = entity_.current_group_state(k_function_group);
        if (!state || !state->has_value()) {
            return {};
        }
        std::snprintf(group_state_, sizeof(group_state_), "%s", (*state)->state);
        return std::string_view{group_state_};
    }

private:
    PlatformLink(exec::ApplicationStateReporter reporter, phm::SupervisedEntity entity) noexcept
        : reporter_(std::move(reporter)), entity_(std::move(entity)) {}

    exec::ApplicationStateReporter reporter_; ///< Readiness towards the EM.
    phm::SupervisedEntity entity_;            ///< Checkpoints and group-state read.
    char group_state_[32]{};                  ///< Copy of the last published state.
};

} // namespace simulation_suite
