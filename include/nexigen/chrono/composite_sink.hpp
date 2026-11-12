// nexigen::chrono — variadic, virtual-free fan-out sink (change
// add-chrono-clock-service; spec chrono/chrono-sink "Composite chrono sink
// fan-out").
//
// CompositeSink<Sinks...> stores its members in a std::tuple and forwards
// every snapshot through C++23 fold expressions: no virtual table — each
// member call is statically resolved and inlined.
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
#include <nexigen/chrono/concepts/sink_concept.hpp>
#include <nexigen/core/result.hpp>
#include <nexigen/core/sink_pack.hpp>

namespace nexigen::chrono {

/// @brief Fan-out over heterogeneous conforming sinks, with zero virtual dispatch.
///
/// Owns its members inline in a `std::tuple` and forwards every snapshot
/// through a C++23 comma fold, so each member call is statically resolved and
/// inlinable. Fan-out never short-circuits: a failing member does not starve
/// the others, and the first failure is what the composite returns. The
/// `find_index`/`exactly_one`/`get<Index>()`/`get<T>()`/`for_each`/`size`
/// scaffolding comes from `core::sink_pack::Fanout`; typed access requires the
/// type to appear exactly once and rejects absent types or duplicate types at
/// compile time.
///
/// Lifetime: the composite owns its members; moving and copying are deleted so
/// member addresses stay stable for the lifetime of the composite.
///
/// @tparam Sinks Member sink types; each must satisfy `concepts::Sink`
///         (`on_snapshot(const Snapshot&)` and `flush()` returning
///         `core::Status`).
/// @note Spec `chrono/chrono-sink`, requirement "Composite chrono sink fan-out".
template <concepts::Sink... Sinks> class CompositeSink : public core::sink_pack::Fanout<Sinks...> {
public:
    /// @brief Inherit the pack's constructor surface.
    ///
    /// Default construction exists only for default-constructible member packs;
    /// otherwise the member-injection constructor moves configured, possibly
    /// non-default-constructible sinks into the tuple.
    using core::sink_pack::Fanout<Sinks...>::Fanout;

    /// @brief Copying is deleted: members are owned in place.
    CompositeSink(const CompositeSink&) = delete;
    /// @brief Copy assignment is deleted: members are owned in place.
    CompositeSink& operator=(const CompositeSink&) = delete;
    /// @brief Moving is deleted: member addresses stay stable.
    CompositeSink(CompositeSink&&) = delete;
    /// @brief Move assignment is deleted: member addresses stay stable.
    CompositeSink& operator=(CompositeSink&&) = delete;

    /// @brief Deliver the snapshot to every member in pack order.
    ///
    /// Every member is invoked exactly once even when an earlier one fails; the
    /// first failure is preserved and later successes never mask it.
    ///
    /// @param snapshot Snapshot forwarded unchanged to each member.
    /// @return Success when every member succeeded, otherwise the first
    ///         member's failure status.
    /// @note Spec `chrono/chrono-sink`, requirement "Composite chrono sink
    ///       fan-out"; fan-out never short-circuits.
    [[nodiscard]] core::Status on_snapshot(const Snapshot& snapshot) noexcept {
        core::Status result{};
        this->for_each([&snapshot, &result](auto& sink) noexcept {
            result = core::sink_pack::combine(result, sink.on_snapshot(snapshot));
        });
        return result;
    }

    /// @brief Flush every member in pack order.
    ///
    /// Every member is flushed even when an earlier one fails.
    ///
    /// @return Success when every member flushed, otherwise the first failure
    ///         status.
    /// @note Spec `chrono/chrono-sink`, requirement "Composite chrono sink
    ///       fan-out".
    [[nodiscard]] core::Status flush() noexcept {
        core::Status result{};
        this->for_each([&result](auto& sink) noexcept {
            result = core::sink_pack::combine(result, sink.flush());
        });
        return result;
    }

    /// @brief Render the composite and each member's diagnostic text.
    ///
    /// Allocates a `std::string`; diagnostics only.
    ///
    /// @return Text of the form `CompositeSink(members=N, [..], ...)`.
    /// @note Spec `chrono/chrono-sink`, requirement "Composite chrono sink
    ///       fan-out"; project mandate 9.
    [[nodiscard]] std::string to_string() const {
        std::string out = "CompositeSink(members=" + std::to_string(sizeof...(Sinks));
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

/// @brief Free-helper mirror of `CompositeSink::to_string`.
///
/// @tparam Sinks Member sink types, each satisfying `concepts::Sink`.
/// @param sink Composite to render.
/// @return The same text as `sink.to_string()`.
/// @note Spec `chrono/chrono-sink`, requirement "Composite chrono sink
///       fan-out"; project mandate 9.
template <concepts::Sink... Sinks>
[[nodiscard]] std::string to_string(const CompositeSink<Sinks...>& sink) {
    return sink.to_string();
}

/// @brief Serialize the composite as diagnostic JSON (cold path).
///
/// Reports the type tag and member count only. The variadic composite cannot
/// be reconstructed from JSON, so deserialization is deliberately absent.
///
/// @tparam Sinks Member sink types, each satisfying `concepts::Sink`.
/// @param j Destination JSON value, replaced by the description.
/// @param sink Composite being described; not otherwise inspected.
/// @note Spec `chrono/chrono-sink`, requirement "Composite chrono sink
///       fan-out"; project mandate 10.
template <concepts::Sink... Sinks>
void to_json(nlohmann::json& j, const CompositeSink<Sinks...>& sink) {
    j = nlohmann::json{{"type", "CompositeSink"}, {"members", CompositeSink<Sinks...>::size}};
    static_cast<void>(sink);
}

static_assert(concepts::Sink<CompositeSink<>>, "empty composite must satisfy the sink contract");

} // namespace nexigen::chrono

/// @brief `std::formatter` specialization rendering the composite's diagnostic text.
///
/// @tparam Sinks Member sink types, each satisfying `concepts::Sink`.
/// @note Spec `chrono/chrono-sink`, requirement "Composite chrono sink fan-out";
///       project mandate 11.
template <nexigen::chrono::concepts::Sink... Sinks>
struct std::formatter<nexigen::chrono::CompositeSink<Sinks...>> : std::formatter<std::string_view> {
    /// @brief Format a composite as its diagnostic string.
    ///
    /// @tparam Ctx Format context type; the inherited string specifiers apply.
    /// @param sink Composite to format.
    /// @param ctx Format context receiving the output.
    /// @return Iterator past the last written character.
    /// @note Project mandate 11 (`std::formatter`).
    template <class Ctx>
    auto format(const nexigen::chrono::CompositeSink<Sinks...>& sink, Ctx& ctx) const {
        const std::string text = sink.to_string();
        return std::formatter<std::string_view>::format(text, ctx);
    }
};
