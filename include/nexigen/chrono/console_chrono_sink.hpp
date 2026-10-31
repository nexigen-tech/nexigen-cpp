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

#include <nexigen/chrono/chrono_snapshot.hpp>
#include <nexigen/chrono/concepts/chrono_sink_concept.hpp>
#include <nexigen/core/result.hpp>

namespace nexigen::chrono {

/// A sink that writes human-readable snapshot lines to a file descriptor.
class ConsoleChronoSink {
public:
    explicit ConsoleChronoSink(int descriptor = STDOUT_FILENO) noexcept : fd_(descriptor) {}

    /// Render and write one snapshot.
    [[nodiscard]] core::Status on_snapshot(const Chrono_snapshot& snapshot) noexcept {
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

    /// Console output is unbuffered: every write is already complete.
    [[nodiscard]] core::Status flush() noexcept { return {}; }

    /// Cold-path diagnostic (mandate 9).
    [[nodiscard]] std::string to_string() const {
        return "ConsoleChronoSink(fd=" + std::to_string(fd_) + ")";
    }

    /// The destination descriptor.
    [[nodiscard]] int descriptor() const noexcept { return fd_; }

private:
    static constexpr std::size_t line_capacity = 128;

    void append_char(char c) noexcept {
        if (len_ < line_capacity) {
            line_[len_++] = c;
        }
    }

    void append_str(std::string_view text) noexcept {
        const std::size_t room = (len_ < line_capacity) ? (line_capacity - len_) : 0U;
        const std::size_t take = text.size() < room ? text.size() : room;
        for (std::size_t i = 0; i < take; ++i) {
            line_[len_++] = text[i];
        }
    }

    void append_u64(std::uint64_t value) noexcept {
        char digits[24];
        const auto result = std::to_chars(digits, digits + sizeof(digits), value);
        if (result.ec == std::errc{}) {
            append_str(std::string_view(digits, static_cast<std::size_t>(result.ptr - digits)));
        }
    }

    void append_i64(std::int64_t value) noexcept {
        char digits[24];
        const auto result = std::to_chars(digits, digits + sizeof(digits), value);
        if (result.ec == std::errc{}) {
            append_str(std::string_view(digits, static_cast<std::size_t>(result.ptr - digits)));
        }
    }

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

    int fd_;
    char line_[line_capacity]{};
    std::size_t len_{0};
};

/// Free helper (mandate 9): mirrors the member.
[[nodiscard]] inline std::string to_string(const ConsoleChronoSink& sink) { return sink.to_string(); }

/// Diagnostic JSON (cold path): the sink round-trips its descriptor.
inline void to_json(nlohmann::json& j, const ConsoleChronoSink& sink) {
    j = nlohmann::json{{"type", "ConsoleChronoSink"}, {"fd", sink.descriptor()}};
}

inline void from_json(const nlohmann::json& j, ConsoleChronoSink& sink) noexcept {
    const auto it = j.find("fd");
    if (it != j.end() && it->is_number_integer()) {
        const auto value = it->get<std::int64_t>();
        sink = ConsoleChronoSink{static_cast<int>(value)};
    }
}

static_assert(concepts::ChronoSink<ConsoleChronoSink>,
              "ConsoleChronoSink must satisfy the sink contract");

} // namespace nexigen::chrono

/// The console sink formats as its diagnostic string (mandate 11).
template <>
struct std::formatter<nexigen::chrono::ConsoleChronoSink> : std::formatter<std::string_view> {
    template <class Ctx>
    auto format(const nexigen::chrono::ConsoleChronoSink& sink, Ctx& ctx) const {
        const std::string text = sink.to_string();
        return std::formatter<std::string_view>::format(text, ctx);
    }
};
