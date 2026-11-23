#include "export_writer.hpp"

#include <array>
#include <cerrno>
#include <chrono>
#include <cstdio>
#include <cstring>
#include <format>

#include <fcntl.h>
#include <sched.h>
#include <sys/resource.h>
#include <unistd.h>

#include <nexigen/chrono/boot_identity.hpp>
#include <nexigen/core/thread_management.hpp>

#include "json_escape.hpp"
#include "time_formatter.hpp"

namespace nexigen::logview {

namespace {

bool write_all(int fd, const char* data, std::size_t size) noexcept {
    std::size_t offset = 0;
    while (offset < size) {
        const ssize_t written = ::write(fd, data + offset, size - offset);
        if (written > 0) {
            offset += static_cast<std::size_t>(written);
        } else if (errno != EINTR) {
            return false;
        }
    }
    return true;
}

std::size_t csv_escape(std::string_view text, char* out, std::size_t capacity) noexcept {
    if (capacity < 3U) {
        return 0U;
    }
    const bool quote = text.find_first_of(",\"\n\r") != std::string_view::npos;
    std::size_t used = 0;
    if (quote) {
        out[used++] = '"';
    }
    for (const char ch : text) {
        if (ch == '"') {
            if (used + 2U >= capacity) {
                return 0U;
            }
            out[used++] = '"';
            out[used++] = '"';
        } else {
            if (used + 1U >= capacity) {
                return 0U;
            }
            out[used++] = ch;
        }
    }
    if (quote) {
        if (used + 1U >= capacity) {
            return 0U;
        }
        out[used++] = '"';
    }
    out[used] = '\0';
    return used;
}

bool affinity_allowed(const nexigen::core::CpuSet& requested) noexcept {
    if (requested.empty()) {
        return true;
    }
    cpu_set_t current{};
    if (::sched_getaffinity(0, sizeof(current), &current) != 0) {
        return false;
    }
    for (std::uint32_t cpu = 0; cpu <= nexigen::core::CpuSet::max_cpu_id; ++cpu) {
        if (!requested.contains(cpu)) {
            continue;
        }
        if (cpu >= static_cast<std::uint32_t>(CPU_SETSIZE) || !CPU_ISSET(cpu, &current)) {
            return false;
        }
    }
    return true;
}

} // namespace

std::string ExportStats::to_string() const {
    return std::format("ExportStats(queued={}, written={}, missing={}, total={}, active={}, "
                       "cancelled={}, failed={})",
                       queued, written, missing, total, active, cancelled, failed);
}

ExportWriter::~ExportWriter() {
    finished_.store(true, std::memory_order_release);
    cancelled_.store(true, std::memory_order_release);
    wake_.release();
    if (worker_.joinable()) {
        worker_.request_stop();
        worker_.join();
    }
    if (fd_ >= 0) {
        static_cast<void>(::close(fd_));
        fd_ = -1;
    }
}

nexigen::core::Status ExportWriter::start(const std::string& path, ExportFormat format,
                                          std::uint64_t total, TimeContext time,
                                          nexigen::core::ThreadSpec spec) noexcept {
    if (worker_.joinable() || fd_ >= 0) {
        return std::unexpected(nexigen::core::ErrorCode::InvalidArgument);
    }
    const auto validated = spec.validate();
    if (!validated || !affinity_allowed(spec.cpus())) {
        return std::unexpected(nexigen::core::ErrorCode::InvalidArgument);
    }
    spec_ = std::move(spec);
    fd_ = ::open(path.c_str(), O_WRONLY | O_CREAT | O_TRUNC | O_CLOEXEC, 0644);
    if (fd_ < 0) {
        return std::unexpected(nexigen::core::ErrorCode::SystemError);
    }
    format_ = format;
    total_ = total;
    time_ = time;
    finished_.store(false, std::memory_order_relaxed);
    cancelled_.store(false, std::memory_order_relaxed);
    failed_.store(false, std::memory_order_relaxed);
    written_.store(0, std::memory_order_relaxed);
    queued_.store(0, std::memory_order_relaxed);
    missing_.store(0, std::memory_order_relaxed);
    worker_ = std::jthread([this] { run(); });
    return {};
}

bool ExportWriter::try_enqueue(const ExportRecord& record) noexcept {
    if (finished_.load(std::memory_order_acquire) || cancelled_.load(std::memory_order_acquire) ||
        failed_.load(std::memory_order_relaxed)) {
        return false;
    }
    if (!queue_.try_push(record)) {
        return false;
    }
    queued_.fetch_add(1, std::memory_order_relaxed);
    wake_.release();
    return true;
}

void ExportWriter::record_missing() noexcept {
    missing_.fetch_add(1, std::memory_order_relaxed);
}

ExportStats ExportWriter::finish() noexcept {
    finished_.store(true, std::memory_order_release);
    wake_.release();
    if (worker_.joinable()) {
        worker_.join();
    }
    return stats();
}

void ExportWriter::cancel() noexcept {
    cancelled_.store(true, std::memory_order_release);
    finished_.store(true, std::memory_order_release);
    wake_.release();
    if (worker_.joinable()) {
        worker_.request_stop();
    }
}

ExportStats ExportWriter::stats() const noexcept {
    ExportStats out{};
    out.queued = queued_.load(std::memory_order_relaxed);
    out.written = written_.load(std::memory_order_relaxed);
    const std::uint64_t explicit_missing = missing_.load(std::memory_order_relaxed);
    out.missing = explicit_missing;
    out.total = total_;
    if (!out.active && out.total > out.queued) {
        const std::uint64_t absent = out.total - out.queued;
        out.missing += absent;
    }
    out.active = worker_.joinable();
    out.cancelled = cancelled_.load(std::memory_order_relaxed);
    out.failed = failed_.load(std::memory_order_relaxed);
    return out;
}

void ExportWriter::run() noexcept {
    nexigen::core::ThreadSpec effective = spec_;
    if (effective.name().empty()) {
        static_cast<void>(effective.set_name("lgview-ex"));
    }
    nexigen::core::ThreadApplyReport report{};
    static_cast<void>(nexigen::core::apply_thread_spec_self(effective, report));
    static_cast<void>(::setpriority(PRIO_PROCESS, 0, 19));
    if (fd_ < 0) {
        return;
    }
    char boot_text[nexigen::chrono::boot_token_text_size + 1U]{};
    static_cast<void>(
        nexigen::chrono::format_boot_token(nexigen::chrono::current_boot_token(), boot_text));
    if (format_ == ExportFormat::Csv) {
        static constexpr char header[] =
            "seq,timestamp,mono_ns,boot,app,level,thread_id,pid,message\n";
        if (!write_all(fd_, header, sizeof(header) - 1U)) {
            failed_.store(true, std::memory_order_relaxed);
        }
    } else if (format_ == ExportFormat::Txt) {
        static constexpr char header[] =
            "seq timestamp mono_ns boot app level thread_id pid message\n";
        if (!write_all(fd_, header, sizeof(header) - 1U)) {
            failed_.store(true, std::memory_order_relaxed);
        }
    }
    while (!failed_.load(std::memory_order_relaxed) &&
           !cancelled_.load(std::memory_order_acquire)) {
        if (!wake_.try_acquire_for(std::chrono::milliseconds(10))) {
            if (finished_.load(std::memory_order_acquire) && queue_.is_empty()) {
                break;
            }
            continue;
        }
        ExportRecord record;
        if (queue_.try_pop(record)) {
            if (!write_record(record)) {
                failed_.store(true, std::memory_order_relaxed);
                break;
            }
            written_.fetch_add(1, std::memory_order_relaxed);
        }
    }
    if (fd_ >= 0) {
        static_cast<void>(::close(fd_));
        fd_ = -1;
    }
}

bool ExportWriter::write_record(const ExportRecord& record) noexcept {
    if (fd_ < 0) {
        return false;
    }
    char boot_text[nexigen::chrono::boot_token_text_size + 1U]{};
    static_cast<void>(
        nexigen::chrono::format_boot_token(nexigen::chrono::current_boot_token(), boot_text));
    const FormattedTime stamp =
        TimeFormatter::format_timestamp(record.frame.timestamp_ns, TimestampMode::Absolute, time_);
    const std::string_view app = record.frame.app_id_text();
    const std::string_view message = record.kind == RecordKind::Frame
                                         ? record.frame.payload_text()
                                         : std::string_view("[gap] synthetic loss record");
    std::array<char, 4096> escaped{};
    std::array<char, 4096> line{};
    if (format_ == ExportFormat::Jsonl) {
        if (json_escape(message, escaped.data(), escaped.size()) == 0U) {
            return false;
        }
        const int written = std::snprintf(
            line.data(), line.size(),
            R"({"timestamp":"%.47s","mono_ns":%llu,"boot":"%.16s","app":"%.16s","level":"%.8s","thread_id":%u,"pid":%u,"seq":%llu,"delta_ns":%lld,"kind":"%.16s","message":"%s"})"
            "\n",
            stamp.text.data(), static_cast<unsigned long long>(record.frame.timestamp_ns),
            boot_text, app.data(), nexigen::core::to_string(record.frame.log_level).data(),
            record.frame.thread_id, record.frame.pid, static_cast<unsigned long long>(record.seq),
            static_cast<long long>(record.previous_visible_ns),
            nexigen::logview::to_string(record.kind).data(), escaped.data());
        return written > 0 && static_cast<std::size_t>(written) < line.size() &&
               write_all(fd_, line.data(), static_cast<std::size_t>(written));
    }
    if (format_ == ExportFormat::Csv) {
        if (csv_escape(message, escaped.data(), escaped.size()) == 0U) {
            return false;
        }
        const int written =
            std::snprintf(line.data(), line.size(), "%llu,%.47s,%llu,%.16s,%.16s,%.8s,%u,%u,%s\n",
                          static_cast<unsigned long long>(record.seq), stamp.text.data(),
                          static_cast<unsigned long long>(record.frame.timestamp_ns), boot_text,
                          app.data(), nexigen::core::to_string(record.frame.log_level).data(),
                          record.frame.thread_id, record.frame.pid, escaped.data());
        return written > 0 && static_cast<std::size_t>(written) < line.size() &&
               write_all(fd_, line.data(), static_cast<std::size_t>(written));
    }
    const int written = std::snprintf(
        line.data(), line.size(), "%llu %s %llu %.16s %.16s %.8s %u %u %.*s\n",
        static_cast<unsigned long long>(record.seq), stamp.text.data(),
        static_cast<unsigned long long>(record.frame.timestamp_ns), boot_text, app.data(),
        nexigen::core::to_string(record.frame.log_level).data(), record.frame.thread_id,
        record.frame.pid, static_cast<int>(message.size()), message.data());
    return written > 0 && static_cast<std::size_t>(written) < line.size() &&
           write_all(fd_, line.data(), static_cast<std::size_t>(written));
}

} // namespace nexigen::logview
