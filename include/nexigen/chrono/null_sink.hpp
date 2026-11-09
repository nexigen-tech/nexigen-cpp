// nexigen::chrono — null sink (change add-chrono-clock-service; spec
// chrono/chrono-sink "Null chrono sink").
//
// The cheapest conforming sink: it accepts every snapshot and does nothing.
// No output, no allocation, no system call.
#pragma once

#include <string>

#include <nexigen/chrono/snapshot.hpp>
#include <nexigen/chrono/concepts/sink_concept.hpp>
#include <nexigen/core/result.hpp>

namespace nexigen::chrono {

/// A sink that discards every snapshot.
class NullSink {
public:
    [[nodiscard]] core::Status on_snapshot(const Snapshot&) noexcept { return {}; }

    [[nodiscard]] core::Status flush() noexcept { return {}; }

    /// Cold-path diagnostic (mandate 9).
    [[nodiscard]] std::string to_string() const { return "NullSink()"; }
};

/// Free helper (mandate 9): mirrors the member.
[[nodiscard]] inline std::string to_string(const NullSink&) {
    return "NullSink()";
}

/// Diagnostic JSON (cold path): the sink is stateless, so it round-trips.
inline void to_json(nlohmann::json& j, const NullSink&) {
    j = nlohmann::json{{"type", "NullSink"}};
}

inline void from_json(const nlohmann::json&, NullSink&) noexcept {
    // Stateless: nothing to restore.
}

static_assert(concepts::Sink<NullSink>, "NullSink must satisfy the sink contract");

} // namespace nexigen::chrono

/// The null sink formats as its diagnostic string (mandate 11).
template <>
struct std::formatter<nexigen::chrono::NullSink> : std::formatter<std::string_view> {
    template <class Ctx> auto format(const nexigen::chrono::NullSink&, Ctx& ctx) const {
        return std::formatter<std::string_view>::format(std::string_view("NullSink()"), ctx);
    }
};
