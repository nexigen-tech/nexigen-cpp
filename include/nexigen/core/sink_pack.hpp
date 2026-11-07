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

/// Keep the first failure; a later success never masks it. Free on purpose:
/// composite bodies call it as `sink_pack::combine(...)` inside folds.
[[nodiscard]] inline core::Status combine(core::Status accumulated, core::Status next) noexcept {
    if (!accumulated.has_value()) {
        return accumulated;
    }
    return next;
}

/// Variadic, virtual-free member pack: tuple storage plus the compile-time
/// lookup/iteration surface every composite fan-out sink needs. Members and
/// protected helper methods are moved in verbatim from the per-module
/// composite headers, so the codegen and the compile-time contract (exact
/// static_assert texts included) are unchanged.
template <class... Sinks> class Fanout {
protected:
    /// Compile-time index of the first member whose type is T; the sentinel
    /// `sizeof...(Sinks)` marks an absent type (pair with exactly_one<T>()).
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

    /// True when type T appears exactly once in the pack — the compile-time
    /// gate behind type-based get<T>() (absent or duplicated types fail it).
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

    /// Invoke fn against every member in pack order (index 0 first), never
    /// short-circuiting — the non-const delivery path.
    template <typename Fn> void for_each(Fn&& fn) noexcept {
        std::apply(
            [&fn](Sinks&... sink) {
                static_cast<void>(fn);
                (fn(sink), ...);
            },
            members_);
    }

    /// Invoke fn against every member in pack order (index 0 first), never
    /// short-circuiting — the const delivery path.
    template <typename Fn> void for_each(Fn&& fn) const noexcept {
        std::apply(
            [&fn](const Sinks&... sink) {
                static_cast<void>(fn);
                (fn(sink), ...);
            },
            members_);
    }

    /// Heterogeneous member storage in pack order (the tuple the fold
    /// expressions and both access forms resolve against).
    std::tuple<Sinks...> members_{};

public:
    /// Static pack length; inherited by every composite (their to_json
    /// reports it verbatim and type access bounds itself with it).
    static constexpr std::size_t size = sizeof...(Sinks);

    /// Default construction — only for default-constructible member packs
    /// (the constraint keeps this honest at the derived composite surface).
    Fanout() noexcept
        requires(std::is_default_constructible_v<Sinks> && ...)
        : members_() {}

    /// Member-injection construction: configured, possibly
    /// non-default-constructible members are moved into the tuple — this is
    /// what makes CompositeX<ConsoleX, JsonFileX, NullX> constructible
    /// around a path-configured file sink.
    explicit Fanout(Sinks... members)
        requires(sizeof...(Sinks) > 0)
        : members_(std::move(members)...) {}

    Fanout(const Fanout&) = delete;
    Fanout& operator=(const Fanout&) = delete;
    Fanout(Fanout&&) = delete;
    Fanout& operator=(Fanout&&) = delete;

    /// Member access by index.
    template <std::size_t Index> [[nodiscard]] decltype(auto) get() noexcept {
        static_assert(Index < sizeof...(Sinks), "index beyond the member pack");
        return std::get<Index>(members_);
    }
    /// Const member access by index.
    template <std::size_t Index> [[nodiscard]] decltype(auto) get() const noexcept {
        static_assert(Index < sizeof...(Sinks), "index beyond the member pack");
        return std::get<Index>(members_);
    }

    /// Member access by type (must be unique in the pack).
    template <class T> [[nodiscard]] T& get() noexcept {
        static_assert(exactly_one<T>(), "type absent or duplicated — get<Index>() for duplicates");
        return std::get<find_index<T>()>(members_);
    }
    /// Const member access by type (must be unique in the pack).
    template <class T> [[nodiscard]] const T& get() const noexcept {
        static_assert(exactly_one<T>(), "type absent or duplicated — get<Index>() for duplicates");
        return std::get<find_index<T>()>(members_);
    }
};

} // namespace nexigen::core::sink_pack
