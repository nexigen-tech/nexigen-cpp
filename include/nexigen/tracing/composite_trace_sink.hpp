// nexigen::tracing — variadic, virtual-free sink fan-out (design D11;
// spec tracing/trace-sinks "CompositeTraceSink fan-out"; metrics D13 pattern
// verbatim).
//
// CompositeTraceSink<Sinks...> stores its members in a std::tuple and
// forwards every span through fold expressions: no base class, no virtual
// table — each member call is statically resolved and inlined.
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

#include <array>
#include <cstddef>
#include <format>
#include <string>
#include <tuple>
#include <type_traits>
#include <utility>

#include <nlohmann/json.hpp>

#include <nexigen/core/result.hpp>
#include <nexigen/tracing/concepts/tracer_concept.hpp>
#include <nexigen/tracing/span_record.hpp>

namespace nexigen::tracing {

/// Fan-out over heterogeneous conforming sinks, zero virtual dispatch.
template <concepts::TraceSink... Sinks>
class CompositeTraceSink {
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

    /// Default construction — only for default-constructible member packs.
    CompositeTraceSink() noexcept
        requires (std::is_default_constructible_v<Sinks> && ...)
        : members_() {}

    /// Member-injection construction: configured, possibly
    /// non-default-constructible sinks are moved into the tuple — this is
    /// what makes CompositeTraceSink<ConsoleTraceSink, JsonFileTraceSink,
    /// NullTraceSink> constructible around a path-configured file sink.
    explicit CompositeTraceSink(Sinks... members)
        requires (sizeof...(Sinks) > 0)
        : members_(std::move(members)...) {}

    CompositeTraceSink(const CompositeTraceSink&) = delete;
    CompositeTraceSink& operator=(const CompositeTraceSink&) = delete;
    CompositeTraceSink(CompositeTraceSink&&) = delete;
    CompositeTraceSink& operator=(CompositeTraceSink&&) = delete;

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

    /// Start every member; the first failure is returned after all were
    /// attempted.
    [[nodiscard]] core::Status start() noexcept {
        core::Status result{};
        for_each([&result](auto& sink) noexcept { result = combine(result, sink.start()); });
        return result;
    }

    /// Deliver the span to every member; the first failure is returned
    /// after all were attempted (no starvation).
    [[nodiscard]] core::Status on_span(const SpanRecord& record) noexcept {
        core::Status result{};
        for_each([&record, &result](auto& sink) noexcept {
            result = combine(result, sink.on_span(record));
        });
        return result;
    }

    /// Flush every member; the first failure is returned.
    [[nodiscard]] core::Status flush() noexcept {
        core::Status result{};
        for_each([&result](auto& sink) noexcept { result = combine(result, sink.flush()); });
        return result;
    }

    /// Stop every member.
    void stop() noexcept {
        for_each([](auto& sink) noexcept { sink.stop(); });
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
            members_);
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
