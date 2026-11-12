// nexigen::exec — StateClient acknowledgement payload (change add-sm;
// spec exec/state-client; design D4).
//
// The EM's acknowledgement of one SetState command. Trivially copyable.
#pragma once

#include <cstdint>
#include <format>
#include <string>

#include <nexigen/com/concepts/message_concept.hpp>

namespace nexigen::exec {

/// @brief The EM's acknowledgement of one `SetState` command.
///
/// A trivially copyable payload with no owning members, safe for zero-copy
/// transport. The receiver sets `accepted` after it has executed (or rejected)
/// the switch; the response is written before the command is considered
/// handled.
///
/// @note Spec `exec/state-client`, requirement "StateClient command reception".
/// @note Spec `exec/state-client`, requirement "Function Group execution".
struct StateAck {
    std::uint32_t accepted{0}; ///< 1 = the EM applied the switch, 0 = rejected.

    /// @brief Cold-path diagnostic (mandate 9).
    ///
    /// @return `StateAck(accepted=<0|1>)`.
    [[nodiscard]] std::string to_string() const {
        return std::format("StateAck(accepted={})", accepted);
    }
};

/// @brief The payload contract: plain bytes, safe for zero-copy transport.
static_assert(com::concepts::Message<StateAck>);

/// @brief Free helper (mandate 9): mirrors the member.
///
/// @param ack Acknowledgement to render.
/// @return The same text as `ack.to_string()`.
[[nodiscard]] inline std::string to_string(const StateAck& ack) {
    return ack.to_string();
}

} // namespace nexigen::exec

/// @brief The acknowledgement formats as its diagnostic string (mandate 11).
template <> struct std::formatter<nexigen::exec::StateAck> : std::formatter<std::string_view> {
    /// @brief Format the acknowledgement through its `to_string()` member.
    ///
    /// @tparam Ctx `std::formatter` context type.
    /// @param ack Acknowledgement to format.
    /// @param ctx Format context receiving the text.
    /// @return The context's output iterator.
    template <class Ctx> auto format(const nexigen::exec::StateAck& ack, Ctx& ctx) const {
        const std::string text = ack.to_string();
        return std::formatter<std::string_view>::format(text, ctx);
    }
};
