// nexigen::metrics — variadic, virtual-free fan-out sink (design D13;
// spec metrics/metric-sinks "Composite metric sink fan-out").
//
// CompositeMetricSink<Sinks...> stores its members in a std::tuple and
// forwards every batch through C++23 fold expressions: no virtual table —
// each member call is statically resolved and inlined.
//
// (dedup-sink-plumbing) The once-per-module copy scaffolding (find_index,
// exactly_one, get<Index>/get<T>, for_each, size, constructors, combine)
// now comes from nexigen::core::sink_pack::Fanout — the ONE implementation
// shared by the chrono/metrics/tracing composites; this module keeps the
// delivery surface, the module concept gate and the diagnostic formats.
//
// Fan-out never short-circuits: a failing member does not starve the others,
// and the FIRST failure is what the composite returns. flush() flushes every
// member and reports the first failure.
//
// Member access: get<Index>() / get<Type>() return references. The type form
// requires the type to appear exactly once; out-of-range indices and
// absent/duplicated types fail at compile time via static_assert.
#pragma once

#include <cstddef>
#include <string>
#include <tuple>
#include <utility>

#include <nexigen/core/result.hpp>
#include <nexigen/core/sink_pack.hpp>
#include <nexigen/metrics/concepts/metric_sink_concept.hpp>
#include <nexigen/metrics/metric_snapshot.hpp>

namespace nexigen::metrics {

/// @brief Variadic fan-out over heterogeneous conforming sinks with no virtual dispatch.
///
/// Stores members in a base tuple (`core::sink_pack::Fanout<Sinks...>`) and
/// forwards each batch through fold expressions, so every member call resolves
/// statically. Fan-out never short-circuits: a failing member does not starve
/// the rest, and the first failure is the one returned. `get<Index>()` and
/// `get<Type>()` come from the pack base and return references.
///
/// @tparam Sinks Member sink types; each must satisfy `concepts::MetricSink`
///         (the concept is enforced by the template parameter list, so a
///         non-conforming member is rejected at compile time).
/// @note Spec `metrics/metric-sinks`, requirement "Composite metric sink
///       fan-out".
template <concepts::MetricSink... Sinks>
class CompositeMetricSink : public core::sink_pack::Fanout<Sinks...> {
public:
    /// @brief Inherit the pack's construction surface.
    ///
    /// Default construction exists only for default-constructible member packs
    /// (design D3); the injecting constructors move configured, possibly
    /// non-default-constructible sinks, e.g.
    /// `CompositeMetricSink<ConsoleMetricSink, JsonFileMetricSink,
    /// NullMetricSink>` around a path-configured file sink.
    using core::sink_pack::Fanout<Sinks...>::Fanout;

    /// @brief Copy construction is deleted: members are owned in place.
    CompositeMetricSink(const CompositeMetricSink&) = delete;
    /// @brief Copy assignment is deleted: members are owned in place.
    CompositeMetricSink& operator=(const CompositeMetricSink&) = delete;
    /// @brief Move construction is deleted: members are owned in place.
    CompositeMetricSink(CompositeMetricSink&&) = delete;
    /// @brief Move assignment is deleted: members are owned in place.
    CompositeMetricSink& operator=(CompositeMetricSink&&) = delete;

    /// @brief Deliver one batch to every member and return the first failure.
    ///
    /// @param batch Snapshot forwarded unchanged to each member, once.
    /// @return Success when every member succeeded, otherwise the first
    ///         non-success status in member order.
    /// @note Spec `metrics/metric-sinks`, requirement "Composite metric sink
    ///       fan-out"; the delivery path stays allocation-free.
    [[nodiscard]] core::Status on_batch(const MetricBatchSnapshot& batch) noexcept {
        core::Status result{};
        this->for_each([&batch, &result](auto& sink) noexcept {
            result = core::sink_pack::combine(result, sink.on_batch(batch));
        });
        return result;
    }

    /// @brief Flush every member and return the first failure.
    ///
    /// @return Success when every member flushed, otherwise the first
    ///         non-success status in member order.
    /// @note Spec `metrics/metric-sinks`, requirement "Composite metric sink
    ///       fan-out".
    [[nodiscard]] core::Status flush() noexcept {
        core::Status result{};
        this->for_each([&result](auto& sink) noexcept {
            result = core::sink_pack::combine(result, sink.flush());
        });
        return result;
    }

    /// @brief Render the member count and each member's own description.
    ///
    /// @return `"CompositeMetricSink(members=<n>, [<member>], ...)"`; allocates,
    ///         cold path only.
    /// @note Spec `metrics/metric-sinks`, scenario "Composite diagnostics
    ///       contract"; project mandate 9 (`to_string`).
    [[nodiscard]] std::string to_string() const {
        std::string out = "CompositeMetricSink(members=" + std::to_string(sizeof...(Sinks));
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

/// @brief Free `to_string` helper mirroring the member function.
///
/// @tparam Sinks Member sink types of the composite.
/// @param sink Composite to describe.
/// @return The same text as `CompositeMetricSink::to_string`.
/// @note Spec `metrics/metric-sinks`, scenario "Composite diagnostics
///       contract"; project mandate 9.
template <concepts::MetricSink... Sinks>
[[nodiscard]] std::string to_string(const CompositeMetricSink<Sinks...>& sink) {
    return sink.to_string();
}

/// @brief Serialize the composite as its type tag plus member count.
///
/// The variadic composite cannot be reconstructed from JSON: there is
/// deliberately no `from_json`, matching its factory-only nature.
///
/// @tparam Sinks Member sink types of the composite.
/// @param j Destination JSON value.
/// @param sink Composite to describe; only its arity is written.
/// @note Spec `metrics/metric-sinks`, scenario "Composite diagnostics
///       contract"; project mandate 10 (JSON).
template <concepts::MetricSink... Sinks>
void to_json(nlohmann::json& j, const CompositeMetricSink<Sinks...>& sink) {
    j = nlohmann::json{{"type", "CompositeMetricSink"},
                       {"members", CompositeMetricSink<Sinks...>::size}};
    static_cast<void>(sink);
}

static_assert(concepts::MetricSink<CompositeMetricSink<>>,
              "empty composite must satisfy the sink contract");

} // namespace nexigen::metrics

/// @brief `std::formatter` for every composite instantiation.
///
/// @tparam Sinks Member sink types of the composite, each satisfying
///         `concepts::MetricSink`.
/// @note Spec `metrics/metric-sinks`, scenario "Composite diagnostics
///       contract"; project mandate 11 (`std::formatter`).
template <nexigen::metrics::concepts::MetricSink... Sinks>
struct std::formatter<nexigen::metrics::CompositeMetricSink<Sinks...>>
    : std::formatter<std::string_view> {
    /// @brief Write the composite's `to_string` text through the string formatter.
    ///
    /// @tparam Ctx Formatting context type supplied by `std::format`.
    /// @param sink Composite being formatted.
    /// @param ctx Destination formatting context.
    /// @return Iterator past the last written character.
    template <class Ctx>
    auto format(const nexigen::metrics::CompositeMetricSink<Sinks...>& sink, Ctx& ctx) const {
        const std::string text = sink.to_string();
        return std::formatter<std::string_view>::format(text, ctx);
    }
};
