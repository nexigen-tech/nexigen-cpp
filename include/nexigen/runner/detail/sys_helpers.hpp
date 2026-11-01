// nexigen::runner — shared cold-path POSIX / JSON / time helpers
// (internal; refactor step 1: single source of truth for the helpers that were
// copy-pasted across runner.cpp / app_suite.cpp / instance_lifecycle.cpp /
// runner_logging.cpp).
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

#include <fcntl.h>
#include <signal.h>
#include <unistd.h>

#include <nlohmann/json.hpp>

#include <nexigen/chrono/time_format.hpp>
#include <nexigen/chrono/timestamp.hpp>
#include <nexigen/core/result.hpp>

namespace nexigen::runner::detail {

inline constexpr std::uint64_t nanoseconds_per_second = 1'000'000'000ULL;

/// Handshake message written by a supervisor before it starts reaping.
/// `pid` is the launched child (or the suite supervisor); a non-zero `error`
/// carries a `core::ErrorCode` when startup failed.
struct StartReport {
    std::int32_t pid{0};
    std::int32_t error{0};
};

[[nodiscard]] inline std::uint64_t monotonic_now_ns() noexcept {
    const core::Result<nexigen::chrono::Timestamp> now =
        nexigen::chrono::Timestamp::now();
    return now ? now->nanoseconds() : 0;
}

[[nodiscard]] inline std::string utc_now_rfc3339() {
    using namespace std::chrono;
    const auto since_epoch = system_clock::now().time_since_epoch();
    const std::int64_t epoch_ns =
        duration_cast<nanoseconds>(since_epoch).count();
    char buffer[31]{};
    nexigen::chrono::format_utc_iso8601_ns(epoch_ns, buffer);
    return std::string(buffer);
}

inline void sleep_ms(std::uint64_t milliseconds) noexcept {
    ::timespec request{};
    request.tv_sec = static_cast<time_t>(milliseconds / 1000ULL);
    request.tv_nsec =
        static_cast<long>((milliseconds % 1000ULL) * 1'000'000ULL);
    while (::nanosleep(&request, &request) != 0 && errno == EINTR) {
    }
}

[[nodiscard]] inline bool write_all(int fd, const void* data,
                                    std::size_t size) noexcept {
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

[[nodiscard]] inline bool file_exists(std::string_view path) noexcept {
    const std::string name(path);
    return ::access(name.c_str(), F_OK) == 0;
}

/// True when the PID exists and is not a zombie. `/proc/<pid>/stat` is used
/// after the kill probe so a reaped child is not mistaken for a live one.
[[nodiscard]] inline bool process_alive(std::int32_t pid) noexcept {
    if (pid <= 0) {
        return false;
    }
    if (::kill(static_cast<pid_t>(pid), 0) != 0 && errno != EPERM) {
        return false;
    }
    char path[64]{};
    static_cast<void>(
        std::snprintf(path, sizeof(path), "/proc/%d/stat", pid));
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

[[nodiscard]] inline bool group_alive(std::int32_t pgid) noexcept {
    if (pgid <= 0) {
        return false;
    }
    if (::kill(-static_cast<pid_t>(pgid), 0) == 0) {
        return true;
    }
    return errno == EPERM;
}

// --- nlohmann::json field readers (non-throwing, JSON_NOEXCEPTION-safe) ----

[[nodiscard]] inline bool read_string(const nlohmann::json& document,
                                      const char* key,
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

[[nodiscard]] inline bool read_u32(const nlohmann::json& document,
                                   const char* key,
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

[[nodiscard]] inline bool read_u64(const nlohmann::json& document,
                                   const char* key,
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

[[nodiscard]] inline bool read_i32(const nlohmann::json& document,
                                   const char* key,
                                   std::int32_t& target) noexcept {
    if (!document.is_object()) {
        return false;
    }
    const auto it = document.find(key);
    if (it == document.end() || !it->is_number_integer()) {
        return false;
    }
    const std::int64_t value = it->get<std::int64_t>();
    if (value < static_cast<std::int64_t>(
                    std::numeric_limits<std::int32_t>::min()) ||
        value > static_cast<std::int64_t>(
                    std::numeric_limits<std::int32_t>::max())) {
        return false;
    }
    target = static_cast<std::int32_t>(value);
    return true;
}

/// Reads a nullable int32 field. `present` reports whether the key was seen;
/// a wrong-typed value leaves `target` untouched and reports present=false.
[[nodiscard]] inline bool read_optional_i32(
    const nlohmann::json& document, const char* key,
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
    if (value < static_cast<std::int64_t>(
                    std::numeric_limits<std::int32_t>::min()) ||
        value > static_cast<std::int64_t>(
                    std::numeric_limits<std::int32_t>::max())) {
        return false;
    }
    target = static_cast<std::int32_t>(value);
    return true;
}

[[nodiscard]] inline bool read_bool(const nlohmann::json& document,
                                    const char* key, bool& target) noexcept {
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

// --- enum ADL serialization helpers (mandate 10) ---------------------------
//
// The per-enum `to_json`/`from_json` overloads declared in each module's public
// headers delegate here so the serialization contract lives in one place.

/// Serialize an enum as its canonical lower-case name via ADL `to_string`.
template <class Enum>
void enum_to_json(nlohmann::json& j, Enum value) {
    j = std::string(to_string(value));
}

/// Parse an enum from its canonical name via ADL `from_string`; a missing,
/// non-string or unknown value leaves `value` unchanged (non-throwing under
/// JSON_NOEXCEPTION).
template <class Enum>
void enum_from_json(const nlohmann::json& j, Enum& value) noexcept {
    const auto* const name = j.get_ptr<const nlohmann::json::string_t*>();
    if (name != nullptr) {
        static_cast<void>(from_string(std::string_view(*name), value));
    }
}

/// Read an enum field from a JSON object by key, delegating to `enum_from_json`.
/// A missing or wrong-typed key leaves `target` unchanged.
template <class Enum>
void read_enum(const nlohmann::json& document, const char* key,
               Enum& target) noexcept {
    if (!document.is_object()) {
        return;
    }
    const auto it = document.find(key);
    if (it == document.end() || !it->is_string()) {
        return;
    }
    const auto* const name = it->get_ptr<const nlohmann::json::string_t*>();
    if (name == nullptr) {
        return;
    }
    Enum parsed = target;
    enum_from_json(nlohmann::json(*name), parsed);
    target = parsed;
}

} // namespace nexigen::runner::detail
