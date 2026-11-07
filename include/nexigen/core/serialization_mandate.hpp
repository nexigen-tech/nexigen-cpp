// nexigen::core — true-universal serialization mandate machinery
// (change refactor-serialization-mandate, analysis P3-28; spec
// core/serialization mandates the OUTCOMES, this header carries their
// cost once instead of per type).
//
// The project mandate ("serialization trio + std::formatter") stays: it is
// the transmission surface every struct and enum is required to satisfy.
// What collapses here is the per-type BOILERPLATE:
//  1. `std::formatter<T>` delegating to `to_string` — ONE constrained
//     partial covers the whole delegate family;
//  2. the per-enum `to_json`/`from_json` one-liners STAY: [basic.lookup.
//     argdep] gives an ENUM type a single-element associated-namespace set
//     (its immediate namespace), so a generic pair hosted at nexigen::
//     would be unreachable through ADL for module enums — verified
//     experimentally before this final layout (structs would get the full
//     enclosing chain, but struct JSON is per-field by design, not a
//     delegate);
//  3. the free `to_string(const T&) { return v.to_string(); }` pair — a
//     constrained generic `nexigen::to_string` template. Ordinary
//     unqualified lookup from a nexigen::<module> TU walks the enclosing
//     nexigen scope, so a bare `to_string(v)` call resolves to the generic
//     when (and only when) no closer per-type definition exists (test
//     scopes outside nexigen still qualify or keep their using).
//
// A type implements the whole surface by writing `to_string()` (struct)
// plus `to_string`/`from_string` (enum). Nothing else is needed; existing
// per-type definitions OUTRANK the generic (non-template beats template)
// where a module wants a custom shape.
//
// NOTE on the namespace: helpers live in `nexigen::core::mandate` — a
// dedicated nested namespace so `using namespace nexigen::core` test scopes
// never see a second bare `detail` candidate (the same reason the shared
// FixedWriter sits in nexigen::core directly).
#pragma once

#include <concepts>
#include <format>
#include <string>
#include <string_view>
#include <type_traits>

#include <nlohmann/json.hpp>

#include <nexigen/core/result.hpp> // Status/ErrorCode vocabulary only

namespace nexigen::core::mandate {

/// True when the `to_string` resolve is found at ADL/unqualified lookup for
/// `T` and yields a renderable view (std::string converts to string_view;
/// per-enum constexpr helpers return string_view directly): the contract
/// the generic formatter and the generic JSON pair rely on. Member
/// to_string participates through the `v.to_string()` arm; free/namespace
/// functions through the plain call arm.
template <typename T>
concept MandateToString = (requires(const T& v) {
                              { v.to_string() } -> std::convertible_to<std::string_view>;
                          }) || (requires(const T& v) {
                              { to_string(v) } -> std::convertible_to<std::string_view>;
                          });

/// True when an enum resolves its canonical parser: together with the
/// string mandate this is what JSON serialization dispatches on.
template <typename T>
concept MandateFromString = (requires(T& v, std::string_view name) {
                                { from_string(name, v) } -> std::same_as<bool>;
                            }) || (requires(T& v, std::string_view name) {
                                { from_string(name, v) };
                            });

/// Enum with both vocabulary arms.
template <typename T>
concept MandateEnum = std::is_enum_v<T> && MandateToString<T> && MandateFromString<T>;

/// The one delegate formatter: renders through `to_string(T)`. Because
/// the ADL supplied candidates include the member through `v.to_string()`
/// the body dispatches per arm.
template <MandateToString T> struct MandateFormatter : std::formatter<std::string_view> {
    template <class Ctx> constexpr auto format(const T& value, Ctx& ctx) const {
        if constexpr (requires {
                          { value.to_string() };
                      }) {
            return std::formatter<std::string_view>::format(value.to_string(), ctx);
        } else {
            return std::formatter<std::string_view>::format(to_string(value), ctx);
        }
    }
};

} // namespace nexigen::core::mandate

namespace nexigen {

/// The one mandate free function: a constrained generic forwarder over the
/// member contract. Enclosing-scope ordinary lookup makes a bare
/// `to_string(v)` call inside nexigen::<module> TUs resolve here whenever
/// no closer non-template (per-type) definition exists, so NEW types need
/// only the member to reach every caller. Existing per-type friends
/// outrank this template (non-template beats template) — no ambiguity.
template <nexigen::core::mandate::MandateToString T>
[[nodiscard]] std::string to_string(const T& value) {
    if constexpr (requires {
                      { value.to_string() };
                  }) {
        return value.to_string();
    } else {
        return std::string(to_string(value)); // strings melt into a view copy
    }
}

} // namespace nexigen

namespace std {

/// Render any mandate type through its own to_string — one definition for
/// the whole delegate family (spec core/serialization "Universal
/// std::formatter" outcome).
template <nexigen::core::mandate::MandateToString T>
struct formatter<T> : nexigen::core::mandate::MandateFormatter<T> {};

} // namespace std
