// nexigen::chrono — JSON-lines file sink (change add-chrono-clock-service;
// spec chrono/chrono-sink "JSON-file chrono sink").
//
// Writes one JSON object per snapshot per line, rendered with std::to_chars
// into a fixed scratch buffer and written with a raw ::write. The file is
// opened during construction (the sink owns the descriptor); the write path is
// allocation-free and flush() pushes the data to the file with fsync.
#pragma once

#include <charconv>
#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <unistd.h>

#include <cerrno>
#include <fcntl.h>

#include <nexigen/chrono/chrono_snapshot.hpp>
#include <nexigen/chrono/concepts/chrono_sink_concept.hpp>
#include <nexigen/core/result.hpp>

namespace nexigen::chrono {

/// File sink configuration: the path and whether snapshots are appended.
struct JsonFileChronoSink_config {
    std::string path{};
    bool append{true};

    /// Cold-path diagnostic (mandate 9).
    [[nodiscard]] std::string to_string() const {
        return "JsonFileChronoSink_config(path=" + path +
               ", append=" + (append ? "true" : "false") + ")";
    }
};

/// Free helper (mandate 9): mirrors the member.
[[nodiscard]] inline std::string to_string(const JsonFileChronoSink_config& config) {
    return config.to_string();
}

/// A sink that appends one JSON object per snapshot to a file.
class JsonFileChronoSink {
public:
    /// Open the file. An unopenable path leaves the sink without a descriptor;
    /// on_snapshot then reports SystemError.
    explicit JsonFileChronoSink(const JsonFileChronoSink_config& config) noexcept {
        const int flags = O_WRONLY | O_CREAT | O_CLOEXEC | (config.append ? O_APPEND : O_TRUNC);
        fd_ = ::open(config.path.c_str(), flags, 0644);
    }

    JsonFileChronoSink(const JsonFileChronoSink&) = delete;
    JsonFileChronoSink& operator=(const JsonFileChronoSink&) = delete;

    /// Move transfers the descriptor and leaves the source closed (fd = -1).
    JsonFileChronoSink(JsonFileChronoSink&& other) noexcept : fd_(other.fd_) {
        other.fd_ = -1;
    }

    JsonFileChronoSink& operator=(JsonFileChronoSink&& other) noexcept {
        if (this != &other) {
            if (fd_ >= 0) {
                ::close(fd_);
            }
            fd_ = other.fd_;
            other.fd_ = -1;
        }
        return *this;
    }

    ~JsonFileChronoSink() {
        if (fd_ >= 0) {
            ::close(fd_);
            fd_ = -1;
        }
    }

    /// Write one snapshot as a single JSON line.
    [[nodiscard]] core::Status on_snapshot(const Chrono_snapshot& snapshot) noexcept {
        if (fd_ < 0) {
            return std::unexpected(core::ErrorCode::SystemError);
        }
        len_ = 0;
        append_literal("{\"stamp_ns\":");
        append_u64(snapshot.stamp.nanoseconds());
        append_literal(",\"realtime_offset_ns\":");
        append_i64(snapshot.realtime_offset_ns);
        append_literal("}\n");
        if (!write_all()) {
            return std::unexpected(core::ErrorCode::SystemError);
        }
        return {};
    }

    /// Push the data to the file.
    [[nodiscard]] core::Status flush() noexcept {
        if (fd_ < 0) {
            return std::unexpected(core::ErrorCode::SystemError);
        }
        if (::fsync(fd_) != 0) {
            return std::unexpected(core::ErrorCode::SystemError);
        }
        return {};
    }

    /// Cold-path diagnostic (mandate 9).
    [[nodiscard]] std::string to_string() const {
        return "JsonFileChronoSink(fd=" + std::to_string(fd_) + ")";
    }

    /// True when the file was opened successfully.
    [[nodiscard]] bool is_open() const noexcept { return fd_ >= 0; }

private:
    static constexpr std::size_t scratch_capacity = 96;

    void append_literal(std::string_view text) noexcept {
        const std::size_t room = (len_ < scratch_capacity) ? (scratch_capacity - len_) : 0U;
        const std::size_t take = text.size() < room ? text.size() : room;
        for (std::size_t i = 0; i < take; ++i) {
            scratch_[len_++] = text[i];
        }
    }

    void append_u64(std::uint64_t value) noexcept {
        char digits[24];
        const auto result = std::to_chars(digits, digits + sizeof(digits), value);
        if (result.ec == std::errc{}) {
            append_literal(std::string_view(digits, static_cast<std::size_t>(result.ptr - digits)));
        }
    }

    void append_i64(std::int64_t value) noexcept {
        char digits[24];
        const auto result = std::to_chars(digits, digits + sizeof(digits), value);
        if (result.ec == std::errc{}) {
            append_literal(std::string_view(digits, static_cast<std::size_t>(result.ptr - digits)));
        }
    }

    [[nodiscard]] bool write_all() noexcept {
        std::size_t offset = 0;
        while (offset < len_) {
            const ssize_t written = ::write(fd_, scratch_ + offset, len_ - offset);
            if (written > 0) {
                offset += static_cast<std::size_t>(written);
            } else if (written < 0 && errno == EINTR) {
                continue;
            } else {
                return false;
            }
        }
        return true;
    }

    int fd_{-1};
    char scratch_[scratch_capacity]{};
    std::size_t len_{0};
};

/// Free helper (mandate 9): mirrors the member.
[[nodiscard]] inline std::string to_string(const JsonFileChronoSink& sink) { return sink.to_string(); }

/// Configuration JSON (cold path).
inline void to_json(nlohmann::json& j, const JsonFileChronoSink_config& config) {
    j = nlohmann::json{{"path", config.path}, {"append", config.append}};
}

inline void from_json(const nlohmann::json& j, JsonFileChronoSink_config& out) noexcept {
    if (!j.is_object()) {
        return;
    }
    const auto it = j.find("path");
    if (it != j.end() && it->is_string()) {
        const auto* const text = it->get_ptr<const nlohmann::json::string_t*>();
        if (text != nullptr) {
            out.path = std::string(*text);
        }
    }
    const auto append_it = j.find("append");
    if (append_it != j.end() && append_it->is_boolean()) {
        out.append = append_it->get<bool>();
    }
}

/// The sink itself is not deserializable: rebinding it opens a file, which is
/// a construction-time action, not a deserialization.
inline void to_json(nlohmann::json& j, const JsonFileChronoSink& sink) {
    j = nlohmann::json{{"type", "JsonFileChronoSink"}, {"open", sink.is_open()}};
}

static_assert(concepts::ChronoSink<JsonFileChronoSink>,
              "JsonFileChronoSink must satisfy the sink contract");

} // namespace nexigen::chrono

/// The configuration formats as its diagnostic string (mandate 11).
template <>
struct std::formatter<nexigen::chrono::JsonFileChronoSink_config>
    : std::formatter<std::string_view> {
    template <class Ctx>
    auto format(const nexigen::chrono::JsonFileChronoSink_config& config, Ctx& ctx) const {
        const std::string text = config.to_string();
        return std::formatter<std::string_view>::format(text, ctx);
    }
};

/// The file sink formats as its diagnostic string (mandate 11).
template <>
struct std::formatter<nexigen::chrono::JsonFileChronoSink> : std::formatter<std::string_view> {
    template <class Ctx>
    auto format(const nexigen::chrono::JsonFileChronoSink& sink, Ctx& ctx) const {
        const std::string text = sink.to_string();
        return std::formatter<std::string_view>::format(text, ctx);
    }
};
