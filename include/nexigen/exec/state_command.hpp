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

/// Maximum length of the name buffers in a state command.
inline constexpr std::size_t k_state_command_name_length = 32;

/// One SetState command: the Function Group and its target state.
struct StateCommand {
    char function_group[k_state_command_name_length]{};
    char target_state[k_state_command_name_length]{};

    /// Cold-path diagnostic (mandate 9).
    [[nodiscard]] std::string to_string() const {
        return std::format("StateCommand(function_group={}, target_state={})",
                           std::string(function_group), std::string(target_state));
    }
};

/// The payload contract: plain bytes, safe for zero-copy transport.
static_assert(com::concepts::Message<StateCommand>);

/// Free helper (mandate 9): mirrors the member.
[[nodiscard]] inline std::string to_string(const StateCommand& command) {
    return command.to_string();
}

} // namespace nexigen::exec

/// The command formats as its diagnostic string (mandate 11).
template <> struct std::formatter<nexigen::exec::StateCommand> : std::formatter<std::string_view> {
    template <class Ctx>
    auto format(const nexigen::exec::StateCommand& command, Ctx& ctx) const {
        const std::string text = command.to_string();
        return std::formatter<std::string_view>::format(text, ctx);
    }
};
