// nexigen::chrono — variadic, virtual-free fan-out sink (change
// add-chrono-clock-service; spec chrono/chrono-sink "Composite chrono sink
// fan-out").
//
// CompositeChronoSink<Sinks...> stores its members in a std::tuple and
// forwards every snapshot through C++23 fold expressions: no virtual table —
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
#pragma once

#include <cstddef>
#include <string>
#include <tuple>
#include <utility>

#include <nexigen/chrono/snapshot.hpp>
#include <nexigen/chrono/concepts/chrono_sink_concept.hpp>
#include <nexigen/core/result.hpp>
#include <nexigen/core/sink_pack.hpp>

namespace nexigen::chrono {

/// Fan-out over heterogeneous conforming sinks, zero virtual dispatch.
template <concepts::ChronoSink... Sinks>
class CompositeChronoSink : public core::sink_pack::Fanout<Sinks...> {
public:
    /// Constructor surface inherited from the pack: default construction
    /// only for default-constructible member packs; member injection
    /// (moving configured, possibly non-default-constructible sinks)
    /// otherwise.
    using core::sink_pack::Fanout<Sinks...>::Fanout;

    CompositeChronoSink(const CompositeChronoSink&) = delete;
    CompositeChronoSink& operator=(const CompositeChronoSink&) = delete;
    CompositeChronoSink(CompositeChronoSink&&) = delete;
    CompositeChronoSink& operator=(CompositeChronoSink&&) = delete;

    /// Deliver the snapshot to every member; the first failure is returned.
    [[nodiscard]] core::Status on_snapshot(const Snapshot& snapshot) noexcept {
        core::Status result{};
        this->for_each([&snapshot, &result](auto& sink) noexcept {
            result = core::sink_pack::combine(result, sink.on_snapshot(snapshot));
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
        std::string out = "CompositeChronoSink(members=" + std::to_string(sizeof...(Sinks));
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
template <concepts::ChronoSink... Sinks>
[[nodiscard]] std::string to_string(const CompositeChronoSink<Sinks...>& sink) {
    return sink.to_string();
}

/// Diagnostic JSON (cold path). The variadic composite cannot be
/// reconstructed from JSON — deserialization is deliberately absent.
template <concepts::ChronoSink... Sinks>
void to_json(nlohmann::json& j, const CompositeChronoSink<Sinks...>& sink) {
    j = nlohmann::json{{"type", "CompositeChronoSink"},
                       {"members", CompositeChronoSink<Sinks...>::size}};
    static_cast<void>(sink);
}

static_assert(concepts::ChronoSink<CompositeChronoSink<>>,
              "empty composite must satisfy the sink contract");

} // namespace nexigen::chrono

/// Composite sinks format as their diagnostic string (mandate 11).
template <nexigen::chrono::concepts::ChronoSink... Sinks>
struct std::formatter<nexigen::chrono::CompositeChronoSink<Sinks...>>
    : std::formatter<std::string_view> {
    template <class Ctx>
    auto format(const nexigen::chrono::CompositeChronoSink<Sinks...>& sink, Ctx& ctx) const {
        const std::string text = sink.to_string();
        return std::formatter<std::string_view>::format(text, ctx);
    }
};
