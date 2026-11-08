// nexigen::hub — StreamKind enum.
#pragma once
#include <cstdint>
#include <string>
#include <string_view>

namespace nexigen::hub {

enum class StreamKind : std::uint8_t {
    App,
    Metrics,
    Traces,
    Chrono,
    Stdout,
    Stderr,
    Runner,
};

[[nodiscard]] constexpr std::string_view to_string(StreamKind kind) noexcept {
    switch (kind) {
        case StreamKind::App:     return "app";
        case StreamKind::Metrics: return "metrics";
        case StreamKind::Traces:  return "traces";
        case StreamKind::Chrono:  return "chrono";
        case StreamKind::Stdout:  return "stdout";
        case StreamKind::Stderr:  return "stderr";
        case StreamKind::Runner:  return "runner";
        default:                  return "unknown";
    }
}

[[nodiscard]] constexpr bool from_string(std::string_view name, StreamKind& out) noexcept {
    if (name == "app") {
        out = StreamKind::App;
        return true;
    }
    if (name == "metrics") {
        out = StreamKind::Metrics;
        return true;
    }
    if (name == "traces") {
        out = StreamKind::Traces;
        return true;
    }
    if (name == "chrono") {
        out = StreamKind::Chrono;
        return true;
    }
    if (name == "stdout") {
        out = StreamKind::Stdout;
        return true;
    }
    if (name == "stderr") {
        out = StreamKind::Stderr;
        return true;
    }
    if (name == "runner") {
        out = StreamKind::Runner;
        return true;
    }
    return false;
}

} // namespace nexigen::hub
