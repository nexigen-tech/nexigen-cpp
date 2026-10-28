// nexigen::tracing — null trace sink (design D2/D11; spec
// tracing/tracer-concepts "NullTraceSink zero-work sink").
//
// The cheapest conforming sink: it accepts every span and does nothing. No
// output, no allocation, no system call — on_span discards the record
// without reading it. start/flush succeed as no-ops; stop does nothing.
#pragma once

#include <format>
#include <string>

#include <nlohmann/json.hpp>

#include <nexigen/core/result.hpp>
#include <nexigen/tracing/concepts/tracer_concept.hpp>
#include <nexigen/tracing/span_record.hpp>

namespace nexigen::tracing {

/// A sink that discards every span.
class NullTraceSink {
public:
    [[nodiscard]] core::Status start() noexcept { return {}; }

    /// Accepts and discards the record without reading it.
    [[nodiscard]] core::Status on_span(const SpanRecord&) noexcept { return {}; }

    [[nodiscard]] core::Status flush() noexcept { return {}; }

    void stop() noexcept {}

    /// Cold-path diagnostic (mandate 9).
    [[nodiscard]] std::string to_string() const { return "NullTraceSink()"; }
};

/// Free helper (mandate 9): mirrors the member.
[[nodiscard]] inline std::string to_string(const NullTraceSink&) {
    return "NullTraceSink()";
}

/// Diagnostic JSON (cold path); no from_json by design — the null sink is
/// not reconstructed from data (spec "Null types are not deserializable").
inline void to_json(nlohmann::json& j, const NullTraceSink&) {
    j = nlohmann::json{{"type", "NullTraceSink"}};
}

static_assert(concepts::TraceSink<NullTraceSink>,
              "NullTraceSink must satisfy the sink contract");

} // namespace nexigen::tracing

/// The null sink formats as its diagnostic string (mandate 11).
template <>
struct std::formatter<nexigen::tracing::NullTraceSink> : std::formatter<std::string_view> {
    template <class Ctx>
    auto format(const nexigen::tracing::NullTraceSink& sink, Ctx& ctx) const {
        return std::formatter<std::string_view>::format(sink.to_string(), ctx);
    }
};
