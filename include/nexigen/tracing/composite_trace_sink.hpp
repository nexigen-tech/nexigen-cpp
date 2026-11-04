// nexigen::tracing — variadic, virtual-free sink fan-out (design D11;
// spec tracing/trace-sinks "CompositeTraceSink fan-out"; metrics D13 pattern
// verbatim).
//
// CompositeTraceSink<Sinks...> stores its members in a std::tuple and
// forwards every span through fold expressions: no virtual table — each
// member call is statically resolved and inlined.
//
// (dedup-sink-plumbing) The once-per-module copy scaffolding (find_index,
// exactly_one, get<Index>/get<T>, for_each, size, constructors, combine)
// now comes from nexigen::core::sink_pack::Fanout — the ONE implementation
// shared by the chrono/metrics/tracing composites; this module keeps the
// delivery surface, the module concept gate and the diagnostic formats.
//
// Fan-out never short-circuits: a failing member does not starve the others,
// and the FIRST failure is what the composite returns after ALL members
// were attempted. flush() flushes every member and reports the first
// failure; start()/stop() visit every member the same way.
//
// Member access: get<Index>() / get<SinkType>() return references. The type
// form requires the type to appear exactly once; out-of-range indices and
// absent/duplicated types fail at compile time via static_assert.
#pragma once

#include <cstddef>
#include <format>
#include <string>
#include <tuple>
#include <utility>

#include <nlohmann/json.hpp>

#include <nexigen/core/result.hpp>
#include <nexigen/core/sink_pack.hpp>
#include <nexigen/tracing/concepts/tracer_concept.hpp>
#include <nexigen/tracing/span_record.hpp>

namespace nexigen::tracing {

/// Fan-out over heterogeneous conforming sinks, zero virtual dispatch.
template <concepts::TraceSink... Sinks>
class CompositeTraceSink : public core::sink_pack::Fanout<Sinks...> {
public:
    /// Constructor surface inherited from the pack: default construction
    /// only for default-constructible member packs; member injection
    /// (moving configured, possibly non-default-constructible sinks, e.g.
    /// CompositeTraceSink<ConsoleTraceSink, JsonFileTraceSink, NullTraceSink>
    /// around a path-configured file sink) otherwise.
    using core::sink_pack::Fanout<Sinks...>::Fanout;

    CompositeTraceSink(const CompositeTraceSink&) = delete;
    CompositeTraceSink& operator=(const CompositeTraceSink&) = delete;
    CompositeTraceSink(CompositeTraceSink&&) = delete;
    CompositeTraceSink& operator=(CompositeTraceSink&&) = delete;

    /// Start every member; the first failure is returned after all were
    /// attempted.
    [[nodiscard]] core::Status start() noexcept {
        core::Status result{};
        this->for_each([&result](auto& sink) noexcept {
            result = core::sink_pack::combine(result, sink.start());
        });
        return result;
    }

    /// Deliver the span to every member; the first failure is returned
    /// after all were attempted (no starvation).
    [[nodiscard]] core::Status on_span(const SpanRecord& record) noexcept {
        core::Status result{};
        this->for_each([&record, &result](auto& sink) noexcept {
            result = core::sink_pack::combine(result, sink.on_span(record));
        });
        return result;
    }

    /// Flush every member; the first failure is returned.
    [[nodiscard]] core::Status flush() noexcept {
        core::Status result{};
        this->for_each([&result](auto& sink) noexcept {
            result = core::sink_pack::combine(result, sink.flush());
        });
        return result;
    }

    /// Stop every member.
    void stop() noexcept {
        this->for_each([](auto& sink) noexcept { sink.stop(); });
    }

    /// Cold-path diagnostic (mandate 9): the member count and each member's
    /// own description.
    [[nodiscard]] std::string to_string() const {
        std::string out = "CompositeTraceSink(members=" + std::to_string(sizeof...(Sinks));
        std::apply(
            [&](const Sinks&... sink) {
                static_cast<void>(out);
                ((out += ", [" + sink.to_string() + "]"), ...);
            },
            this->members_);
        out += ")";
        return out;
    }
};

/// Free helper (mandate 9): mirrors the member.
template <concepts::TraceSink... Sinks>
[[nodiscard]] std::string to_string(const CompositeTraceSink<Sinks...>& sink) {
    return sink.to_string();
}

/// Diagnostic JSON (cold path). The variadic composite cannot be
/// reconstructed from JSON — deserialization is deliberately absent (spec
/// "Live sinks render diagnostics and refuse deserialization").
template <concepts::TraceSink... Sinks>
void to_json(nlohmann::json& j, const CompositeTraceSink<Sinks...>& sink) {
    j = nlohmann::json{{"type", "CompositeTraceSink"},
                       {"members", CompositeTraceSink<Sinks...>::size}};
    static_cast<void>(sink);
}

static_assert(concepts::TraceSink<CompositeTraceSink<>>,
              "empty composite must satisfy the sink contract");

} // namespace nexigen::tracing

/// Composite sinks format as their diagnostic string (mandate 11).
template <nexigen::tracing::concepts::TraceSink... Sinks>
struct std::formatter<nexigen::tracing::CompositeTraceSink<Sinks...>>
    : std::formatter<std::string_view> {
    template <class Ctx>
    auto format(const nexigen::tracing::CompositeTraceSink<Sinks...>& sink, Ctx& ctx) const {
        const std::string text = sink.to_string();
        return std::formatter<std::string_view>::format(text, ctx);
    }
};
