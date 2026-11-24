// nexigen-logd — центральный демон-агрегатор логов (change add-log-daemon;
// spec logging/log-daemon).
//
// Подписывается на сервис iceoryx2, куда Iceoryx2Logger публикует LogFrame'ы,
// форматирует финальную JSON-строку и пишет в директорию с часовой ротацией.
//
// Использование: nexigen-logd --config <path-to-log-daemon-config.json>
#include <atomic>
#include <cerrno>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <optional>
#include <fstream>
#include <string>
#include <string_view>

#include <csignal>
#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>

#include <nlohmann/json.hpp>

#include <nexigen/chrono/boot_identity.hpp>
#include <nexigen/chrono/time_base_clock.hpp>
#include <nexigen/chrono/civil_time.hpp>
#include <nexigen/chrono/timestamp.hpp>
#include <nexigen/chrono/utc_offset.hpp>
#include <nexigen/com/event.hpp>
#include <nexigen/com/node.hpp>
#include <nexigen/com/subscriber.hpp>
#include <nexigen/com/transport_header.hpp>
#include <nexigen/com/waitset.hpp>
#include <nexigen/core/result.hpp>
#include <nexigen/core/thread_management.hpp>
#include <nexigen/logging/detail/log_service_shape.hpp>
#include <nexigen/logging/log_daemon_config.hpp>
#include <nexigen/logging/log_frame.hpp>
#include <nexigen/logging/log_service_options.hpp>

namespace com = nexigen::com;
namespace logging = nexigen::logging;
namespace core = nexigen::core;
namespace chrono = nexigen::chrono;

namespace {

std::atomic<bool> g_stop{false};
void signal_handler(int) {
    g_stop.store(true, std::memory_order_relaxed);
}

/// Render one LogFrame into a JSON line in the provided buffer.
std::size_t format_frame(const logging::LogFrame& frame, char* out, std::size_t cap,
                         std::int64_t utc_offset_ns) {
    char boot_text[chrono::boot_token_text_size + 1U];
    static_cast<void>(chrono::format_boot_token(chrono::current_boot_token(), boot_text));
    const std::optional<std::int64_t> utc =
        chrono::TimeBaseClock::attached() ? chrono::TimeBaseClock::utc_from_canonical(
                                                static_cast<std::int64_t>(frame.timestamp_ns))
                                          : std::optional<std::int64_t>{chrono::utc_from_mono_ns(
                                                frame.timestamp_ns, utc_offset_ns)};
    char ts_buf[31];
    chrono::format_utc_iso8601_ns(utc.value_or(0), ts_buf);
    char ts_field[40];
    if (utc.has_value()) {
        std::snprintf(ts_field, sizeof(ts_field), "\"%.30s\"", ts_buf);
    } else {
        std::snprintf(ts_field, sizeof(ts_field), "null");
    }
    int n = std::snprintf(
        out, cap,
        R"({"timestamp":%s,"mono_ns":%llu,"boot":"%.16s","app":"%.*s","level":"%s","thread_id":%u,"pid":%u,"message":"%.*s"})"
        "\n",
        ts_field, static_cast<unsigned long long>(frame.timestamp_ns), boot_text,
        static_cast<int>(frame.app_id_text().size()), frame.app_id_text().data(),
        core::to_string(frame.log_level).data(), frame.thread_id, frame.pid,
        static_cast<int>(frame.payload_text().size()), frame.payload_text().data());
    return (n > 0 && static_cast<std::size_t>(n) < cap) ? static_cast<std::size_t>(n) : 0U;
}

/// Write bytes to a file descriptor (EINTR-safe).
bool write_all(int fd, const char* buf, std::size_t len) noexcept {
    std::size_t off = 0;
    while (off < len) {
        const ssize_t w = ::write(fd, buf + off, len - off);
        ;
        if (w > 0) {
            off += static_cast<std::size_t>(w);
        } else if (errno != EINTR) {
            return false;
        }
    }
    return true;
}

/// Open (or re-open) the log file for the current UTC hour.
int open_log_file(const std::string& log_dir, std::int64_t utc_offset_ns) noexcept {
    const auto now = chrono::Timestamp::now();
    if (!now) return -1;
    const std::int64_t utc = chrono::utc_from_mono_ns(now->nanoseconds(), utc_offset_ns);
    const chrono::CivilTime civil = chrono::civil_from_utc_ns(utc);

    char dir[1024];
    std::snprintf(dir, sizeof(dir), "%s/%04lld-%02d-%02dT%02d", log_dir.c_str(),
                  static_cast<long long>(civil.year), static_cast<int>(civil.month),
                  static_cast<int>(civil.day), static_cast<int>(civil.hour));
    ::mkdir(dir, 0755); // best-effort; ignore EEXIST

    char path[1088];
    std::snprintf(path, sizeof(path), "%s/nexigen.jsonl", dir);
    return ::open(path, O_WRONLY | O_CREAT | O_APPEND | O_CLOEXEC, 0644);
}

/// Load LogDaemonConfig from a JSON file.
core::Status load_config(const char* path, logging::LogDaemonConfig& cfg) noexcept {
    std::ifstream file(path);
    if (!file) {
        return std::unexpected(core::ErrorCode::SystemError);
    }
    nlohmann::json j = nlohmann::json::parse(file, nullptr, false);
    if (j.is_discarded()) {
        return std::unexpected(core::ErrorCode::InvalidArgument);
    }
    logging::from_json(j, cfg);
    return {};
}

core::Status validate_config(const logging::LogDaemonConfig& cfg) noexcept {
    if (cfg.service_name.empty() || cfg.service_name.size() > 511U) {
        return std::unexpected(core::ErrorCode::InvalidArgument);
    }
    if (cfg.log_directory.empty() || cfg.log_directory.size() > 511U ||
        cfg.log_directory[0] != '/') {
        return std::unexpected(core::ErrorCode::InvalidArgument);
    }
    return {};
}

/// Parsed command line. `--help` reports an early exit code through the return
/// value; everything else fills the options.
struct Options {
    const char* config_path{nullptr};
};

[[nodiscard]] std::optional<int> parse_args(int argc, char* argv[], Options& options) {
    for (int i = 1; i < argc; ++i) {
        if (std::strcmp(argv[i], "--config") == 0 && i + 1 < argc) {
            options.config_path = argv[++i];
        } else if (std::strcmp(argv[i], "--help") == 0) {
            std::printf("nexigen-logd - Log Daemon\nUsage: nexigen-logd --config <path>\n");
            return 0;
        }
    }
    if (options.config_path == nullptr) {
        std::fprintf(stderr, "nexigen-logd: missing --config <path>\n");
        return 1;
    }
    return std::nullopt;
}

constexpr std::size_t kBufSz = 65536U;

/// Open log file plus the pending batch behind it.
struct Sink {
    int fd{-1};
    std::int64_t hour_key{0}; ///< UTC hour key of the open file (0 = not rotated yet)
    std::size_t used{0};      ///< bytes pending in `buffer`
    std::chrono::steady_clock::time_point last_flush{}; ///< last time-based flush
    alignas(64) char buffer[kBufSz]{};
};

/// Re-open the log file when the frame's UTC hour differs from the open one.
void rotate_if_needed(const logging::LogFrame& frame, const logging::LogDaemonConfig& cfg,
                      std::int64_t utc_offset_ns, Sink& sink) {
    const std::int64_t frame_utc = chrono::utc_from_mono_ns(frame.timestamp_ns, utc_offset_ns);
    const chrono::CivilTime civil = chrono::civil_from_utc_ns(frame_utc);
    const std::int64_t hour_key = static_cast<std::int64_t>(civil.year) * 1000000LL +
                                  civil.month * 10000LL + civil.day * 100LL + civil.hour;
    if (hour_key == sink.hour_key) {
        return;
    }
    if (sink.fd >= 0) {
        ::fsync(sink.fd);
        ::close(sink.fd);
    }
    sink.fd = open_log_file(cfg.log_directory, utc_offset_ns);
    sink.hour_key = hour_key;
}

/// Append one rendered frame to the pending batch, flushing at half a buffer.
void append_frame(const logging::LogFrame& frame, std::int64_t utc_offset_ns, Sink& sink) {
    if (sink.fd < 0) {
        return;
    }
    const std::size_t line_len =
        format_frame(frame, sink.buffer + sink.used, kBufSz - sink.used - 1U, utc_offset_ns);
    if (line_len == 0U) {
        return;
    }
    sink.used += line_len;
    if (sink.used >= kBufSz / 2U) {
        write_all(sink.fd, sink.buffer, sink.used);
        sink.used = 0U;
    }
}

/// Drain every frame the subscriber has queued, applying the storage threshold.
void drain_subscriber(com::Subscriber<logging::LogFrame, com::TransportHeader>& sub,
                      const logging::LogDaemonConfig& cfg, std::int64_t utc_offset_ns, Sink& sink) {
    for (;;) {
        auto got = sub.receive();
        if (!got || !got->has_value()) {
            break;
        }
        const logging::LogFrame& frame = (*got)->payload();
        if (frame.version != logging::LogFrame::format_version) {
            continue; // unknown wire version: reject instead of misreading offsets
        }
        if (frame.log_level < cfg.min_log_level_storage) {
            continue;
        }
        rotate_if_needed(frame, cfg, utc_offset_ns, sink);
        append_frame(frame, utc_offset_ns, sink);
    }
    // Time-based flush: the batch-flush timer wakes the WaitSet, but the pending
    // bytes must actually reach the file on that cadence (the size-based flush
    // in append_frame only fires at half the buffer). Without this a low-rate
    // producer leaves the log file empty until ~32 KiB accumulate or shutdown.
    const std::chrono::milliseconds flush_ms(
        cfg.batch_flush_interval_ms > 0U ? cfg.batch_flush_interval_ms : 500U);
    const auto now = std::chrono::steady_clock::now();
    if (now - sink.last_flush >= flush_ms) {
        if (sink.used > 0U && sink.fd >= 0) {
            write_all(sink.fd, sink.buffer, sink.used);
            sink.used = 0U;
        }
        sink.last_flush = now;
    }
}

/// Flush the pending batch and close the open file.
void flush_and_close(Sink& sink) {
    if (sink.used > 0U && sink.fd >= 0) {
        write_all(sink.fd, sink.buffer, sink.used);
    }
    if (sink.fd >= 0) {
        ::fsync(sink.fd);
        ::close(sink.fd);
    }
}

/// Open every endpoint, run the drain loop until SIGTERM, then flush.
/// Apply the main-thread policy: an explicit main_thread wins; otherwise the
/// legacy bg_cpu_affinity CpuSet becomes a placement-only spec.
[[nodiscard]] bool apply_main_thread_spec(const logging::LogDaemonConfig& cfg) noexcept {
    const core::ThreadSpec effective = logging::effective_main_thread_spec(cfg);
    core::ThreadApplyReport report{};
    const core::Status applied = core::apply_thread_spec_self(effective, report);
    if (!applied) {
        std::fprintf(stderr, "nexigen-logd: main thread spec failed (%s)\n",
                     std::string(core::to_string(applied.error())).c_str());
        return false;
    }
    return true;
}

/// @brief Create the daemon transport node.
///
/// @return The node, or `std::nullopt` after reporting the failure.
std::optional<com::Node> create_daemon_node() {
    auto node = com::Node::create();
    if (!node) {
        std::fprintf(stderr, "nexigen-logd: failed to create iceoryx2 node\n");
        return std::nullopt;
    }
    return std::make_optional<com::Node>(std::move(*node));
}

/// @brief Sample the wall-clock offset and attach the published base when present.
///
/// @return The sampled offset, or `std::nullopt` after reporting the failure.
std::optional<std::int64_t> resolve_utc_offset() {
    std::int64_t utc_offset_ns = 0;
    if (!chrono::sample_utc_offset(utc_offset_ns)) {
        std::fprintf(stderr, "nexigen-logd: failed to sample UTC offset\n");
        return std::nullopt;
    }
    // Best effort: when the suite publishes a time base under the default name,
    // rendered UTC comes from it; otherwise the sampled offset is used.
    static_cast<void>(chrono::TimeBaseClock::attach("/nexigen-timebase"));
    return utc_offset_ns;
}

/// @brief Resolve the configured log service name.
///
/// @param name Configured service name.
/// @return The validated name, or `std::nullopt` after reporting the failure.
std::optional<com::ServiceName> resolve_log_service(const std::string& name) {
    const auto svc = com::ServiceName::create(name.c_str());
    if (!svc) {
        std::fprintf(stderr, "nexigen-logd: invalid service name '%s'\n", name.c_str());
        return std::nullopt;
    }
    return *svc;
}

/// @brief Reject a live service whose capacities differ from this build's shape.
///
/// The daemon normally starts first and therefore fixes the service shape: every
/// later publisher and subscriber must see the same capacities. If a live
/// service was created with the old shape, name both shapes instead of letting
/// the subscriber creation fail with a generic error.
///
/// @param node Transport node.
/// @param cfg Daemon configuration.
/// @return `false` when the live shape is incompatible.
bool check_log_service_shape(com::Node& node, const logging::LogDaemonConfig& cfg) {
    const auto shape = logging::detail::query_log_service_shape(node, cfg.service_name.c_str());
    if (!shape.has_value() || shape->compatible()) {
        return true;
    }
    char diagnostic[512];
    static_cast<void>(logging::detail::format_shape_mismatch(cfg.service_name, *shape, diagnostic,
                                                             sizeof(diagnostic)));
    std::fprintf(stderr, "nexigen-logd: %s\n", diagnostic);
    return false;
}

/// @brief Create the log-frame subscriber.
///
/// @param node Transport node.
/// @param svc Log service name.
/// @return The subscriber, or `std::nullopt` after reporting the failure.
std::optional<com::Subscriber<logging::LogFrame, com::TransportHeader>>
create_log_subscriber(com::Node& node, const com::ServiceName& svc) {
    auto sub = com::Subscriber<logging::LogFrame, com::TransportHeader>::create(
        node, svc, logging::log_service_options());
    if (!sub) {
        std::fprintf(stderr, "nexigen-logd: failed to create subscriber\n");
        return std::nullopt;
    }
    return std::make_optional<com::Subscriber<logging::LogFrame, com::TransportHeader>>(
        std::move(*sub));
}

/// @brief Create the wake-up event listener for the log service.
///
/// @param node Transport node.
/// @param cfg Daemon configuration.
/// @return The listener, or `std::nullopt` after reporting the failure.
std::optional<com::EventListener> create_log_listener(com::Node& node,
                                                      const logging::LogDaemonConfig& cfg) {
    const std::string ev_svc = cfg.service_name + "Event";
    const auto ev_nm = com::ServiceName::create(ev_svc.c_str());
    if (!ev_nm) {
        std::fprintf(stderr, "nexigen-logd: invalid event service name\n");
        return std::nullopt;
    }
    auto listener = com::EventListener::create(node, *ev_nm);
    if (!listener) {
        std::fprintf(stderr, "nexigen-logd: failed to create event listener\n");
        return std::nullopt;
    }
    return std::make_optional<com::EventListener>(std::move(*listener));
}

/// @brief Create the WaitSet the daemon multiplexes on.
///
/// @return The WaitSet, or `std::nullopt` after reporting the failure.
std::optional<com::WaitSet> create_daemon_waitset() {
    auto ws = com::WaitSet::create_without_signal_handling();
    if (!ws) {
        std::fprintf(stderr, "nexigen-logd: failed to create WaitSet\n");
        return std::nullopt;
    }
    return std::make_optional<com::WaitSet>(std::move(*ws));
}

/// @brief Drain frames until the stop flag is raised or the WaitSet fails.
///
/// @param ws WaitSet with the event listener and flush timer attached.
/// @param sub Log-frame subscriber.
/// @param listener Wake-up event listener.
/// @param cfg Daemon configuration.
/// @param utc_offset_ns Sampled wall-clock offset.
/// @param sink Rotating JSON-lines sink.
void run_dispatch_loop(com::WaitSet& ws,
                       com::Subscriber<logging::LogFrame, com::TransportHeader>& sub,
                       com::EventListener& listener, const logging::LogDaemonConfig& cfg,
                       std::int64_t utc_offset_ns, Sink& sink) {
    while (!g_stop.load(std::memory_order_relaxed)) {
        const auto status = ws.wait_and_process([&](com::WaitSetAttachmentId) {
            static_cast<void>(listener.try_wait([](com::EventId) noexcept {}));
            drain_subscriber(sub, cfg, utc_offset_ns, sink);
            return com::CallbackProgression::Continue;
        });
        if (!status) {
            break;
        }
    }
}

int run_session(const logging::LogDaemonConfig& cfg) {
    if (!apply_main_thread_spec(cfg)) {
        return 1;
    }
    auto node = create_daemon_node();
    if (!node) {
        return 1;
    }
    const auto utc_offset_ns = resolve_utc_offset();
    if (!utc_offset_ns) {
        return 1;
    }
    const auto svc = resolve_log_service(cfg.service_name);
    if (!svc) {
        return 1;
    }
    if (!check_log_service_shape(*node, cfg)) {
        return 1;
    }
    auto sub = create_log_subscriber(*node, *svc);
    if (!sub) {
        return 1;
    }
    auto listener = create_log_listener(*node, cfg);
    if (!listener) {
        return 1;
    }

    Sink sink{};
    sink.fd = open_log_file(cfg.log_directory, *utc_offset_ns);

    auto ws = create_daemon_waitset();
    if (!ws) {
        return 1;
    }
    auto guard_ev = ws->attach_notification(*listener);
    if (!guard_ev) {
        std::fprintf(stderr, "nexigen-logd: failed to attach event listener\n");
        return 1;
    }
    const auto flush_ms = std::chrono::milliseconds(
        cfg.batch_flush_interval_ms > 0U ? cfg.batch_flush_interval_ms : 500U);
    auto guard_tm = ws->attach_interval(flush_ms);
    if (!guard_tm) {
        std::fprintf(stderr, "nexigen-logd: failed to attach flush timer\n");
        return 1;
    }

    std::printf("nexigen-logd: listening on %s, writing to %s\n", cfg.service_name.c_str(),
                cfg.log_directory.c_str());

    run_dispatch_loop(*ws, *sub, *listener, cfg, *utc_offset_ns, sink);

    flush_and_close(sink);
    std::printf("nexigen-logd: stopped\n");
    return 0;
}

/// Load the configuration, install the stop handlers, then run one session.
int run_daemon(const Options& options) {
    logging::LogDaemonConfig cfg;
    if (!load_config(options.config_path, cfg)) {
        std::fprintf(stderr, "nexigen-logd: failed to load config from %s\n", options.config_path);
        return 1;
    }
    if (!validate_config(cfg)) {
        std::fprintf(stderr, "nexigen-logd: invalid config\n");
        return 1;
    }

    std::signal(SIGTERM, signal_handler);
    std::signal(SIGINT, signal_handler);
    return run_session(cfg);
}

} // namespace

int main(int argc, char* argv[]) {
    Options options;
    const std::optional<int> early_exit = parse_args(argc, argv, options);
    if (early_exit.has_value()) {
        return *early_exit;
    }
    return run_daemon(options);
}
