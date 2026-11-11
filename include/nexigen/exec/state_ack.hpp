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

/// The EM's acknowledgement of one SetState command.
struct StateAck {
    std::uint32_t accepted{0}; ///< 1 = the EM applied the switch, 0 = rejected.

    /// Cold-path diagnostic (mandate 9).
    [[nodiscard]] std::string to_string() const {
        return std::format("StateAck(accepted={})", accepted);
    }
};

/// The payload contract: plain bytes, safe for zero-copy transport.
static_assert(com::concepts::Message<StateAck>);

/// Free helper (mandate 9): mirrors the member.
[[nodiscard]] inline std::string to_string(const StateAck& ack) {
    return ack.to_string();
}

} // namespace nexigen::exec

/// The acknowledgement formats as its diagnostic string (mandate 11).
template <> struct std::formatter<nexigen::exec::StateAck> : std::formatter<std::string_view> {
    template <class Ctx> auto format(const nexigen::exec::StateAck& ack, Ctx& ctx) const {
        const std::string text = ack.to_string();
        return std::formatter<std::string_view>::format(text, ctx);
    }
};
