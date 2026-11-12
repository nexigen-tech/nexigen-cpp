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

/// @brief Checked format string bundled with the source location of the statement that passed it.
///
/// The value is a short-lived call-site object: the producer constructs it,
/// copies the two fields into the record, and formats nothing. `fmt` points
/// into static literal storage and `loc` into the compiler's static location
/// table, so neither field aliases caller memory after the call returns.
///
/// @tparam Args Argument types accepted by the format string; each must satisfy
///         `std::formattable` for the corresponding replacement field, which
///         `std::format_string` enforces at compile time.
/// @note Spec `logging/format-api`, requirement "FormatWithLocation wrapper type".
template <typename... Args> struct FormatWithLocation {
    /// @brief Checked compile-time format; a pointer into static literal storage.
    std::format_string<Args...> fmt;
    /// @brief Location of the call site, captured in the consteval evaluation context.
    std::source_location loc;

    /// @brief Build the wrapper from a literal, checking format/argument types at compile time.
    ///
    /// Constructing directly from the character array keeps the implicit
    /// conversion a SINGLE user-defined step: `char*` → `format_string` →
    /// wrapper would chain two conversions and be rejected by overload
    /// resolution.
    ///
    /// @tparam N Size of the literal array including the terminating NUL, deduced
    ///         from the argument.
    /// @param literal NUL-terminated format literal whose replacement fields must
    ///        match `Args`.
    /// @param location Call-site location; defaults to `std::source_location::current()`,
    ///        which resolves where the statement is written because the constructor
    ///        is `consteval`.
    /// @note Spec `logging/format-api`, requirement "Compile-time format/argument checking".
    /// @note Spec `logging/format-api`, requirement "Wrapper adds no producer-side formatting".
    /// @warning Not runtime-constructible: a non-constant format string is a
    ///          compile error, so this type can never reach a hot path with an
    ///          unchecked literal.
    template <std::size_t N>
    consteval FormatWithLocation(
        const char (&literal)[N],
        std::source_location location = std::source_location::current()) noexcept
        : fmt(literal), loc(location) {}
};

} // namespace nexigen::logging
