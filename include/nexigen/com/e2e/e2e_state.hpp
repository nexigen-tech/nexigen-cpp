// nexigen::com — per-endpoint E2E counters (change add-e2e-protection).
#pragma once

#include <cstdint>
#include <format>
#include <string>
#include <string_view>

namespace nexigen::com {

/// @brief Counters maintained by one E2E endpoint.
///
/// @note Spec `com/e2e`, requirement "Receiver supervision".
struct E2eState {
    std::uint64_t checked{0};        ///< Every message that reached the check.
    std::uint64_t failed{0};         ///< Length, data-id, or checksum failures.
    std::uint64_t repeated{0};       ///< Duplicate counters.
    std::uint64_t wrong_sequence{0}; ///< Gaps above the maximum or backwards counters.

    /// @brief Cold-path diagnostic rendering.
    /// @return `E2eState(checked=..., failed=..., repeated=..., wrong_sequence=...)`.
    /// @note Spec `com/e2e`, requirement "Receiver supervision".
    [[nodiscard]] std::string to_string() const {
        return std::format("E2eState(checked={}, failed={}, repeated={}, wrong_sequence={})",
                           checked, failed, repeated, wrong_sequence);
    }
};

/// @brief Free helper mirroring `E2eState::to_string()` (mandate 9).
///
/// @param state Counters to render.
/// @return The diagnostic string.
/// @note Spec `com/e2e`, requirement "Receiver supervision".
[[nodiscard]] inline std::string to_string(const E2eState& state) {
    return state.to_string();
}

} // namespace nexigen::com

/// @brief `std::formatter` for E2E counters (project mandate 11).
template <> struct std::formatter<nexigen::com::E2eState> : std::formatter<std::string_view> {
    /// @brief Write the counter diagnostic into the format context.
    /// @tparam Ctx `std::format` context type.
    /// @param state Counters to format.
    /// @param ctx Output context supplied by `std::format`.
    /// @return Iterator one past the last written character.
    /// @note Spec `com/e2e`, requirement "Receiver supervision".
    template <class Ctx> auto format(const nexigen::com::E2eState& state, Ctx& ctx) const {
        return std::formatter<std::string_view>::format(state.to_string(), ctx);
    }
};
