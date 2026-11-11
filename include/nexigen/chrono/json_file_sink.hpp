// nexigen::chrono — JSON-lines file sink (change add-chrono-clock-service;
// spec chrono/chrono-sink "JSON-file chrono sink").
//
// Writes one JSON object per snapshot per line, rendered with std::to_chars
// into a fixed scratch buffer and written with a raw ::write. The file is
// opened during construction (the sink owns the descriptor); the write path is
// allocation-free and flush() pushes the data to the file with fsync.
//
// The configuration value type (JsonFileSinkConfig) lives in
// json_file_sink_config.hpp.
#pragma once

#include <charconv>
#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <unistd.h>

#include <cerrno>
#include <fcntl.h>

#include <nlohmann/json.hpp>

#include <nexigen/chrono/json_file_sink_config.hpp>
#include <nexigen/chrono/snapshot.hpp>
#include <nexigen/chrono/concepts/sink_concept.hpp>
#include <nexigen/core/result.hpp>
#include <nexigen/core/sink_io.hpp>

namespace nexigen::chrono {

/// A sink that appends one JSON object per snapshot to a file.
class JsonFileSink {
public:
    /// Open the file. An unopenable path leaves the sink without a descriptor;
    /// on_snapshot then reports SystemError.
    explicit JsonFileSink(const JsonFileSinkConfig& config) noexcept
        : fd_(core::sink_io::open_output(config.path, config.append)) {}

    JsonFileSink(const JsonFileSink&) = delete;
    JsonFileSink& operator=(const JsonFileSink&) = delete;

    /// Move transfers the descriptor and leaves the source closed (fd = -1).
    JsonFileSink(JsonFileSink&& other) noexcept : fd_(other.fd_) { other.fd_ = -1; }

    JsonFileSink& operator=(JsonFileSink&& other) noexcept {
        if (this != &other) {
            if (fd_ >= 0) {
                ::close(fd_);
            }
            fd_ = other.fd_;
            other.fd_ = -1;
        }
        return *this;
    }

    ~JsonFileSink() {
        if (fd_ >= 0) {
            ::close(fd_);
            fd_ = -1;
        }
    }

    /// Write one snapshot as a single JSON line.
    [[nodiscard]] core::Status on_snapshot(const Snapshot& snapshot) noexcept {
        if (fd_ < 0) {
            return std::unexpected(core::ErrorCode::SystemError);
        }
        len_ = 0;
        append_literal("{\"stamp_ns\":");
        append_u64(snapshot.stamp.nanoseconds());
        append_literal(",\"realtime_offset_ns\":");
        append_i64(snapshot.realtime_offset_ns);
        append_literal("}\n");
        if (!core::sink_io::write_all(fd_, scratch_, len_).has_value()) {
            return std::unexpected(core::ErrorCode::SystemError);
        }
        return {};
    }

    /// Push the data to the file.
    [[nodiscard]] core::Status flush() noexcept {
        if (fd_ < 0) {
            return std::unexpected(core::ErrorCode::SystemError);
        }
        if (!core::sink_io::fsync_fd(fd_).has_value()) {
            return std::unexpected(core::ErrorCode::SystemError);
        }
        return {};
    }

    /// Cold-path diagnostic (mandate 9).
    [[nodiscard]] std::string to_string() const {
        return "JsonFileSink(fd=" + std::to_string(fd_) + ")";
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

    int fd_{-1};
    char scratch_[scratch_capacity]{};
    std::size_t len_{0};
};

/// Free helper (mandate 9): mirrors the member.
[[nodiscard]] inline std::string to_string(const JsonFileSink& sink) {
    return sink.to_string();
}

/// The sink itself is not deserializable: rebinding it opens a file, which is
/// a construction-time action, not a deserialization.
inline void to_json(nlohmann::json& j, const JsonFileSink& sink) {
    j = nlohmann::json{{"type", "JsonFileSink"}, {"open", sink.is_open()}};
}

static_assert(concepts::Sink<JsonFileSink>, "JsonFileSink must satisfy the sink contract");

} // namespace nexigen::chrono

/// The file sink formats as its diagnostic string (mandate 11).
template <>
struct std::formatter<nexigen::chrono::JsonFileSink> : std::formatter<std::string_view> {
    template <class Ctx> auto format(const nexigen::chrono::JsonFileSink& sink, Ctx& ctx) const {
        const std::string text = sink.to_string();
        return std::formatter<std::string_view>::format(text, ctx);
    }
};
