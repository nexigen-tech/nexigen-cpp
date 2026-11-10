// nexigen::sm — TriggerIn request payload (change add-sm; spec sm/ipc
// "TriggerIn request service").
//
// The typed fixed-size request applications send to the SM: no dynamic
// allocations, no pointers (REQ_APP_014). Trivially copyable, so it travels
// zero-copy over the com request-response transport.
#pragma once

#include <cstddef>
#include <format>
#include <string>

#include <nexigen/com/concepts/message_concept.hpp>

namespace nexigen::sm {

/// Maximum length of the name buffers in a trigger request.
inline constexpr std::size_t k_trigger_name_length = 32;

/// One mode-change request from an authorized process.
struct TriggerRequest {
    char function_group[k_trigger_name_length]{};
    char target_state[k_trigger_name_length]{};
    char requester_id[k_trigger_name_length]{};

    /// Cold-path diagnostic (mandate 9).
    [[nodiscard]] std::string to_string() const {
        return std::format("TriggerRequest(function_group={}, target_state={}, requester_id={})",
                           std::string(function_group), std::string(target_state),
                           std::string(requester_id));
    }
};

/// The payload contract: plain bytes, safe for zero-copy transport.
static_assert(com::concepts::Message<TriggerRequest>);

/// Free helper (mandate 9): mirrors the member.
[[nodiscard]] inline std::string to_string(const TriggerRequest& request) {
    return request.to_string();
}

} // namespace nexigen::sm

/// The request formats as its diagnostic string (mandate 11).
template <> struct std::formatter<nexigen::sm::TriggerRequest> : std::formatter<std::string_view> {
    template <class Ctx>
    auto format(const nexigen::sm::TriggerRequest& request, Ctx& ctx) const {
        const std::string text = request.to_string();
        return std::formatter<std::string_view>::format(text, ctx);
    }
};
