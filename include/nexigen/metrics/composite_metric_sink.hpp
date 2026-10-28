// nexigen::metrics — variadic, virtual-free fan-out sink (design D13;
// spec metrics/metric-sinks "Composite metric sink fan-out").
//
// CompositeMetricSink<Sinks...> stores its members in a std::tuple and
// forwards every batch through C++23 fold expressions: no base class, no
// virtual table — each member call is statically resolved and inlined.
//
// Fan-out never short-circuits: a failing member does not starve the others,
// and the FIRST failure is what the composite returns. flush() flushes every
// member and reports the first failure.
//
// Member access: get<Index>() / get<Type>() return references. The type form
// requires the type to appear exactly once; out-of-range indices and
// absent/duplicated types fail at compile time via static_assert.
#pragma once

#include <array>
#include <cstddef>
#include <string>
#include <tuple>
#include <type_traits>
#include <utility>

#include <nexigen/core/result.hpp>
#include <nexigen/metrics/concepts/metric_sink_concept.hpp>
#include <nexigen/metrics/metric_snapshot.hpp>

namespace nexigen::metrics {

/// Fan-out over heterogeneous conforming sinks, zero virtual dispatch.
template <concepts::MetricSink... Sinks>
class CompositeMetricSink {
    template <class T>
    [[nodiscard]] static constexpr std::size_t find_index() noexcept {
        const std::array<bool, sizeof...(Sinks)> matches{std::is_same_v<T, Sinks>...};
        std::size_t found = sizeof...(Sinks); // sentinel: "not present"
        for (std::size_t i = 0; i < sizeof...(Sinks); ++i) {
            if (matches[i] && found == sizeof...(Sinks)) {
                found = i;
            }
        }
        return found;
    }

    template <class T>
    [[nodiscard]] static constexpr bool exactly_one() noexcept {
        const std::array<bool, sizeof...(Sinks)> matches{std::is_same_v<T, Sinks>...};
        std::size_t hits = 0;
        for (std::size_t i = 0; i < sizeof...(Sinks); ++i) {
            if (matches[i]) {
                ++hits;
            }
        }
        return hits == 1U;
    }

    /// Keep the first failure; a later success never masks it.
    [[nodiscard]] static core::Status combine(core::Status accumulated,
                                               core::Status next) noexcept {
        if (!accumulated.has_value()) {
            return accumulated;
        }
        return next;
    }

    template <typename Fn>
    void for_each(Fn&& fn) noexcept {
        std::apply([&fn](Sinks&... sink) {
            static_cast<void>(fn);
            (fn(sink), ...);
        }, members_);
    }

    template <typename Fn>
    void for_each(Fn&& fn) const noexcept {
        std::apply([&fn](const Sinks&... sink) {
            static_cast<void>(fn);
            (fn(sink), ...);
        }, members_);
    }

    std::tuple<Sinks...> members_{};

public:
    static constexpr std::size_t size = sizeof...(Sinks);

    /// Default construction — only for default-constructible member packs
    /// (the tuple constraint keeps this honest).
    CompositeMetricSink() noexcept
        requires (std::is_default_constructible_v<Sinks> && ...)
        : members_() {}

    /// Member-injection construction (design D3): configured, possibly
    /// non-default-constructible sinks are moved into the tuple — this is
    /// what makes CompositeMetricSink<ConsoleMetricSink, JsonFileMetricSink,
    /// NullMetricSink> constructible around a path-configured file sink.
    explicit CompositeMetricSink(Sinks... members)
        requires (sizeof...(Sinks) > 0)
        : members_(std::move(members)...) {}

    CompositeMetricSink(const CompositeMetricSink&) = delete;
    CompositeMetricSink& operator=(const CompositeMetricSink&) = delete;
    CompositeMetricSink(CompositeMetricSink&&) = delete;
    CompositeMetricSink& operator=(CompositeMetricSink&&) = delete;

    /// Sub-sink access by index.
    template <std::size_t Index>
    [[nodiscard]] decltype(auto) get() noexcept {
        static_assert(Index < sizeof...(Sinks), "index beyond the member pack");
        return std::get<Index>(members_);
    }
    template <std::size_t Index>
    [[nodiscard]] decltype(auto) get() const noexcept {
        static_assert(Index < sizeof...(Sinks), "index beyond the member pack");
        return std::get<Index>(members_);
    }

    /// Sub-sink access by type (must be unique in the pack).
    template <class T>
    [[nodiscard]] T& get() noexcept {
        static_assert(exactly_one<T>(),
                      "type absent or duplicated — get<Index>() for duplicates");
        return std::get<find_index<T>()>(members_);
    }
    template <class T>
    [[nodiscard]] const T& get() const noexcept {
        static_assert(exactly_one<T>(),
                      "type absent or duplicated — get<Index>() for duplicates");
        return std::get<find_index<T>()>(members_);
    }

    /// Deliver the batch to every member; the first failure is returned.
    [[nodiscard]] core::Status on_batch(const MetricBatchSnapshot& batch) noexcept {
        core::Status result{};
        for_each([&batch, &result](auto& sink) noexcept {
            result = combine(result, sink.on_batch(batch));
        });
        return result;
    }

    /// Flush every member; the first failure is returned.
    [[nodiscard]] core::Status flush() noexcept {
        core::Status result{};
        for_each([&result](auto& sink) noexcept { result = combine(result, sink.flush()); });
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
            members_);
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
