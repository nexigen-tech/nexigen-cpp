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

template <class... Args> struct format_param {
    using type = FormatWithLocation<Args...>;
};

template <class... Args> using format_param_t = typename format_param<Args...>::type;

} // namespace nexigen::logging::detail
