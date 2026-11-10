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
#include <fstream>
#include <string>
#include <string_view>

#include <csignal>
#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>

#include <nlohmann/json.hpp>

#include <nexigen/chrono/civil_time.hpp>
#include <nexigen/chrono/timestamp.hpp>
#include <nexigen/chrono/utc_offset.hpp>
#include <nexigen/com/event.hpp>
#include <nexigen/com/node.hpp>
#include <nexigen/com/subscriber.hpp>
#include <nexigen/com/waitset.hpp>
#include <nexigen/core/result.hpp>
#include <nexigen/logging/log_daemon_config.hpp>
#include <nexigen/logging/log_frame.hpp>

namespace com = nexigen::com;
namespace logging = nexigen::logging;
namespace core = nexigen::core;
namespace chrono = nexigen::chrono;

namespace {

std::atomic<bool> g_stop{false};
void signal_handler(int) { g_stop.store(true, std::memory_order_relaxed); }

/// Render one LogFrame into a JSON line in the provided buffer.
std::size_t format_frame(const logging::LogFrame& frame, char* out, std::size_t cap,
                         std::int64_t utc_offset_ns) {
    char ts_buf[31];
    const std::int64_t utc =
        chrono::utc_from_mono_ns(frame.timestamp_ns, utc_offset_ns);
    chrono::format_utc_iso8601_ns(utc, ts_buf);
    int n = std::snprintf(
        out, cap,
        R"({"app":"%.*s","level":"%s","timestamp":"%.30s","thread":%u,"pid":%u,"message":"%.*s"})" "\n",
        static_cast<int>(frame.app_id_text().size()), frame.app_id_text().data(),
        core::to_string(frame.log_level).data(),
        ts_buf,
        frame.thread_id,
        frame.pid,
        static_cast<int>(frame.payload_text().size()), frame.payload_text().data());
    return (n > 0 && static_cast<std::size_t>(n) < cap) ? static_cast<std::size_t>(n) : 0U;
}

/// Write bytes to a file descriptor (EINTR-safe).
bool write_all(int fd, const char* buf, std::size_t len) noexcept {
    std::size_t off = 0;
    while (off < len) {
        const ssize_t w = ::write(fd, buf + off, len - off);;
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
    std::snprintf(dir, sizeof(dir), "%s/%04lld-%02d-%02dT%02d",
                  log_dir.c_str(), static_cast<long long>(civil.year),
                  static_cast<int>(civil.month), static_cast<int>(civil.day),
                  static_cast<int>(civil.hour));
    ::mkdir(dir, 0755); // best-effort; ignore EEXIST

    char path[1088];
    std::snprintf(path, sizeof(path), "%s/nexigen.jsonl", dir);
    return ::open(path, O_WRONLY | O_CREAT | O_APPEND | O_CLOEXEC, 0644);
}

/// Load LogDaemonConfig from a JSON file.
core::Status load_config(const char* path,
                         logging::LogDaemonConfig& cfg) noexcept {
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

} // namespace

int main(int argc, char* argv[]) {
    const char* config_path = nullptr;
    for (int i = 1; i < argc; ++i) {
        if (std::strcmp(argv[i], "--config") == 0 && i + 1 < argc) {
            config_path = argv[++i];
        } else if (std::strcmp(argv[i], "--help") == 0) {
            std::printf("nexigen-logd - Log Daemon\nUsage: nexigen-logd --config <path>\n");
            return 0;
        }
    }
    if (config_path == nullptr) {
        std::fprintf(stderr, "nexigen-logd: missing --config <path>\n");
        return 1;
    }

    logging::LogDaemonConfig cfg;
    if (!load_config(config_path, cfg)) {
        std::fprintf(stderr, "nexigen-logd: failed to load config from %s\n", config_path);
        return 1;
    }
    if (!validate_config(cfg)) {
        std::fprintf(stderr, "nexigen-logd: invalid config\n");
        return 1;
    }

    std::signal(SIGTERM, signal_handler);
    std::signal(SIGINT, signal_handler);

    auto node = com::Node::create();
    if (!node) {
        std::fprintf(stderr, "nexigen-logd: failed to create iceoryx2 node\n");
        return 1;
    }

    std::int64_t utc_offset_ns = 0;
    if (!chrono::sample_utc_offset(utc_offset_ns)) {
        std::fprintf(stderr, "nexigen-logd: failed to sample UTC offset\n");
        return 1;
    }

    const auto svc = com::ServiceName::create(cfg.service_name.c_str());
    if (!svc) {
        std::fprintf(stderr, "nexigen-logd: invalid service name '%s'\n", cfg.service_name.c_str());
        return 1;
    }
    auto sub = com::Subscriber<logging::LogFrame>::create(*node, *svc);
    if (!sub) {
        std::fprintf(stderr, "nexigen-logd: failed to create subscriber\n");
        return 1;
    }

    const std::string ev_svc = cfg.service_name + "Event";
    const auto ev_nm = com::ServiceName::create(ev_svc.c_str());
    if (!ev_nm) {
        std::fprintf(stderr, "nexigen-logd: invalid event service name\n");
        return 1;
    }
    auto listener = com::EventListener::create(*node, *ev_nm);
    if (!listener) {
        std::fprintf(stderr, "nexigen-logd: failed to create event listener\n");
        return 1;
    }

    // Открываем первый лог-файл
    int fd = open_log_file(cfg.log_directory, utc_offset_ns);
    std::int64_t current_utc_key = 0; // будет обновлён в цикле

    constexpr std::size_t kBufSz = 65536U;
    alignas(64) char buffer[kBufSz]{};
    std::size_t used = 0;

    auto ws = com::WaitSet::create();
    if (!ws) {
        std::fprintf(stderr, "nexigen-logd: failed to create WaitSet\n");
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

    std::printf("nexigen-logd: listening on %s, writing to %s\n",
                cfg.service_name.c_str(), cfg.log_directory.c_str());

    while (!g_stop.load(std::memory_order_relaxed)) {
        const auto status = ws->wait_and_process([&](com::WaitSetAttachmentId id) {
            for (;;) {
                auto got = sub->receive();
                if (!got || !got->has_value()) break;
                const logging::LogFrame& frame = (*got)->payload();
                if (frame.log_level < cfg.min_log_level_storage) continue;

                // Проверка ротации: если час сменился — открыть новый файл
                const std::int64_t frame_utc =
                    chrono::utc_from_mono_ns(frame.timestamp_ns, utc_offset_ns);
                const chrono::CivilTime civil = chrono::civil_from_utc_ns(frame_utc);
                const std::int64_t hour_key =
                    static_cast<std::int64_t>(civil.year) * 1000000LL +
                    civil.month * 10000LL + civil.day * 100LL + civil.hour;
                if (hour_key != current_utc_key) {
                    if (fd >= 0) {
                        ::fsync(fd);
                        ::close(fd);
                    }
                    fd = open_log_file(cfg.log_directory, utc_offset_ns);
                    current_utc_key = hour_key;
                }
                if (fd < 0) continue;

                const std::size_t line_len =
                    format_frame(frame, buffer + used, kBufSz - used - 1U, utc_offset_ns);
                if (line_len == 0U) continue;
                used += line_len;
                if (used >= kBufSz / 2U) {
                    write_all(fd, buffer, used);
                    used = 0U;
                }
            }
            if (id.has_event_from(*guard_ev)) {
                // drained above
            }
            return com::CallbackProgression::Continue;
        });
        if (!status) {
            break;
        }
    }

    if (used > 0U && fd >= 0) {
        write_all(fd, buffer, used);
    }
    if (fd >= 0) {
        ::fsync(fd);
        ::close(fd);
    }
    std::printf("nexigen-logd: stopped\n");
    return 0;
}