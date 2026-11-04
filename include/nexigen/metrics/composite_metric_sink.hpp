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

/// Fan-out over heterogeneous conforming sinks, zero virtual dispatch.
template <concepts::MetricSink... Sinks>
class CompositeMetricSink : public core::sink_pack::Fanout<Sinks...> {
public:
    /// Constructor surface inherited from the pack: default construction
    /// only for default-constructible member packs (design D3), member
    /// injection (moving configured, possibly non-default-constructible
    /// sinks, e.g. CompositeMetricSink<ConsoleMetricSink, JsonFileMetricSink,
    /// NullMetricSink> around a path-configured file sink) otherwise.
    using core::sink_pack::Fanout<Sinks...>::Fanout;

    CompositeMetricSink(const CompositeMetricSink&) = delete;
    CompositeMetricSink& operator=(const CompositeMetricSink&) = delete;
    CompositeMetricSink(CompositeMetricSink&&) = delete;
    CompositeMetricSink& operator=(CompositeMetricSink&&) = delete;

    /// Deliver the batch to every member; the first failure is returned.
    [[nodiscard]] core::Status on_batch(const MetricBatchSnapshot& batch) noexcept {
        core::Status result{};
        this->for_each([&batch, &result](auto& sink) noexcept {
            result = core::sink_pack::combine(result, sink.on_batch(batch));
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

    /// Cold-path diagnostic (mandate 9): the member count and each member's
    /// own description.
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

/// Free helper (mandate 9): mirrors the member.
template <concepts::MetricSink... Sinks>
[[nodiscard]] std::string to_string(const CompositeMetricSink<Sinks...>& sink) {
    return sink.to_string();
}

/// Diagnostic JSON (cold path). The variadic composite cannot be
/// reconstructed from JSON — deserialization is deliberately absent.
template <concepts::MetricSink... Sinks>
void to_json(nlohmann::json& j, const CompositeMetricSink<Sinks...>& sink) {
    j = nlohmann::json{{"type", "CompositeMetricSink"}, {"members", CompositeMetricSink<Sinks...>::size}};
    static_cast<void>(sink);
}

static_assert(concepts::MetricSink<CompositeMetricSink<>>, "empty composite must satisfy the sink contract");

} // namespace nexigen::metrics

/// Composite sinks format as their diagnostic string (mandate 11).
template <nexigen::metrics::concepts::MetricSink... Sinks>
struct std::formatter<nexigen::metrics::CompositeMetricSink<Sinks...>>
    : std::formatter<std::string_view> {
    template <class Ctx>
    auto format(const nexigen::metrics::CompositeMetricSink<Sinks...>& sink, Ctx& ctx) const {
        const std::string text = sink.to_string();
        return std::formatter<std::string_view>::format(text, ctx);
    }
};
