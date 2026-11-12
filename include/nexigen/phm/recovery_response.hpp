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

/// @brief The EM's acknowledgement of one recovery request.
///
/// The response payload of the PHM->EM request-response channel; trivially
/// copyable, so it travels zero-copy. Deliberately minimal: the daemon only
/// needs to know whether the Execution Manager accepted the action.
///
/// @note Spec `phm/recovery`, requirement "Local process recovery".
struct RecoveryResponse {
    std::uint32_t accepted{0}; ///< 1 = the EM accepted the action, 0 = rejected.

    /// @brief Cold-path diagnostic reporting the acknowledgement.
    ///
    /// @return Text of the form `RecoveryResponse(accepted=<0|1>)`.
    /// @note Mandate 9 (universal `to_string`); allocates, so cold path only.
    ///       Spec `phm/recovery`, requirement "Local process recovery".
    [[nodiscard]] std::string to_string() const {
        return std::format("RecoveryResponse(accepted={})", accepted);
    }
};

/// @brief The payload contract: plain bytes, safe for zero-copy transport.
///
/// @note Spec `phm/recovery`, requirement "Local process recovery".
static_assert(com::concepts::Message<RecoveryResponse>);

/// @brief Free helper mirroring `RecoveryResponse::to_string()` (mandate 9).
///
/// @param response Response to render.
/// @return The response's diagnostic text.
/// @note Mandate 9; allocates, so cold path only.
[[nodiscard]] inline std::string to_string(const RecoveryResponse& response) {
    return response.to_string();
}

} // namespace nexigen::phm

/// @brief Formats a `RecoveryResponse` as its diagnostic string.
///
/// Delegates to the member `to_string`, so `std::format("{}", response)` equals
/// the member output.
///
/// @note Mandate 11 (universal `std::formatter`); cold path only. Spec
///       `phm/recovery`, requirement "Local process recovery".
template <>
struct std::formatter<nexigen::phm::RecoveryResponse> : std::formatter<std::string_view> {
    /// @brief Write the response's diagnostic text into the format context.
    ///
    /// @tparam Ctx `std::format` context type, for example
    ///         `std::format_context`.
    /// @param response Response to format.
    /// @param ctx Output context supplied by `std::format`.
    /// @return Iterator one past the last written character.
    template <class Ctx>
    auto format(const nexigen::phm::RecoveryResponse& response, Ctx& ctx) const {
        const std::string text = response.to_string();
        return std::formatter<std::string_view>::format(text, ctx);
    }
};
