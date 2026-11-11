// nexigen::sm — the Function Group state payload (change add-sm; extracted in
// add-app-phm-client so the app-side PHM client can read the published state
// without pulling the whole StatePublication type).
//
// Fixed-size, trivially copyable: the Field pattern payload every application
// reads through the shared-memory state service.
#pragma once

#include <cstddef>
#include <format>
#include <string>

#include <nexigen/com/concepts/message_concept.hpp>

namespace nexigen::sm {

/// Maximum length of a published group-state string.
inline constexpr std::size_t k_group_state_length = 32;

/// The published payload of one Function Group's state (trivially copyable).
struct GroupStatePayload {
    char state[k_group_state_length]{};

    /// Cold-path diagnostic (mandate 9).
    [[nodiscard]] std::string to_string() const {
        return std::format("GroupStatePayload(state={})", std::string(state));
    }
};

/// The payload contract: plain bytes, safe for zero-copy transport.
static_assert(com::concepts::Message<GroupStatePayload>);

/// Free helper (mandate 9): mirrors the member.
[[nodiscard]] inline std::string to_string(const GroupStatePayload& payload) {
    return payload.to_string();
}

} // namespace nexigen::sm
