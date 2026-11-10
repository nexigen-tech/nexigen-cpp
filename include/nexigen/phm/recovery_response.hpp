// nexigen::phm — recovery response payload (change add-phm; spec
// phm/recovery; design D4).
//
// The Execution Manager's acknowledgement of a recovery request. Trivially
// copyable; the EM-side StateClient (implemented by the later nexigen::sm
// change) answers with this type.
#pragma once

#include <cstdint>
#include <format>
#include <string>

#include <nexigen/com/concepts/message_concept.hpp>

namespace nexigen::phm {

/// The EM's acknowledgement of one recovery request.
struct RecoveryResponse {
    std::uint32_t accepted{0};  ///< 1 = the EM accepted the action, 0 = rejected.

    /// Cold-path diagnostic (mandate 9).
    [[nodiscard]] std::string to_string() const {
        return std::format("RecoveryResponse(accepted={})", accepted);
    }
};

/// The payload contract: plain bytes, safe for zero-copy transport.
static_assert(com::concepts::Message<RecoveryResponse>);

/// Free helper (mandate 9): mirrors the member.
[[nodiscard]] inline std::string to_string(const RecoveryResponse& response) {
    return response.to_string();
}

} // namespace nexigen::phm

/// The response formats as its diagnostic string (mandate 11).
template <> struct std::formatter<nexigen::phm::RecoveryResponse> : std::formatter<std::string_view> {
    template <class Ctx>
    auto format(const nexigen::phm::RecoveryResponse& response, Ctx& ctx) const {
        const std::string text = response.to_string();
        return std::formatter<std::string_view>::format(text, ctx);
    }
};
