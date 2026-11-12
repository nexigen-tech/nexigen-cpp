// nexigen::logging — non-deduced rendering of the FormatWithLocation wrapper
// type (change add-serialization-and-wait-free-logging, design D3).
//
// Using bare FormatWithLocation<Args...> as the first variadic parameter of a
// logger call would make it a DEDUCED context: a string literal (const char[N])
// mismatches the class template head and fails the whole template-argument
// deduction before the implicit consteval conversion can ever apply. Going
// through this identity alias makes Args deduce from the trailing arguments
// only, after which the literal converts through the single user-defined
// consteval constructor.
#pragma once

#include <nexigen/logging/format_with_location.hpp>

namespace nexigen::logging::detail {

/// @brief Identity metafunction that names `FormatWithLocation<Args...>`
///        without making it a deduced parameter.
///
/// Logger calls spell the format parameter as `format_param_t<Args...>`, so
/// `Args` deduce from the trailing arguments only and a string literal still
/// converts through `FormatWithLocation`'s single `consteval` constructor.
/// Compile-time-only helper: no state, no runtime work.
///
/// @tparam Args Format argument types of the logger call; each must be
///         formattable by `std::format_string` and packable into `LogRecord`.
/// @note Spec `logging/format-api`, requirement "FormatWithLocation wrapper
///       type"; the non-deduced spelling keeps requirement "Compile-time
///       format/argument checking" effective before any logging code runs.
template <class... Args> struct format_param {
    /// @brief The wrapper type the logger call accepts.
    using type = FormatWithLocation<Args...>;
};

/// @brief Convenience alias for `format_param<Args...>::type`.
///
/// @tparam Args Format argument types of the logger call; each must be
///         formattable by `std::format_string` and packable into `LogRecord`.
/// @note Spec `logging/format-api`, requirement "FormatWithLocation wrapper
///       type".
template <class... Args> using format_param_t = typename format_param<Args...>::type;

} // namespace nexigen::logging::detail
