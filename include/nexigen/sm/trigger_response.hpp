// nexigen::sm — TriggerIn response payload (change add-sm; spec sm/ipc
// "TriggerIn request service").
//
// The arbitration outcome the SM returns to the requester. Trivially
// copyable (the TransitionResult enum is a uint8 under the hood).
#pragma once

#include <format>
#include <string>

#include <nexigen/com/concepts/message_concept.hpp>
#include <nexigen/sm/transition_result.hpp>

namespace nexigen::sm {

/// The SM's answer to one TriggerIn request.
struct TriggerResponse {
    TransitionResult result{TransitionResult::Success};

    /// Cold-path diagnostic (mandate 9).
    [[nodiscard]] std::string to_string() const {
        return std::format("TriggerResponse(result={})", nexigen::sm::to_string(result));
    }
};

/// The payload contract: plain bytes, safe for zero-copy transport.
static_assert(com::concepts::Message<TriggerResponse>);

/// Free helper (mandate 9): mirrors the member.
[[nodiscard]] inline std::string to_string(const TriggerResponse& response) {
    return response.to_string();
}

} // namespace nexigen::sm

/// The response formats as its diagnostic string (mandate 11).
template <> struct std::formatter<nexigen::sm::TriggerResponse> : std::formatter<std::string_view> {
    template <class Ctx> auto format(const nexigen::sm::TriggerResponse& response, Ctx& ctx) const {
        const std::string text = response.to_string();
        return std::formatter<std::string_view>::format(text, ctx);
    }
};
