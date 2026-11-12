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

/// @brief Variadic fan-out over heterogeneous conforming sinks, no virtual
///        dispatch.
///
/// Stores its members in `core::sink_pack::Fanout<Sinks...>` and forwards each
/// record through fold expressions, so every member call resolves statically
/// and inlines. Fan-out never short-circuits: a failing member does not starve
/// the rest, and the first failure is the one returned after all members were
/// attempted. `start()`, `flush()` and `stop()` visit every member the same
/// way. `get<Index>()` and `get<SinkType>()` come from the pack base and
/// return references.
///
/// @tparam Sinks Member sink types; each must satisfy `concepts::TraceSink`
///         (the constraint is enforced by the template parameter list, so a
///         non-conforming member is rejected at compile time).
/// @note Spec `tracing/trace-sinks`, requirement "CompositeTraceSink fan-out".
template <concepts::TraceSink... Sinks>
class CompositeTraceSink : public core::sink_pack::Fanout<Sinks...> {
public:
    /// @brief Inherit the pack's construction surface.
    ///
    /// Default construction exists only for default-constructible member
    /// packs; the injecting constructors move configured, possibly
    /// non-default-constructible sinks, e.g.
    /// `CompositeTraceSink<ConsoleTraceSink, JsonFileTraceSink,
    /// NullTraceSink>` around a path-configured file sink.
    using core::sink_pack::Fanout<Sinks...>::Fanout;

    /// @brief Copy construction is deleted: members are owned in place.
    CompositeTraceSink(const CompositeTraceSink&) = delete;
    /// @brief Copy assignment is deleted: members are owned in place.
    CompositeTraceSink& operator=(const CompositeTraceSink&) = delete;
    /// @brief Move construction is deleted: members are owned in place.
    CompositeTraceSink(CompositeTraceSink&&) = delete;
    /// @brief Move assignment is deleted: members are owned in place.
    CompositeTraceSink& operator=(CompositeTraceSink&&) = delete;

    /// @brief Start every member and return the first failure.
    ///
    /// Every member is attempted even after an earlier one fails.
    ///
    /// @return Success when every member started, otherwise the first
    ///         non-success status in member order.
    /// @note Spec `tracing/trace-sinks`, requirement "CompositeTraceSink
    ///       fan-out".
    [[nodiscard]] core::Status start() noexcept {
        core::Status result{};
        this->for_each([&result](auto& sink) noexcept {
            result = core::sink_pack::combine(result, sink.start());
        });
        return result;
    }

    /// @brief Deliver the record to every member and return the first failure.
    ///
    /// No member is skipped when another fails (no starvation); the same
    /// immutable record is passed unchanged to each member once.
    ///
    /// @param record Span forwarded unchanged to every member.
    /// @return Success when every member accepted the record, otherwise the
    ///         first non-success status in member order.
    /// @note Spec `tracing/trace-sinks`, requirement "CompositeTraceSink
    ///       fan-out"; the delivery path stays allocation-free.
    [[nodiscard]] core::Status on_span(const SpanRecord& record) noexcept {
        core::Status result{};
        this->for_each([&record, &result](auto& sink) noexcept {
            result = core::sink_pack::combine(result, sink.on_span(record));
        });
        return result;
    }

    /// @brief Flush every member and return the first failure.
    ///
    /// @return Success when every member flushed, otherwise the first
    ///         non-success status in member order.
    /// @note Spec `tracing/trace-sinks`, requirement "CompositeTraceSink
    ///       fan-out".
    [[nodiscard]] core::Status flush() noexcept {
        core::Status result{};
        this->for_each([&result](auto& sink) noexcept {
            result = core::sink_pack::combine(result, sink.flush());
        });
        return result;
    }

    /// @brief Stop every member, regardless of individual outcomes.
    ///
    /// @note Spec `tracing/trace-sinks`, requirement "CompositeTraceSink
    ///       fan-out".
    void stop() noexcept {
        this->for_each([](auto& sink) noexcept { sink.stop(); });
    }

    /// @brief Render the member count and each member's own description.
    ///
    /// @return `"CompositeTraceSink(members=<n>, [<member>], ...)"`;
    ///         allocates, cold path only.
    /// @note Spec `tracing/trace-sinks`, requirement "CompositeTraceSink
    ///       fan-out"; project mandate 9 (`to_string`).
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

/// @brief Free diagnostic helper mirroring
///        `CompositeTraceSink::to_string()`.
///
/// @tparam Sinks Member sink types; each must satisfy `concepts::TraceSink`.
/// @param sink Composite to describe.
/// @return The same text the member returns.
/// @note Spec `tracing/trace-sinks`, requirement "CompositeTraceSink fan-out";
///       project mandate 9 (`to_string`).
template <concepts::TraceSink... Sinks>
[[nodiscard]] std::string to_string(const CompositeTraceSink<Sinks...>& sink) {
    return sink.to_string();
}

/// @brief Serialize the composite's member count as JSON (cold path).
///
/// The variadic composite cannot be reconstructed from JSON, so there is
/// deliberately no `from_json` (spec "Live sinks render diagnostics and refuse
/// deserialization").
///
/// @tparam Sinks Member sink types; each must satisfy `concepts::TraceSink`.
/// @param j Destination JSON value, overwritten with type and member count.
/// @param sink Composite to describe; consulted only for its type.
/// @note Spec `tracing/trace-sinks`, requirement "Live sink serialization
///       surfaces"; project mandate 10 (JSON).
template <concepts::TraceSink... Sinks>
void to_json(nlohmann::json& j, const CompositeTraceSink<Sinks...>& sink) {
    j = nlohmann::json{{"type", "CompositeTraceSink"},
                       {"members", CompositeTraceSink<Sinks...>::size}};
    static_cast<void>(sink);
}

static_assert(concepts::TraceSink<CompositeTraceSink<>>,
              "empty composite must satisfy the sink contract");

} // namespace nexigen::tracing

/// @brief `std::formatter` for `CompositeTraceSink` instantiations.
///
/// @tparam Sinks Member sink types; each must satisfy `concepts::TraceSink`.
/// @note Project mandate 11 (`std::formatter`); formatting is cold path only.
template <nexigen::tracing::concepts::TraceSink... Sinks>
struct std::formatter<nexigen::tracing::CompositeTraceSink<Sinks...>>
    : std::formatter<std::string_view> {
    /// @brief Format the composite through its `to_string()` text.
    ///
    /// @tparam Ctx Format context type supplied by `std::format`.
    /// @param sink Composite to render.
    /// @param ctx Output context receiving the text.
    /// @return Iterator past the last written character.
    template <class Ctx>
    auto format(const nexigen::tracing::CompositeTraceSink<Sinks...>& sink, Ctx& ctx) const {
        const std::string text = sink.to_string();
        return std::formatter<std::string_view>::format(text, ctx);
    }
};
