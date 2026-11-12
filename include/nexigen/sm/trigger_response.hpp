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

/// @brief The SM's answer to one TriggerIn request.
///
/// A single `TransitionResult` value, byte-sized and trivially copyable, so it
/// can be returned zero-copy over the request-response transport. The
/// enumerators distinguish success, priority rejection, graph rejection, and a
/// watchdog timeout.
///
/// @note Spec `sm/ipc`, requirement "TriggerIn request service".
/// @note Spec `sm/state-machine`, requirement "Transition result vocabulary".
struct TriggerResponse {
    TransitionResult result{TransitionResult::Success}; ///< Arbitration outcome for the request.

    /// @brief Render the response as a diagnostic string.
    ///
    /// @return Text naming the result; allocates through `std::string` and
    ///         `std::format`.
    /// @note Cold-path diagnostic only (project mandate 9).
    [[nodiscard]] std::string to_string() const {
        return std::format("TriggerResponse(result={})", nexigen::sm::to_string(result));
    }
};

/// @brief Compile-time proof that the response satisfies `Message`.
///
/// The concept requires plain bytes and trivially copyable storage, so the
/// response can travel zero-copy over the com transport.
static_assert(com::concepts::Message<TriggerResponse>);

/// @brief Free diagnostic helper mirroring `TriggerResponse::to_string`.
///
/// @param response Response to render.
/// @return The member `to_string()` output.
/// @note Cold-path diagnostic only (project mandate 9).
[[nodiscard]] inline std::string to_string(const TriggerResponse& response) {
    return response.to_string();
}

} // namespace nexigen::sm

/// @brief `std::formatter` for `TriggerResponse` (project mandate 11).
///
/// Delegates to `std::formatter<std::string_view>` after rendering the member
/// diagnostic, so `std::format("{}", response)` equals `to_string()`.
template <> struct std::formatter<nexigen::sm::TriggerResponse> : std::formatter<std::string_view> {
    /// @brief Format the response as its diagnostic string.
    ///
    /// @tparam Ctx Format context type supplied by `std::format`.
    /// @param response Response to render.
    /// @param ctx Output iterator context receiving the text.
    /// @return Iterator past the written characters.
    template <class Ctx> auto format(const nexigen::sm::TriggerResponse& response, Ctx& ctx) const {
        const std::string text = response.to_string();
        return std::formatter<std::string_view>::format(text, ctx);
    }
};
