// nexigen::phm — recovery request payload (change add-phm; spec
// phm/recovery; design D4).
//
// The request the daemon sends over the PHM→EM request-response channel.
// Trivially copyable (com::concepts::Message): fixed char buffers instead of
// owning strings, so it travels zero-copy.
#pragma once

#include <cstddef>
#include <cstdint>
#include <format>
#include <string>

#include <nexigen/com/concepts/message_concept.hpp>
#include <nexigen/phm/recovery_action.hpp>

namespace nexigen::phm {

/// @brief Maximum length of the process-name / function-group buffers.
///
/// A fixed bound keeps `RecoveryRequest` trivially copyable, so the payload
/// travels zero-copy over the request-response channel.
///
/// @note Spec `phm/recovery`, requirement "Local process recovery".
inline constexpr std::size_t k_recovery_name_length = 32;

/// @brief One recovery command: an action plus its target identity.
///
/// The payload of the PHM->EM request-response channel. Fixed `char` buffers
/// replace owning strings, so the type satisfies `com::concepts::Message` and
/// is delivered without a serialization step. Buffers are NUL-terminated by
/// the sender; a value with an empty buffer names no target.
///
/// @note Spec `phm/recovery`, requirement "Local process recovery": the
///       restart request carries the process identity (`pid`/name) and is
///       delivered without a serialization step.
struct RecoveryRequest {
    RecoveryAction action{RecoveryAction::RestartProcess}; ///< Command to execute.
    std::uint32_t pid{0};                                  ///< Target process id, 0 when unused.
    char name[k_recovery_name_length]{};                   ///< NUL-terminated process name.
    char function_group[k_recovery_name_length]{};         ///< NUL-terminated Function Group.

    /// @brief Cold-path diagnostic rendering of the request.
    ///
    /// @return Text naming the action, pid, name, and function group.
    /// @note Mandate 9 (universal `to_string`); allocates, so cold path only.
    ///       Spec `phm/recovery`, requirement "Local process recovery".
    [[nodiscard]] std::string to_string() const {
        return std::format("RecoveryRequest(action={}, pid={}, name={}, function_group={})",
                           nexigen::phm::to_string(action), pid, std::string(name),
                           std::string(function_group));
    }
};

/// @brief The payload contract: plain bytes, safe for zero-copy transport.
///
/// @note Spec `phm/recovery`, requirement "Local process recovery".
static_assert(com::concepts::Message<RecoveryRequest>);

/// @brief Free helper mirroring `RecoveryRequest::to_string()` (mandate 9).
///
/// @param request Request to render.
/// @return The request's diagnostic text.
/// @note Mandate 9; allocates, so cold path only.
[[nodiscard]] inline std::string to_string(const RecoveryRequest& request) {
    return request.to_string();
}

} // namespace nexigen::phm

/// @brief Formats a `RecoveryRequest` as its diagnostic string.
///
/// Delegates to the member `to_string`, so `std::format("{}", request)` equals
/// the member output.
///
/// @note Mandate 11 (universal `std::formatter`); cold path only. Spec
///       `phm/recovery`, requirement "Local process recovery".
template <>
struct std::formatter<nexigen::phm::RecoveryRequest> : std::formatter<std::string_view> {
    /// @brief Write the request's diagnostic text into the format context.
    ///
    /// @tparam Ctx `std::format` context type, for example
    ///         `std::format_context`.
    /// @param request Request to format.
    /// @param ctx Output context supplied by `std::format`.
    /// @return Iterator one past the last written character.
    template <class Ctx> auto format(const nexigen::phm::RecoveryRequest& request, Ctx& ctx) const {
        const std::string text = request.to_string();
        return std::formatter<std::string_view>::format(text, ctx);
    }
};
