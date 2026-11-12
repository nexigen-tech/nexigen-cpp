// nexigen::diagnostics — borrowed-slot sink for ScopedStopwatch (design D7).
#pragma once

#include <cstdint>

namespace nexigen::diagnostics::detail {

/// @brief Borrowed destination slot used by the out-reference guard flavors.
///
/// Stores nothing but the caller's pointer; the guard invokes it exactly once
/// at scope exit to write the elapsed nanoseconds. It never allocates and never
/// throws, so the out-ref construction path stays as cheap as the callable one.
///
/// @note Spec `diagnostics/scoped-stopwatch`, requirement "Construction
///       overloads and default-clock fallback"; hot path only.
struct OutRefSink {
    std::uint64_t* out; ///< Borrowed destination slot (never null in practice).

    /// @brief Bind the adapter to a caller-owned slot.
    ///
    /// @param slot Destination written once at scope exit; must outlive the
    ///        guard.
    constexpr explicit OutRefSink(std::uint64_t* slot) noexcept : out{slot} {}

    /// @brief Write the scope's elapsed nanoseconds to the bound slot.
    ///
    /// @param elapsed_ns Elapsed nanoseconds measured by the guard.
    /// @note A null slot makes the call a harmless no-op; the guard's delivery
    ///       remains `noexcept`.
    void operator()(std::uint64_t elapsed_ns) const noexcept {
        if (out != nullptr) {
            *out = elapsed_ns;
        }
    }
};

} // namespace nexigen::diagnostics::detail
