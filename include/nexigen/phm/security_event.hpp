// nexigen::phm — security event sample (change checkpoint-identity).
#pragma once

#include <cstddef>
#include <cstdint>
#include <format>
#include <string>
#include <string_view>

#include <nexigen/com/concepts/message_concept.hpp>
#include <nexigen/phm/checkpoint_claim.hpp>
#include <nexigen/phm/security_event_kind.hpp>

namespace nexigen::phm {

/// @brief One security finding published on `Nexigen/PHM/Security`.
///
/// @note Spec `phm/identity`, requirement "Security event channel".
struct SecurityEvent {
    SecurityEventKind kind{SecurityEventKind::ClaimRejected}; ///< Trigger.
    char process[k_claim_name_length]{};                      ///< NUL-terminated process name.
    std::uint32_t pid{0};                                     ///< Claimed pid.
    std::uint32_t uid{0};                                     ///< Claimed uid.

    /// @brief Cold-path diagnostic rendering.
    [[nodiscard]] std::string to_string() const {
        return std::format("SecurityEvent(kind={}, process={}, pid={}, uid={})",
                           nexigen::phm::to_string(kind), bounded_name(process, sizeof(process)),
                           pid, uid);
    }
};

static_assert(com::concepts::Message<SecurityEvent>);

/// @brief Free helper mirroring `SecurityEvent::to_string()` (mandate 9).
[[nodiscard]] inline std::string to_string(const SecurityEvent& event) {
    return event.to_string();
}

} // namespace nexigen::phm

/// @brief `std::formatter` for a security event (project mandate 11).
template <> struct std::formatter<nexigen::phm::SecurityEvent> : std::formatter<std::string_view> {
    template <class Ctx> auto format(const nexigen::phm::SecurityEvent& event, Ctx& ctx) const {
        return std::formatter<std::string_view>::format(event.to_string(), ctx);
    }
};
