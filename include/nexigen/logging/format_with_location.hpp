// nexigen::logging — compile-time format string + call-site location wrapper
// (change add-serialization-and-wait-free-logging, design D3; spec
// logging/format-api).
//
// FormatWithLocation<Args...> bundles a std::format_string<Args...> (the
// literal whose format/argument types are checked at compile time by the
// consteval constructor) with the std::source_location of the statement that
// built it. Because the location defaults to std::source_location::current()
// inside a consteval constructor, it is evaluated at the CALL SITE — records
// carry the caller's file/line/function, never a library-internal frame.
//
// The type adds no producer-side formatting (spec logging/format-api):
// construction stores two values; rendering happens on the consumer thread
// through the type-erased callback packed into the LogRecord (design D5).
//
// Deduction contract: public call shapes are
//     logger.info("literal");                    // Args = {} (empty pack)
//     logger.info("lit {}", value);              // Args deduced from values
// The format-string parameter is a non-deduced context for a literal (a
// const char[N] is not a FormatWithLocation, so it contributes no candidates);
// Args therefore deduce from the trailing arguments only, and the literal
// converts implicitly through the consteval constructor afterwards.
#pragma once

#include <format>
#include <source_location>
#include <utility>

namespace nexigen::logging {

/// Checked format string + the source location of the statement that passed it.
template <typename... Args>
struct FormatWithLocation {
    /// Checked compile-time format (pointer into static literal storage).
    std::format_string<Args...> fmt;
    /// Location captured at the call site (consteval evaluation context).
    std::source_location loc;

    /// The only way to build one: a character-array literal whose format/
    /// argument types are checked by std::format_string's consteval scanner,
    /// plus the implicit location of the caller's statement. Constructing
    /// directly from the array keeps the implicit conversion a SINGLE
    /// user-defined step (char*→format_string→wrapper would chain two and be
    /// rejected by overload resolution). Not runtime-constructible: with the
    /// location defaulted inside a consteval constructor, current() resolves
    /// where the call is written, not inside library machinery.
    template <std::size_t N>
    consteval FormatWithLocation(const char (&literal)[N],
                                 std::source_location location =
                                     std::source_location::current()) noexcept
        : fmt(literal), loc(location) {}
};

namespace detail {

/// Non-deduced rendering of the wrapper type. Using bare
/// FormatWithLocation<Args...> as the first variadic parameter would make it
/// a DUCED context: a string literal (const char[N]) mismatches the class
/// template head and fails the whole template-argument deduction before the
/// implicit consteval conversion can ever apply. Going through this identity
/// alias makes Args deduce from the trailing arguments only (an empty pack
/// vacuously for a bare literal call), after which the literal converts
/// through the single user-defined consteval constructor.
template <class... Args>
struct format_param {
    using type = FormatWithLocation<Args...>;
};

template <class... Args>
using format_param_t = typename format_param<Args...>::type;

} // namespace detail

} // namespace nexigen::logging
