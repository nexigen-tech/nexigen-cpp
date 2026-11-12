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

/// @brief Maximum length of the name buffers in a trigger request.
///
/// Each buffer includes its terminating NUL, so a stored name is at most 31
/// bytes.
inline constexpr std::size_t k_trigger_name_length = 32;

/// @brief One mode-change request from an authorized process.
///
/// Three inline fixed buffers identify the group, the requested target, and the
/// requester whose priority drives arbitration. There are no pointers or
/// dynamic members, so the request is trivially copyable and safe for
/// zero-copy transport.
///
/// @note Spec `sm/ipc`, requirement "TriggerIn request service" (typed
///       fixed-size structures, no dynamic allocations, no pointers).
struct TriggerRequest {
    char function_group[k_trigger_name_length]{}; ///< Group to transition.
    char target_state[k_trigger_name_length]{};   ///< Requested destination state.
    char requester_id[k_trigger_name_length]{};   ///< Requester identity for arbitration.

    /// @brief Render the request as a diagnostic string.
    ///
    /// @return Text naming the group, target, and requester; allocates through
    ///         `std::string` and `std::format`.
    /// @note Cold-path diagnostic only (project mandate 9).
    [[nodiscard]] std::string to_string() const {
        return std::format("TriggerRequest(function_group={}, target_state={}, requester_id={})",
                           std::string(function_group), std::string(target_state),
                           std::string(requester_id));
    }
};

/// @brief Compile-time proof that the request satisfies `Message`.
///
/// The concept requires plain bytes and trivially copyable storage, so the
/// request can travel zero-copy over the com transport.
static_assert(com::concepts::Message<TriggerRequest>);

/// @brief Free diagnostic helper mirroring `TriggerRequest::to_string`.
///
/// @param request Request to render.
/// @return The member `to_string()` output.
/// @note Cold-path diagnostic only (project mandate 9).
[[nodiscard]] inline std::string to_string(const TriggerRequest& request) {
    return request.to_string();
}

} // namespace nexigen::sm

/// @brief `std::formatter` for `TriggerRequest` (project mandate 11).
///
/// Delegates to `std::formatter<std::string_view>` after rendering the member
/// diagnostic, so `std::format("{}", request)` equals `to_string()`.
template <> struct std::formatter<nexigen::sm::TriggerRequest> : std::formatter<std::string_view> {
    /// @brief Format the request as its diagnostic string.
    ///
    /// @tparam Ctx Format context type supplied by `std::format`.
    /// @param request Request to render.
    /// @param ctx Output iterator context receiving the text.
    /// @return Iterator past the written characters.
    template <class Ctx> auto format(const nexigen::sm::TriggerRequest& request, Ctx& ctx) const {
        const std::string text = request.to_string();
        return std::formatter<std::string_view>::format(text, ctx);
    }
};
