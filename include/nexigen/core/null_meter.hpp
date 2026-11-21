// nexigen::core — the default meter whose counters do nothing (change
// unify-core-concepts; spec core/meter-concepts "Null meter default").
//
// Instantiated by endpoints that are not instrumented. Every counter method is
// an empty inline function, so an endpoint templated on NullMeter contains no
// counter storage and no recording code.
#pragma once

#include <cstdint>
#include <string_view>

#include <nexigen/core/concepts/meter_concept.hpp>
#include <nexigen/core/result.hpp>

namespace nexigen::core {

/// @brief No-op meter used as the default template argument.
///
/// Satisfies `core::concepts::MeterLike`; its counter satisfies
/// `core::concepts::CounterLike` and every call compiles away.
///
/// @note Spec `core/meter-concepts`, requirement "Null meter default".
struct NullMeter {
    /// @brief No-op counter returned by `NullMeter`.
    struct Counter {
        /// @brief Add `delta` to the counter (the value is ignored).
        void add(std::uint64_t /*delta*/) noexcept {}
        /// @brief Increment the counter (the call compiles away).
        void inc() noexcept {}
    };

    /// @brief Return the shared no-op counter.
    ///
    /// Never fails, never allocates, and ignores both names. The template
    /// parameter pack matches the policy-shaped factory signature so the same
    /// `create_counter<>(...)` call compiles for `NullMeter` and `metrics::Meter`.
    ///
    /// @tparam Policies Policy-shaped factory arguments; every pack is ignored.
    /// @return Pointer to a function-local static counter.
    template <class... Policies>
    [[nodiscard]] static Result<Counter*> create_counter(std::string_view /*name*/,
                                                         std::string_view /*unit*/ = {}) noexcept {
        static Counter counter{};
        return &counter;
    }

    /// @brief Convenience overload that forwards to the policy-shaped factory.
    ///
    /// Lets callers write `create_counter("name")` without the empty template
    /// argument list; `MeterLike` still checks the template form.
    ///
    /// @param name Metric name; ignored.
    /// @param unit Optional unit string; ignored.
    /// @return Pointer to the shared no-op counter.
    [[nodiscard]] static Result<Counter*> create_counter(std::string_view name,
                                                         std::string_view unit = {}) noexcept {
        return create_counter<>(name, unit);
    }
};

static_assert(concepts::MeterLike<NullMeter>, "NullMeter must satisfy core::concepts::MeterLike");
static_assert(concepts::CounterLike<NullMeter::Counter>,
              "NullMeter::Counter must satisfy core::concepts::CounterLike");

} // namespace nexigen::core
