// nexigen::exec — StateClient command payload (change add-sm;
// spec exec/state-client; design D4).
//
// The typed SetState command the State Manager sends to the EM. Trivially
// copyable (fixed char buffers, no owning members) so it travels zero-copy
// over the com request-response transport.
#pragma once

#include <cstddef>
#include <format>
#include <string>

#include <nexigen/com/concepts/message_concept.hpp>

namespace nexigen::exec {

/// @brief Maximum length of the name buffers in a state command.
///
/// Includes the terminating NUL; names longer than the remaining capacity are
/// rejected by the sender rather than truncated silently.
///
/// @note Spec `exec/state-client`, requirement "StateClient command reception".
inline constexpr std::size_t k_state_command_name_length = 32;

/// @brief One `SetState` command: the Function Group and its target state.
///
/// A fixed-size, trivially copyable payload with no owning members, so it is
/// safe for zero-copy transport over the com request-response channel. The
/// receiver (`StateClient`) maps `function_group` onto the EM's managed
/// processes and acknowledges completion or rejection. Both buffers are
/// NUL-terminated.
///
/// @note Spec `exec/state-client`, requirement "StateClient command reception".
/// @note Spec `exec/state-client`, requirement "Function Group execution".
struct StateCommand {
    char function_group[k_state_command_name_length]{}; ///< Target Function Group name.
    char target_state[k_state_command_name_length]{};   ///< Requested state name.

    /// @brief Cold-path diagnostic (mandate 9).
    ///
    /// @return `StateCommand(function_group=..., target_state=...)`.
    [[nodiscard]] std::string to_string() const {
        return std::format("StateCommand(function_group={}, target_state={})",
                           std::string(function_group), std::string(target_state));
    }
};

/// @brief The payload contract: plain bytes, safe for zero-copy transport.
static_assert(com::concepts::Message<StateCommand>);

/// @brief Free helper (mandate 9): mirrors the member.
///
/// @param command Command to render.
/// @return The same text as `command.to_string()`.
[[nodiscard]] inline std::string to_string(const StateCommand& command) {
    return command.to_string();
}

} // namespace nexigen::exec

/// @brief The command formats as its diagnostic string (mandate 11).
template <> struct std::formatter<nexigen::exec::StateCommand> : std::formatter<std::string_view> {
    /// @brief Format the command through its `to_string()` member.
    ///
    /// @tparam Ctx `std::formatter` context type.
    /// @param command Command to format.
    /// @param ctx Format context receiving the text.
    /// @return The context's output iterator.
    template <class Ctx> auto format(const nexigen::exec::StateCommand& command, Ctx& ctx) const {
        const std::string text = command.to_string();
        return std::formatter<std::string_view>::format(text, ctx);
    }
};
