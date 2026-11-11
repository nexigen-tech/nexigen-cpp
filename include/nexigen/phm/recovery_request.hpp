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

/// Maximum length of the process-name / function-group buffers.
inline constexpr std::size_t k_recovery_name_length = 32;

/// One recovery command: an action plus its target identity.
struct RecoveryRequest {
    RecoveryAction action{RecoveryAction::RestartProcess};
    std::uint32_t pid{0};
    char name[k_recovery_name_length]{};
    char function_group[k_recovery_name_length]{};

    /// Cold-path diagnostic (mandate 9).
    [[nodiscard]] std::string to_string() const {
        return std::format("RecoveryRequest(action={}, pid={}, name={}, function_group={})",
                           nexigen::phm::to_string(action), pid, std::string(name),
                           std::string(function_group));
    }
};

/// The payload contract: plain bytes, safe for zero-copy transport.
static_assert(com::concepts::Message<RecoveryRequest>);

/// Free helper (mandate 9): mirrors the member.
[[nodiscard]] inline std::string to_string(const RecoveryRequest& request) {
    return request.to_string();
}

} // namespace nexigen::phm

/// The request formats as its diagnostic string (mandate 11).
template <>
struct std::formatter<nexigen::phm::RecoveryRequest> : std::formatter<std::string_view> {
    template <class Ctx> auto format(const nexigen::phm::RecoveryRequest& request, Ctx& ctx) const {
        const std::string text = request.to_string();
        return std::formatter<std::string_view>::format(text, ctx);
    }
};
