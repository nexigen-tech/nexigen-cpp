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

/// @brief True when `T` renders to a string view via member or free
///        `to_string`.
///
/// Accepts either `v.to_string()` or a `to_string(v)` found by ordinary/ADL
/// lookup, each convertible to `std::string_view`. This is the contract the
/// generic formatter and the generic JSON pair rely on: the member arm covers
/// structs, the free arm covers per-enum constexpr helpers.
///
/// @tparam T Type probed for a renderable `to_string` resolution.
/// @note Spec `core/serialization`, requirement "Universal cold-path to_string
///       contract".
template <typename T>
concept MandateToString = (requires(const T& v) {
                              { v.to_string() } -> std::convertible_to<std::string_view>;
                          }) || (requires(const T& v) {
                              { to_string(v) } -> std::convertible_to<std::string_view>;
                          });

/// @brief True when `T` resolves a canonical parser from a name.
///
/// Accepts `from_string(std::string_view, T&)` returning `bool` or returning
/// nothing; together with `MandateToString` this is what JSON serialization
/// dispatches on.
///
/// @tparam T Type probed for a `from_string` overload.
/// @note Spec `core/serialization`, requirement "Universal enum string and JSON
///       vocabulary".
template <typename T>
concept MandateFromString = (requires(T& v, std::string_view name) {
                                { from_string(name, v) } -> std::same_as<bool>;
                            }) || (requires(T& v, std::string_view name) {
                                { from_string(name, v) };
                            });

/// @brief Enum satisfying both vocabulary arms.
///
/// @tparam T Type constrained to `std::is_enum_v<T>`, `MandateToString<T>` and
///         `MandateFromString<T>`.
/// @note Spec `core/serialization`, requirement "Universal enum string and JSON
///       vocabulary".
template <typename T>
concept MandateEnum = std::is_enum_v<T> && MandateToString<T> && MandateFromString<T>;

/// @brief The one delegate formatter: renders any mandate type through
///        `to_string`.
///
/// Derives from `std::formatter<std::string_view>`, so fill, width and
/// alignment specs follow the string-view rules. The body dispatches per arm:
/// member `v.to_string()` when present, otherwise free `to_string(v)`.
///
/// @tparam T Type satisfying `MandateToString`.
/// @note Spec `core/serialization`, requirement "Universal enum std::formatter
///       specialization"; mandate 11. Cold path only.
template <MandateToString T> struct MandateFormatter : std::formatter<std::string_view> {
    /// @brief Format `value` into `ctx` as its `to_string` text.
    ///
    /// @tparam Ctx Format context type, for example `std::format_context`.
    /// @param value Object to render.
    /// @param ctx Format context receiving the output.
    /// @return Iterator past the last character written into the context.
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

/// @brief The one mandate free function: a constrained forwarder over the
///        member contract.
///
/// Enclosing-scope ordinary lookup makes a bare `to_string(v)` call inside
/// `nexigen::<module>` translation units resolve here whenever no closer
/// non-template definition exists, so a new type needs only its member.
/// Existing per-type functions outrank this template (non-template beats
/// template), so there is no ambiguity.
///
/// @tparam T Type satisfying `nexigen::core::mandate::MandateToString`.
/// @param value Object to render.
/// @return Owned copy of the rendered text (allocates).
/// @note Spec `core/serialization`, requirement "Universal cold-path to_string
///       contract"; mandate 9. Cold path only.
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

/// @brief Render any mandate type through its own `to_string`.
///
/// One definition for the whole delegate family; the base supplies parsing and
/// the string-view fill/align rules.
///
/// @tparam T Type satisfying `nexigen::core::mandate::MandateToString`.
/// @note Spec `core/serialization`, requirement "Universal enum std::formatter
///       specialization"; mandate 11.
template <nexigen::core::mandate::MandateToString T>
struct formatter<T> : nexigen::core::mandate::MandateFormatter<T> {};

} // namespace std
