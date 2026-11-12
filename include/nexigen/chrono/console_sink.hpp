// nexigen::chrono — console sink (change add-chrono-clock-service; spec
// chrono/chrono-sink "Console chrono sink").
//
// Renders every snapshot as one human-readable line and emits it with a raw
// ::write to the configured descriptor (stdout by default). Deliberately NOT
// iostreams: their lazy initialization, buffering and locale state allocate
// and can block.
//
// The rendering uses fixed stack buffers and std::to_chars (allocation-free
// numerics), so a snapshot never touches the heap. Partial writes and EINTR
// are retried; an unrecoverable write error is reported as SystemError.
#pragma once

#include <charconv>
#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <unistd.h>

#include <cerrno>

#include <nexigen/chrono/snapshot.hpp>
#include <nexigen/chrono/concepts/sink_concept.hpp>
#include <nexigen/core/result.hpp>

namespace nexigen::chrono {

/// @brief A sink that writes human-readable snapshot lines to a file descriptor.
///
/// Each snapshot becomes one line of the form
/// `stamp_ns=<n> realtime_offset_ns=<signed>` emitted with raw `::write` to
/// `descriptor` (stdout by default). The class deliberately avoids iostreams,
/// whose lazy initialization, buffering, and locale state allocate and can
/// block; rendering uses a fixed stack buffer and `std::to_chars`, so delivery
/// never touches the heap. Partial writes and `EINTR` are retried.
///
/// Ownership: the descriptor is borrowed and never closed by this sink. The
/// class holds a mutable render buffer, so a single instance must not be used
/// concurrently by two delivering threads.
///
/// @note Spec `chrono/chrono-sink`, requirement "Console chrono sink".
class ConsoleSink {
public:
    /// @brief Construct a console sink over an existing descriptor.
    ///
    /// @param descriptor File descriptor receiving the lines; defaults to
    ///        `STDOUT_FILENO`. It must stay open for the sink's lifetime.
    explicit ConsoleSink(int descriptor = STDOUT_FILENO) noexcept : fd_(descriptor) {}

    /// @brief Render one snapshot and write it as a single line.
    ///
    /// The render is allocation-free and goes through one full-write loop.
    ///
    /// @param snapshot Snapshot whose monotonic stamp and UTC offset are rendered.
    /// @return Success when every byte was written, or
    ///         `core::ErrorCode::SystemError` when the descriptor reports an
    ///         unrecoverable write error.
    /// @note Spec `chrono/chrono-sink`, requirement "Console chrono sink".
    [[nodiscard]] core::Status on_snapshot(const Snapshot& snapshot) noexcept {
        len_ = 0;
        append_str("stamp_ns=");
        append_u64(snapshot.stamp.nanoseconds());
        append_str(" realtime_offset_ns=");
        append_i64(snapshot.realtime_offset_ns);
        append_char('\n');
        if (!write_all()) {
            return std::unexpected(core::ErrorCode::SystemError);
        }
        return {};
    }

    /// @brief Flush the sink.
    ///
    /// Console output is unbuffered: every `on_snapshot` write is already
    /// complete, so this is a no-op kept for the sink contract.
    ///
    /// @return Always success.
    /// @note Spec `chrono/chrono-sink`, requirement "Console chrono sink".
    [[nodiscard]] core::Status flush() noexcept { return {}; }

    /// @brief Render the sink's diagnostic identity.
    ///
    /// @return Text of the form `ConsoleSink(fd=<descriptor>)`.
    /// @note Spec `chrono/chrono-sink`, requirement "Console chrono sink";
    ///       project mandate 9.
    [[nodiscard]] std::string to_string() const {
        return "ConsoleSink(fd=" + std::to_string(fd_) + ")";
    }

    /// @brief The destination file descriptor.
    ///
    /// @return The borrowed descriptor supplied at construction.
    [[nodiscard]] int descriptor() const noexcept { return fd_; }

private:
    /// @brief Capacity of the fixed stack line buffer in bytes.
    static constexpr std::size_t line_capacity = 128;

    /// @brief Append one character while capacity remains; overflow is dropped.
    ///
    /// @param c Character to append.
    void append_char(char c) noexcept {
        if (len_ < line_capacity) {
            line_[len_++] = c;
        }
    }

    /// @brief Append as many bytes as fit; overflow is dropped, never truncated mid-call.
    ///
    /// @param text View to copy into the line buffer.
    void append_str(std::string_view text) noexcept {
        const std::size_t room = (len_ < line_capacity) ? (line_capacity - len_) : 0U;
        const std::size_t take = text.size() < room ? text.size() : room;
        for (std::size_t i = 0; i < take; ++i) {
            line_[len_++] = text[i];
        }
    }

    /// @brief Append an unsigned decimal integer using `std::to_chars`.
    ///
    /// @param value Value to render; a conversion error appends nothing.
    void append_u64(std::uint64_t value) noexcept {
        char digits[24];
        const auto result = std::to_chars(digits, digits + sizeof(digits), value);
        if (result.ec == std::errc{}) {
            append_str(std::string_view(digits, static_cast<std::size_t>(result.ptr - digits)));
        }
    }

    /// @brief Append a signed decimal integer using `std::to_chars`.
    ///
    /// @param value Value to render; a conversion error appends nothing.
    void append_i64(std::int64_t value) noexcept {
        char digits[24];
        const auto result = std::to_chars(digits, digits + sizeof(digits), value);
        if (result.ec == std::errc{}) {
            append_str(std::string_view(digits, static_cast<std::size_t>(result.ptr - digits)));
        }
    }

    /// @brief Write the buffered line completely, retrying partial writes and `EINTR`.
    ///
    /// @return `true` when every buffered byte reached the descriptor; `false`
    ///         on an unrecoverable write error.
    [[nodiscard]] bool write_all() noexcept {
        std::size_t offset = 0;
        while (offset < len_) {
            const ssize_t written = ::write(fd_, line_ + offset, len_ - offset);
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

    int fd_;                     ///< Borrowed destination descriptor.
    char line_[line_capacity]{}; ///< Fixed render buffer; never heap-allocated.
    std::size_t len_{0};         ///< Number of valid bytes currently in `line_`.
};

/// @brief Free-helper mirror of `ConsoleSink::to_string`.
///
/// @param sink Console sink to render.
/// @return The same text as `sink.to_string()`.
/// @note Spec `chrono/chrono-sink`, requirement "Console chrono sink"; project
///       mandate 9.
[[nodiscard]] inline std::string to_string(const ConsoleSink& sink) {
    return sink.to_string();
}

/// @brief Serialize the console sink as diagnostic JSON (cold path).
///
/// The sink round-trips its descriptor. The body lives in `console_sink.cpp`.
///
/// @param j Destination JSON value, replaced by the serialized sink.
/// @param sink Sink to serialize.
/// @note Spec `chrono/chrono-sink`, requirement "Console chrono sink"; project
///       mandate 10.
void to_json(nlohmann::json& j, const ConsoleSink& sink);
/// @brief Restore the console sink from JSON, strictly non-throwing.
///
/// Reads the integer `fd` field; a missing or non-integer field leaves the sink
/// unchanged. The body lives in `console_sink.cpp`.
///
/// @param j Source JSON value.
/// @param sink Destination sink; replaced only by a valid integer `fd`.
/// @note Spec `chrono/chrono-sink`, requirement "Console chrono sink"; project
///       mandate 10.
void from_json(const nlohmann::json& j, ConsoleSink& sink) noexcept;

static_assert(concepts::Sink<ConsoleSink>, "ConsoleSink must satisfy the sink contract");

} // namespace nexigen::chrono

/// @brief `std::formatter` specialization rendering the console-sink diagnostic text.
///
/// @note Spec `chrono/chrono-sink`, requirement "Console chrono sink"; project
///       mandate 11.
template <> struct std::formatter<nexigen::chrono::ConsoleSink> : std::formatter<std::string_view> {
    /// @brief Format the sink as its diagnostic string.
    ///
    /// @tparam Ctx Format context type; the inherited string specifiers apply.
    /// @param sink Console sink to format.
    /// @param ctx Format context receiving the output.
    /// @return Iterator past the last written character.
    /// @note Project mandate 11 (`std::formatter`).
    template <class Ctx> auto format(const nexigen::chrono::ConsoleSink& sink, Ctx& ctx) const {
        const std::string text = sink.to_string();
        return std::formatter<std::string_view>::format(text, ctx);
    }
};
