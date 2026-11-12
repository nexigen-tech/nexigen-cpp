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

/// @brief A sink that appends one JSON object per snapshot to a file.
///
/// The file is opened during construction with the configured append policy,
/// and the sink owns that descriptor for its lifetime. Delivery renders the
/// snapshot into a fixed 96-byte scratch buffer with `std::to_chars` and writes
/// the complete line through `core::sink_io::write_all`, so the delivery path
/// is allocation-free and retries partial/`EINTR` writes. `flush()` pushes the
/// data with `fsync`.
///
/// The sink is movable (descriptor transfer) but not copyable, and it is not
/// deserializable: rebinding a sink opens a file, which is a construction-time
/// action. A single instance must not deliver concurrently from two threads.
///
/// @note Spec `chrono/chrono-sink`, requirement "JSON-file chrono sink".
class JsonFileSink {
public:
    /// @brief Open the configured output file.
    ///
    /// An unopenable path leaves the sink without a descriptor; subsequent
    /// `on_snapshot()` and `flush()` calls then report `SystemError`.
    ///
    /// @param config Path and append policy copied at construction.
    explicit JsonFileSink(const JsonFileSinkConfig& config) noexcept
        : fd_(core::sink_io::open_output(config.path, config.append)) {}

    /// @brief Copying is deleted: the sink owns a file descriptor.
    JsonFileSink(const JsonFileSink&) = delete;
    /// @brief Copy assignment is deleted: the sink owns a file descriptor.
    JsonFileSink& operator=(const JsonFileSink&) = delete;

    /// @brief Move-construct, transferring descriptor ownership.
    ///
    /// @param other Source sink; left closed with descriptor `-1`.
    JsonFileSink(JsonFileSink&& other) noexcept : fd_(other.fd_) { other.fd_ = -1; }

    /// @brief Move-assign, closing any descriptor this sink already owns.
    ///
    /// @param other Source sink; left closed with descriptor `-1`.
    /// @return `*this`.
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

    /// @brief Close the owned descriptor, if any.
    ~JsonFileSink() {
        if (fd_ >= 0) {
            ::close(fd_);
            fd_ = -1;
        }
    }

    /// @brief Write one snapshot as a single JSON line.
    ///
    /// Emits `{"stamp_ns":...,"realtime_offset_ns":...}` followed by a newline.
    ///
    /// @param snapshot Snapshot whose stamp and UTC offset are rendered.
    /// @return Success when the whole line was written, or
    ///         `core::ErrorCode::SystemError` when the file is not open or the
    ///         write fails.
    /// @note Spec `chrono/chrono-sink`, requirement "JSON-file chrono sink".
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

    /// @brief Push written data to the file with `fsync`.
    ///
    /// @return Success when `fsync` completed, or
    ///         `core::ErrorCode::SystemError` when the file is not open or the
    ///         flush fails.
    /// @note Spec `chrono/chrono-sink`, requirement "JSON-file chrono sink".
    [[nodiscard]] core::Status flush() noexcept {
        if (fd_ < 0) {
            return std::unexpected(core::ErrorCode::SystemError);
        }
        if (!core::sink_io::fsync_fd(fd_).has_value()) {
            return std::unexpected(core::ErrorCode::SystemError);
        }
        return {};
    }

    /// @brief Render the sink's diagnostic identity.
    ///
    /// @return Text of the form `JsonFileSink(fd=<descriptor>)`.
    /// @note Spec `chrono/chrono-sink`, requirement "JSON-file chrono sink";
    ///       project mandate 9.
    [[nodiscard]] std::string to_string() const {
        return "JsonFileSink(fd=" + std::to_string(fd_) + ")";
    }

    /// @brief Whether the file was opened successfully.
    ///
    /// @return `true` when the sink owns a valid descriptor.
    [[nodiscard]] bool is_open() const noexcept { return fd_ >= 0; }

private:
    /// @brief Capacity of the fixed stack scratch buffer in bytes.
    static constexpr std::size_t scratch_capacity = 96;

    /// @brief Append as many bytes as fit; overflow is dropped, never truncated mid-call.
    ///
    /// @param text View to copy into the scratch buffer.
    void append_literal(std::string_view text) noexcept {
        const std::size_t room = (len_ < scratch_capacity) ? (scratch_capacity - len_) : 0U;
        const std::size_t take = text.size() < room ? text.size() : room;
        for (std::size_t i = 0; i < take; ++i) {
            scratch_[len_++] = text[i];
        }
    }

    /// @brief Append an unsigned decimal integer using `std::to_chars`.
    ///
    /// @param value Value to render; a conversion error appends nothing.
    void append_u64(std::uint64_t value) noexcept {
        char digits[24];
        const auto result = std::to_chars(digits, digits + sizeof(digits), value);
        if (result.ec == std::errc{}) {
            append_literal(std::string_view(digits, static_cast<std::size_t>(result.ptr - digits)));
        }
    }

    /// @brief Append a signed decimal integer using `std::to_chars`.
    ///
    /// @param value Value to render; a conversion error appends nothing.
    void append_i64(std::int64_t value) noexcept {
        char digits[24];
        const auto result = std::to_chars(digits, digits + sizeof(digits), value);
        if (result.ec == std::errc{}) {
            append_literal(std::string_view(digits, static_cast<std::size_t>(result.ptr - digits)));
        }
    }

    int fd_{-1};                       ///< Owned output descriptor; `-1` when unopened.
    char scratch_[scratch_capacity]{}; ///< Fixed JSON-line buffer; never heap-allocated.
    std::size_t len_{0};               ///< Number of valid bytes currently in `scratch_`.
};

/// @brief Free-helper mirror of `JsonFileSink::to_string`.
///
/// @param sink File sink to render.
/// @return The same text as `sink.to_string()`.
/// @note Spec `chrono/chrono-sink`, requirement "JSON-file chrono sink"; project
///       mandate 9.
[[nodiscard]] inline std::string to_string(const JsonFileSink& sink) {
    return sink.to_string();
}

/// @brief Serialize the sink as diagnostic JSON (cold path).
///
/// Reports the type tag and whether the file is open. The sink itself is not
/// deserializable: rebinding it opens a file, which is a construction-time
/// action, not a deserialization.
///
/// @param j Destination JSON value, replaced by the description.
/// @param sink File sink being described.
/// @note Spec `chrono/chrono-sink`, requirement "JSON-file chrono sink"; project
///       mandate 10.
inline void to_json(nlohmann::json& j, const JsonFileSink& sink) {
    j = nlohmann::json{{"type", "JsonFileSink"}, {"open", sink.is_open()}};
}

static_assert(concepts::Sink<JsonFileSink>, "JsonFileSink must satisfy the sink contract");

} // namespace nexigen::chrono

/// @brief `std::formatter` specialization rendering the file-sink diagnostic text.
///
/// @note Spec `chrono/chrono-sink`, requirement "JSON-file chrono sink"; project
///       mandate 11.
template <>
struct std::formatter<nexigen::chrono::JsonFileSink> : std::formatter<std::string_view> {
    /// @brief Format a file sink as its diagnostic string.
    ///
    /// @tparam Ctx Format context type; the inherited string specifiers apply.
    /// @param sink File sink to format.
    /// @param ctx Format context receiving the output.
    /// @return Iterator past the last written character.
    /// @note Project mandate 11 (`std::formatter`).
    template <class Ctx> auto format(const nexigen::chrono::JsonFileSink& sink, Ctx& ctx) const {
        const std::string text = sink.to_string();
        return std::formatter<std::string_view>::format(text, ctx);
    }
};
