// nexigen::exec — shared cold-path POSIX / JSON / time helpers
// (internal; refactor step 1: single source of truth for the helpers that were
// copy-pasted across execution_manager.cpp / app_suite.cpp / instance_lifecycle.cpp /
// execution_manager_logging.cpp).
//
// COLD PATH ONLY. These functions run during process supervision, manifest
// parsing and instance metadata I/O — never in an application tick, producer,
// snapshot or sink callback. nlohmann::json, std::string and file I/O are
// allowed here by design.
#pragma once

#include <cerrno>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <limits>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include <fcntl.h>
#include <signal.h>
#include <sys/wait.h>
#include <unistd.h>

#include <nlohmann/json.hpp>

#include <nexigen/chrono/civil_time.hpp>
#include <nexigen/chrono/timestamp.hpp>
#include <nexigen/core/result.hpp>
#include <nexigen/exec/detail/start_report.hpp>

namespace nexigen::exec::detail {

/// @brief Nanoseconds in one second, used to build supervision deadlines.
inline constexpr std::uint64_t nanoseconds_per_second = 1'000'000'000ULL;

/// @brief Read the monotonic clock in nanoseconds.
///
/// @return `Timestamp::now()` in nanoseconds, or 0 when the clock read fails.
/// @note Spec `exec/process-supervision`, requirement "`supervise_child`
///       delegates to wait helpers" (deadline waits).
[[nodiscard]] inline std::uint64_t monotonic_now_ns() noexcept {
    const core::Result<nexigen::chrono::Timestamp> now = nexigen::chrono::Timestamp::now();
    return now ? now->nanoseconds() : 0;
}

/// @brief Current UTC instant formatted as RFC 3339 with nanoseconds.
///
/// @return An ISO-8601 UTC timestamp such as `2026-11-12T10:00:00.000000000Z`.
/// @note Spec `exec/instance-lifecycle`, requirement "resource.json captures
///       start-time metadata".
[[nodiscard]] inline std::string utc_now_rfc3339() {
    using namespace std::chrono;
    const auto since_epoch = system_clock::now().time_since_epoch();
    const std::int64_t epoch_ns = duration_cast<nanoseconds>(since_epoch).count();
    char buffer[31]{};
    nexigen::chrono::format_utc_iso8601_ns(epoch_ns, buffer);
    return std::string(buffer);
}

/// @brief Sleep for a whole number of milliseconds.
///
/// Restarts the sleep on `EINTR` so a signal does not shorten the intended
/// delay.
///
/// @param milliseconds Delay length; 0 returns immediately.
/// @note Spec `exec/app-suite`, requirement "Restarts follow abnormal
///       termination only" (restart delay).
inline void sleep_ms(std::uint64_t milliseconds) noexcept {
    ::timespec request{};
    request.tv_sec = static_cast<time_t>(milliseconds / 1000ULL);
    request.tv_nsec = static_cast<long>((milliseconds % 1000ULL) * 1'000'000ULL);
    while (::nanosleep(&request, &request) != 0 && errno == EINTR) {}
}

/// @brief Write the whole buffer to a file descriptor.
///
/// Retries on `EINTR` and short writes until `size` bytes are written.
///
/// @param fd Destination descriptor.
/// @param data Buffer to write.
/// @param size Number of bytes in `data`.
/// @return `true` when every byte was written; `false` on a non-`EINTR` error.
[[nodiscard]] inline bool write_all(int fd, const void* data, std::size_t size) noexcept {
    const auto* bytes = static_cast<const char*>(data);
    std::size_t written = 0;
    while (written < size) {
        const ssize_t count = ::write(fd, bytes + written, size - written);
        if (count <= 0) {
            if (count < 0 && errno == EINTR) {
                continue;
            }
            return false;
        }
        written += static_cast<std::size_t>(count);
    }
    return true;
}

/// @brief Point stdin/stdout/stderr at an already-open `/dev/null` descriptor.
///
/// @param null_fd Open descriptor for `/dev/null`; must remain open during the
///        call.
/// @note Spec `exec/app-suite`, requirement "`start_suite` is split into
///       validation, conflict check, and supervisor launch" (supervisor image
///       setup).
inline void redirect_to_null(int null_fd) noexcept {
    static_cast<void>(::dup2(null_fd, STDIN_FILENO));
    static_cast<void>(::dup2(null_fd, STDOUT_FILENO));
    static_cast<void>(::dup2(null_fd, STDERR_FILENO));
}

/// @brief Write the whole buffer while shielding the write from `SIGPIPE`.
///
/// Handshake/report writes are the one IPC family without a recv-style
/// `MSG_NOSIGNAL` (analysis #23): save the old `SIGPIPE` disposition, write,
/// restore. The restore matters because a `SIG_IGN` disposition would survive
/// `exec` into the launched application; a handler reset would not.
///
/// @param fd Destination descriptor.
/// @param data Buffer to write.
/// @param size Number of bytes in `data`.
/// @return `true` when every byte was written; `false` on a write error.
/// @note Spec `exec/process-supervision`, requirement "A failed start reaps the
///       supervisor child".
[[nodiscard]] inline bool write_all_no_sigpipe(int fd, const void* data,
                                               std::size_t size) noexcept {
    void (*const saved)(int) = ::signal(SIGPIPE, SIG_IGN);
    const bool ok = write_all(fd, data, size);
    static_cast<void>(::signal(SIGPIPE, saved == SIG_ERR ? SIG_DFL : saved));
    return ok;
}

/// @brief Read exactly `size` bytes from a file descriptor.
///
/// Retries on `EINTR`; a premature EOF is a failure.
///
/// @param fd Source descriptor.
/// @param data Destination buffer with room for `size` bytes.
/// @param size Number of bytes expected.
/// @return `true` when every byte was read; `false` on EOF or a read error.
/// @note Spec `exec/process-supervision`, requirement "Setup handshake
///       completes only after a successful exec" (EOF is the success signal).
[[nodiscard]] inline bool read_all(int fd, void* data, std::size_t size) noexcept {
    auto* bytes = static_cast<char*>(data);
    std::size_t read_bytes = 0;
    while (read_bytes < size) {
        const ssize_t count = ::read(fd, bytes + read_bytes, size - read_bytes);
        if (count == 0) {
            return false;
        }
        if (count < 0) {
            if (errno == EINTR) {
                continue;
            }
            return false;
        }
        read_bytes += static_cast<std::size_t>(count);
    }
    return true;
}

/// @brief Whether a filesystem entry exists.
///
/// @param path Path to probe with `access(F_OK)`.
/// @return `true` when the path exists.
[[nodiscard]] inline bool file_exists(std::string_view path) noexcept {
    const std::string name(path);
    return ::access(name.c_str(), F_OK) == 0;
}

/// @brief Validate child-only environment entries.
///
/// Requires non-empty keys without `=` or embedded NUL, and values without
/// embedded NUL.
///
/// @param env Caller-supplied name/value pairs.
/// @return Success, or `InvalidArgument` for the first invalid entry.
/// @note Spec `exec/process-supervision`, requirement "Supervisor environment
///       rejects NUL in values".
[[nodiscard]] inline core::Status
validate_env_entries(const std::vector<std::pair<std::string, std::string>>& env) noexcept {
    for (const auto& [key, value] : env) {
        if (key.empty() || key.find('=') != std::string::npos ||
            key.find('\0') != std::string::npos || value.find('\0') != std::string::npos) {
            return std::unexpected(core::ErrorCode::InvalidArgument);
        }
    }
    return {};
}

/// @brief Whether a pid exists and is not a zombie.
///
/// `/proc/<pid>/stat` is consulted after the `kill(pid, 0)` probe so a reaped
/// child is not mistaken for a live one; `EPERM` counts as alive.
///
/// @param pid Process id to probe.
/// @return `true` when the process exists and its state is not `Z`, `X`, or
///         `x`; `false` for a non-positive pid or a missing process.
/// @note Spec `exec/process-supervision`, requirement "Status reports
///       lifecycle state".
[[nodiscard]] inline bool process_alive(std::int32_t pid) noexcept {
    if (pid <= 0) {
        return false;
    }
    if (::kill(static_cast<pid_t>(pid), 0) != 0 && errno != EPERM) {
        return false;
    }
    char path[64]{};
    static_cast<void>(std::snprintf(path, sizeof(path), "/proc/%d/stat", pid));
    const int fd = ::open(path, O_RDONLY | O_CLOEXEC);
    if (fd < 0) {
        return true;
    }
    char buffer[512]{};
    const ssize_t count = ::read(fd, buffer, sizeof(buffer) - 1);
    static_cast<void>(::close(fd));
    if (count <= 0) {
        return true;
    }
    buffer[count] = '\0';
    const char* const last_paren = std::strrchr(buffer, ')');
    if (last_paren == nullptr || last_paren[1] != ' ') {
        return true;
    }
    const char state = last_paren[2];
    return state != 'Z' && state != 'X' && state != 'x';
}

/// @brief Whether any process remains in a process group.
///
/// @param pgid Process group id; non-positive values are not alive.
/// @return `true` when `kill(-pgid, 0)` succeeds or reports `EPERM`.
/// @note Spec `exec/process-supervision`, requirement "Stop delivers SIGTERM
///       then SIGKILL on timeout".
[[nodiscard]] inline bool group_alive(std::int32_t pgid) noexcept {
    if (pgid <= 0) {
        return false;
    }
    if (::kill(-static_cast<pid_t>(pgid), 0) == 0) {
        return true;
    }
    return errno == EPERM;
}

/// @brief Linux start-ticks of a process: `/proc/<pid>/stat` field 22.
///
/// Returns the `starttime` value (clock ticks since boot). A readable value is
/// exact and stable, comparable ticks-to-ticks without any clock-domain math;
/// 0 means unreadable, and the caller falls back to the legacy pid-only check.
///
/// @param pid Process id to inspect.
/// @return The starttime ticks, or 0 when the pid is invalid or `/proc` cannot
///         be parsed.
/// @note Spec `exec/process-supervision`, requirement "Identity is verified
///       before signals are sent" (PID-recycling protection).
/// @note Spec `exec/instance-lifecycle`, requirement "resource.json captures
///       start-time metadata".
[[nodiscard]] inline std::uint64_t proc_start_ticks(std::int32_t pid) noexcept {
    if (pid <= 0) {
        return 0;
    }
    char path[64]{};
    static_cast<void>(std::snprintf(path, sizeof(path), "/proc/%d/stat", pid));
    const int fd = ::open(path, O_RDONLY | O_CLOEXEC);
    if (fd < 0) {
        return 0;
    }
    char buffer[512]{};
    const ssize_t count = ::read(fd, buffer, sizeof(buffer) - 1);
    static_cast<void>(::close(fd));
    if (count <= 0) {
        return 0;
    }
    buffer[count] = '\0';
    // comm (field 2) may contain any character; fields continue after the
    // LAST ')'. Time fields stay stable across all supported kernels.
    constexpr std::uint32_t starttime_field = 22;
    // After the paren, field 3 (state) starts; field 22 is the 20th token.
    constexpr std::uint32_t tokens_before_starttime = starttime_field - 3;
    const char* const last_paren = std::strrchr(buffer, ')');
    if (last_paren == nullptr || last_paren[1] != ' ') {
        return 0;
    }
    const char* cursor = last_paren + 2; // first char of field 3 (state)
    for (std::uint32_t token = 0; token < tokens_before_starttime; ++token) {
        const char* const separator = std::strchr(cursor, ' ');
        if (separator == nullptr) {
            return 0;
        }
        cursor = separator + 1;
    }
    char* end = nullptr;
    const long long value = std::strtoll(cursor, &end, 10);
    if (end == cursor || value <= 0) {
        return 0;
    }
    return static_cast<std::uint64_t>(value);
}

/// @brief Identity gate before signals are sent.
///
/// True when the recorded anchor matches the live process. A 0 anchor
/// (pre-anchor writer) or an unreadable `/proc` keeps the legacy single-check
/// behavior; only a readable, differing pair counts as recycled.
///
/// @param pid Recorded process id.
/// @param recorded_ticks `proc_start_ticks` value from `resource.json`.
/// @return `true` when the target is trusted and may be signalled.
/// @note Spec `exec/process-supervision`, requirement "Identity is verified
///       before signals are sent".
[[nodiscard]] inline bool identity_matches(std::int32_t pid,
                                           std::uint64_t recorded_ticks) noexcept {
    if (pid <= 0 || recorded_ticks == 0) {
        return true; // legacy resource.json — previous trust level stands
    }
    const std::uint64_t live_ticks = proc_start_ticks(pid);
    return live_ticks == 0 || live_ticks == recorded_ticks;
}

/// @brief Reap a supervisor child that reported a failed handshake.
///
/// Bounded non-blocking drain: the supervisor exits self-terminally after the
/// report on every failure path, and the launcher never blocks forever, so a
/// hung supervisor under fault injection cannot stall it.
///
/// @param supervisor Supervisor pid to reap.
/// @note Spec `exec/process-supervision`, requirement "A failed start reaps the
///       supervisor child".
/// @warning Waits at most five seconds, polling every 5 ms.
inline void reap_failed_supervisor(pid_t supervisor) noexcept {
    const std::uint64_t deadline = monotonic_now_ns() + 5ULL * nanoseconds_per_second;
    int status = 0;
    for (;;) {
        const pid_t reaped = ::waitpid(supervisor, &status, WNOHANG);
        if (reaped == supervisor) {
            return;
        }
        if (reaped < 0 && errno != EINTR) {
            return; // already reaped elsewhere (ECHILD etc.) — done
        }
        if (monotonic_now_ns() >= deadline) {
            return; // abnormal miss: launcher still returns, never blocks
        }
        sleep_ms(5);
    }
}

/// @brief Write one `StartReport` handshake record.
///
/// Shields the write from `SIGPIPE` in case the launcher has already closed
/// its end of the pipe.
///
/// @param fd Write end of the start-report pipe.
/// @param report Record to write.
/// @return `true` when the record was written.
/// @note Spec `exec/process-supervision`, requirement "Setup handshake
///       completes only after a successful exec".
inline bool write_start_report(int fd, StartReport report) noexcept {
    return write_all_no_sigpipe(fd, &report, sizeof(report));
}

/// @brief Read the supervisor's `StartReport` and close the read end.
///
/// A short read, a non-zero error, or a non-positive pid is a failed start: the
/// supervisor child is reaped so no zombie survives.
///
/// @param read_fd Read end of the start-report pipe; closed by this call.
/// @param supervisor Supervisor pid to reap when the report is invalid.
/// @return Success, or `SystemError` for a missing or failed handshake.
/// @note Spec `exec/process-supervision`, requirement "A failed start reaps the
///       supervisor child".
/// @note Spec `exec/process-supervision`, requirement "A failed exec fails the
///       start visibly".
[[nodiscard]] inline core::Status read_start_report(int read_fd, pid_t supervisor) noexcept {
    StartReport report{};
    const bool received = read_all(read_fd, &report, sizeof(report));
    static_cast<void>(::close(read_fd));
    if (!received || report.error != 0 || report.pid <= 0) {
        reap_failed_supervisor(supervisor);
        return std::unexpected(core::ErrorCode::SystemError);
    }
    return {};
}

// --- nlohmann::json field readers (non-throwing, JSON_NOEXCEPTION-safe) ----

/// @brief Read a string field from a JSON object.
///
/// @param document Source object; non-objects are rejected.
/// @param key Field name.
/// @param target Receives the text only on success.
/// @return `true` when the field exists and is a string.
/// @note Spec `exec/app-suite`, scenario "Hostile suite JSON is ignored".
[[nodiscard]] inline bool read_string(const nlohmann::json& document, const char* key,
                                      std::string& target) noexcept {
    if (!document.is_object()) {
        return false;
    }
    const auto it = document.find(key);
    if (it == document.end() || !it->is_string()) {
        return false;
    }
    const auto* const text = it->get_ptr<const nlohmann::json::string_t*>();
    if (text == nullptr) {
        return false;
    }
    target = *text;
    return true;
}

/// @brief Read an unsigned 32-bit field from a JSON object.
///
/// @param document Source object; non-objects are rejected.
/// @param key Field name.
/// @param target Receives the value only on success.
/// @return `true` when the field is an unsigned number that fits `uint32_t`.
/// @note Spec `exec/app-suite`, scenario "Hostile suite JSON is ignored".
[[nodiscard]] inline bool read_u32(const nlohmann::json& document, const char* key,
                                   std::uint32_t& target) noexcept {
    if (!document.is_object()) {
        return false;
    }
    const auto it = document.find(key);
    if (it == document.end() || !it->is_number_unsigned()) {
        return false;
    }
    const std::uint64_t value = it->get<std::uint64_t>();
    if (value > UINT32_MAX) {
        return false;
    }
    target = static_cast<std::uint32_t>(value);
    return true;
}

/// @brief Read an unsigned 64-bit field from a JSON object.
///
/// @param document Source object; non-objects are rejected.
/// @param key Field name.
/// @param target Receives the value only on success.
/// @return `true` when the field exists and is an unsigned number.
/// @note Spec `exec/app-suite`, scenario "Hostile suite JSON is ignored".
[[nodiscard]] inline bool read_u64(const nlohmann::json& document, const char* key,
                                   std::uint64_t& target) noexcept {
    if (!document.is_object()) {
        return false;
    }
    const auto it = document.find(key);
    if (it == document.end() || !it->is_number_unsigned()) {
        return false;
    }
    target = it->get<std::uint64_t>();
    return true;
}

/// @brief Read a signed 32-bit field from a JSON object.
///
/// @param document Source object; non-objects are rejected.
/// @param key Field name.
/// @param target Receives the value only on success.
/// @return `true` when the field is an integer that fits `int32_t`.
/// @note Spec `exec/app-suite`, scenario "Hostile suite JSON is ignored".
[[nodiscard]] inline bool read_i32(const nlohmann::json& document, const char* key,
                                   std::int32_t& target) noexcept {
    if (!document.is_object()) {
        return false;
    }
    const auto it = document.find(key);
    if (it == document.end() || !it->is_number_integer()) {
        return false;
    }
    const std::int64_t value = it->get<std::int64_t>();
    if (value < static_cast<std::int64_t>(std::numeric_limits<std::int32_t>::min()) ||
        value > static_cast<std::int64_t>(std::numeric_limits<std::int32_t>::max())) {
        return false;
    }
    target = static_cast<std::int32_t>(value);
    return true;
}

/// @brief Read a nullable signed 32-bit field from a JSON object.
///
/// A JSON `null` clears `target`; a wrong-typed value leaves `target` untouched
/// and returns `false`.
///
/// @param document Source object; non-objects are rejected.
/// @param key Field name.
/// @param target Receives the value or `std::nullopt` only on success.
/// @return `true` when the key exists and is a null or an in-range integer.
/// @note Spec `exec/instance-lifecycle`, requirement "exit.json captures exit
///       metadata".
[[nodiscard]] inline bool read_optional_i32(const nlohmann::json& document, const char* key,
                                            std::optional<std::int32_t>& target) noexcept {
    if (!document.is_object()) {
        return false;
    }
    const auto it = document.find(key);
    if (it == document.end()) {
        return false;
    }
    if (it->is_null()) {
        target = std::nullopt;
        return true;
    }
    if (!it->is_number_integer()) {
        return false;
    }
    const std::int64_t value = it->get<std::int64_t>();
    if (value < static_cast<std::int64_t>(std::numeric_limits<std::int32_t>::min()) ||
        value > static_cast<std::int64_t>(std::numeric_limits<std::int32_t>::max())) {
        return false;
    }
    target = static_cast<std::int32_t>(value);
    return true;
}

/// @brief Read a boolean field from a JSON object.
///
/// @param document Source object; non-objects are rejected.
/// @param key Field name.
/// @param target Receives the value only on success.
/// @return `true` when the field exists and is a boolean.
/// @note Spec `exec/app-suite`, scenario "Hostile suite JSON is ignored".
[[nodiscard]] inline bool read_bool(const nlohmann::json& document, const char* key,
                                    bool& target) noexcept {
    if (!document.is_object()) {
        return false;
    }
    const auto it = document.find(key);
    if (it == document.end() || !it->is_boolean()) {
        return false;
    }
    target = it->get<bool>();
    return true;
}

// --- enum ADL serialization helpers: moved to nexigen::core::mandate -------
// (change refactor-enum-traits-helper). `enum_to_json`/`enum_from_json`/
// `read_enum` now live once in <nexigen/core/enum_traits.hpp>; every module’s
// public enum glue delegates there.

} // namespace nexigen::exec::detail
