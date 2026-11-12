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

/// @brief A sink that accepts and discards every snapshot.
///
/// The cheapest conforming sink: no output, no allocation, no system call, and
/// no access to the snapshot fields. It is stateless, so all instances are
/// interchangeable and it is safe to bind wherever a sink is optional.
///
/// @note Spec `chrono/chrono-sink`, requirement "Null chrono sink".
class NullSink {
public:
    /// @brief Accept and ignore one snapshot.
    ///
    /// @return Always success.
    /// @note Spec `chrono/chrono-sink`, requirement "Null chrono sink".
    [[nodiscard]] core::Status on_snapshot(const Snapshot&) noexcept { return {}; }

    /// @brief Flush the sink.
    ///
    /// @return Always success; there is nothing buffered.
    /// @note Spec `chrono/chrono-sink`, requirement "Null chrono sink".
    [[nodiscard]] core::Status flush() noexcept { return {}; }

    /// @brief Render the sink's diagnostic identity.
    ///
    /// @return The constant text `NullSink()`.
    /// @note Spec `chrono/chrono-sink`, requirement "Null chrono sink"; project
    ///       mandate 9.
    [[nodiscard]] std::string to_string() const { return "NullSink()"; }
};

/// @brief Free-helper mirror of `NullSink::to_string`.
///
/// @return The constant text `NullSink()`.
/// @note Spec `chrono/chrono-sink`, requirement "Null chrono sink"; project
///       mandate 9.
[[nodiscard]] inline std::string to_string(const NullSink&) {
    return "NullSink()";
}

/// @brief Serialize the stateless sink as diagnostic JSON (cold path).
///
/// @param j Destination JSON value, replaced by `{"type": "NullSink"}`.
/// @note Spec `chrono/chrono-sink`, requirement "Null chrono sink"; project
///       mandate 10.
inline void to_json(nlohmann::json& j, const NullSink&) {
    j = nlohmann::json{{"type", "NullSink"}};
}

/// @brief Restore the stateless sink from JSON, strictly non-throwing.
///
/// The sink has no visible state, so any input is accepted and ignored.
///
/// @note Spec `chrono/chrono-sink`, requirement "Null chrono sink"; project
///       mandate 10.
inline void from_json(const nlohmann::json&, NullSink&) noexcept {}

static_assert(concepts::Sink<NullSink>, "NullSink must satisfy the sink contract");

} // namespace nexigen::chrono

/// @brief `std::formatter` specialization rendering the null-sink diagnostic text.
///
/// @note Spec `chrono/chrono-sink`, requirement "Null chrono sink"; project
///       mandate 11.
template <> struct std::formatter<nexigen::chrono::NullSink> : std::formatter<std::string_view> {
    /// @brief Format the sink as `NullSink()`.
    ///
    /// @tparam Ctx Format context type; the inherited string specifiers apply.
    /// @param ctx Format context receiving the output.
    /// @return Iterator past the last written character.
    /// @note Project mandate 11 (`std::formatter`).
    template <class Ctx> auto format(const nexigen::chrono::NullSink&, Ctx& ctx) const {
        return std::formatter<std::string_view>::format(std::string_view("NullSink()"), ctx);
    }
};
