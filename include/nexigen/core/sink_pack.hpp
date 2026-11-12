// nexigen::core — variadic fan-out member pack shared by the observability
// composite sinks (change dedup-sink-plumbing; spec core/sink-plumbing
// "Variadic fan-out member pack").
//
// Consumed by nexigen::metrics / nexigen::tracing / nexigen::chrono composite
// sink headers: the find_index/exactly_one/get/get<Index>/for_each/size
// scaffolding used to live as three per-module copies that had started
// diverging in naming only. This header is the ONE implementation (the
// fixed_writer.hpp "one implementation instead of three near-identical
// copies" precedent; per-module concept gates and payload methods stay in
// the modules).
//
// DETAIL DISCLAIMER (mirrors settings_io.hpp and fixed_writer.hpp): the
// namespace keeps a dedicated name (`nexigen::core::sink_pack`) so test
// scopes that `using namespace nexigen::core` never learn a SECOND bare
// `detail` candidate name. The pack is compile-time only: no virtual table,
// no heap allocation, no runtime dispatch — member calls stay statically
// resolved per composite.
//
// Fold-order note: for_each's `(fn(sink), ...)` comma-fold visits members in
// pack order (index 0 first) — [expr.fold]: the comma pack-fold is sequenced
// left to right. Fan-out never short-circuits: a failing member does not
// starve the others; combine() keeps the FIRST failure.
#pragma once

#include <array>
#include <cstddef>
#include <tuple>
#include <type_traits>
#include <utility>

#include <nexigen/core/result.hpp>

namespace nexigen::core::sink_pack {

/// @brief Keep the first failure; a later success never masks it.
///
/// Free on purpose: composite bodies call it as `sink_pack::combine(...)` inside
/// folds.
///
/// @param accumulated Outcome accumulated so far.
/// @param next Outcome of the member just visited.
/// @return `accumulated` when it is already a failure, else `next`.
/// @note Spec `core/sink-plumbing`, requirement "Variadic fan-out member pack"
///       (first-failure combination).
[[nodiscard]] inline core::Status combine(core::Status accumulated, core::Status next) noexcept {
    if (!accumulated.has_value()) {
        return accumulated;
    }
    return next;
}

/// @brief Variadic, virtual-free member pack for composite fan-out sinks.
///
/// Tuple storage plus the compile-time lookup/iteration surface every composite
/// needs: index and type access, ordered non-short-circuiting iteration, and a
/// static pack size. Compile-time only: no virtual table, no heap allocation,
/// no runtime dispatch. Non-copyable and non-movable, matching the composites.
///
/// @tparam Sinks Member sink types, stored in pack order.
/// @note Spec `core/sink-plumbing`, requirement "Variadic fan-out member pack".
template <class... Sinks> class Fanout {
protected:
    /// @brief Compile-time index of the first member whose type is `T`.
    ///
    /// @tparam T Member type to locate.
    /// @return Zero-based index, or the sentinel `sizeof...(Sinks)` when `T` is
    ///         absent (pair with `exactly_one<T>()`).
    template <class T> [[nodiscard]] static constexpr std::size_t find_index() noexcept {
        const std::array<bool, sizeof...(Sinks)> matches{std::is_same_v<T, Sinks>...};
        std::size_t found = sizeof...(Sinks); // sentinel: "not present"
        for (std::size_t i = 0; i < sizeof...(Sinks); ++i) {
            if (matches[i] && found == sizeof...(Sinks)) {
                found = i;
            }
        }
        return found;
    }

    /// @brief Whether `T` appears exactly once in the pack.
    ///
    /// Compile-time gate behind type-based `get<T>()`: an absent or duplicated
    /// type fails it.
    ///
    /// @tparam T Member type to count.
    /// @return `true` when exactly one member has type `T`.
    template <class T> [[nodiscard]] static constexpr bool exactly_one() noexcept {
        const std::array<bool, sizeof...(Sinks)> matches{std::is_same_v<T, Sinks>...};
        std::size_t hits = 0;
        for (std::size_t i = 0; i < sizeof...(Sinks); ++i) {
            if (matches[i]) {
                ++hits;
            }
        }
        return hits == 1U;
    }

    /// @brief Invoke `fn` against every member in pack order (index 0 first).
    ///
    /// Never short-circuits: a failing member does not starve the others. This
    /// is the non-const delivery path.
    ///
    /// @tparam Fn Callable invocable as `fn(Sink&)` for every member type.
    /// @param fn Visitor applied once per member.
    template <typename Fn> void for_each(Fn&& fn) noexcept {
        std::apply(
            [&fn](Sinks&... sink) {
                static_cast<void>(fn);
                (fn(sink), ...);
            },
            members_);
    }

    /// @brief Invoke `fn` against every member in pack order (index 0 first).
    ///
    /// Never short-circuits. This is the const delivery path.
    ///
    /// @tparam Fn Callable invocable as `fn(const Sink&)` for every member type.
    /// @param fn Visitor applied once per member.
    template <typename Fn> void for_each(Fn&& fn) const noexcept {
        std::apply(
            [&fn](const Sinks&... sink) {
                static_cast<void>(fn);
                (fn(sink), ...);
            },
            members_);
    }

    /// @brief Heterogeneous member storage in pack order.
    ///
    /// The tuple the fold expressions and both access forms resolve against;
    /// populated by the constructor, never replaced afterwards.
    std::tuple<Sinks...> members_{};

public:
    /// @brief Static pack length, inherited by every composite.
    ///
    /// Composite `to_json` reports it verbatim and type access bounds itself
    /// with it.
    static constexpr std::size_t size = sizeof...(Sinks);

    /// @brief Default construction for fully default-constructible member
    ///        packs.
    ///
    /// The constraint keeps the default surface honest at the derived composite
    /// level.
    Fanout() noexcept
        requires(std::is_default_constructible_v<Sinks> && ...)
        : members_() {}

    /// @brief Member-injection construction.
    ///
    /// Moves configured, possibly non-default-constructible members into the
    /// tuple, which is what makes `CompositeX<ConsoleX, JsonFileX, NullX>`
    /// constructible around a path-configured file sink.
    ///
    /// @param members Members to move in, in pack order.
    explicit Fanout(Sinks... members)
        requires(sizeof...(Sinks) > 0)
        : members_(std::move(members)...) {}

    /// @brief Copy construction is disabled: members are held in place.
    Fanout(const Fanout&) = delete;
    /// @brief Copy assignment is disabled: members are held in place.
    Fanout& operator=(const Fanout&) = delete;
    /// @brief Move construction is disabled: the pack address is stable.
    Fanout(Fanout&&) = delete;
    /// @brief Move assignment is disabled: the pack address is stable.
    Fanout& operator=(Fanout&&) = delete;

    /// @brief Member access by index.
    ///
    /// @tparam Index Zero-based member index; out-of-range indices fail a
    ///         `static_assert`.
    /// @return Reference to the member at `Index`.
    template <std::size_t Index> [[nodiscard]] decltype(auto) get() noexcept {
        static_assert(Index < sizeof...(Sinks), "index beyond the member pack");
        return std::get<Index>(members_);
    }
    /// @brief Const member access by index.
    ///
    /// @tparam Index Zero-based member index; out-of-range indices fail a
    ///         `static_assert`.
    /// @return Const reference to the member at `Index`.
    template <std::size_t Index> [[nodiscard]] decltype(auto) get() const noexcept {
        static_assert(Index < sizeof...(Sinks), "index beyond the member pack");
        return std::get<Index>(members_);
    }

    /// @brief Member access by type; the type must be unique in the pack.
    ///
    /// @tparam T Member type; absence or duplication fails a `static_assert`
    ///         (use `get<Index>()` for duplicates).
    /// @return Reference to the single member of type `T`.
    template <class T> [[nodiscard]] T& get() noexcept {
        static_assert(exactly_one<T>(), "type absent or duplicated — get<Index>() for duplicates");
        return std::get<find_index<T>()>(members_);
    }
    /// @brief Const member access by type; the type must be unique.
    ///
    /// @tparam T Member type; absence or duplication fails a `static_assert`
    ///         (use `get<Index>()` for duplicates).
    /// @return Const reference to the single member of type `T`.
    template <class T> [[nodiscard]] const T& get() const noexcept {
        static_assert(exactly_one<T>(), "type absent or duplicated — get<Index>() for duplicates");
        return std::get<find_index<T>()>(members_);
    }
};

} // namespace nexigen::core::sink_pack
