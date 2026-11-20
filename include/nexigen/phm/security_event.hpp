// nexigen::phm — security event sample (change checkpoint-identity).
#pragma once

#include <cstddef>
#include <cstdint>
#include <format>
#include <string>
#include <string_view>

#include <nexigen/core/concepts/message_concept.hpp>
#include <nexigen/phm/checkpoint_claim.hpp>
#include <nexigen/phm/security_event_kind.hpp>

namespace nexigen::phm {

/// @brief One security finding published on `Nexigen/PHM/Security`.
///
/// A fixed-size message: the PHM daemon fills it when a claim is rejected and
/// wakes `Nexigen/PHM/SecurityEvent`. It carries what the daemon observed — the
/// trigger, the claimed process name, and the claimed pid/uid — so an operator
/// or a future IAM consumer can correlate the finding without parsing logs.
///
/// The process name is the (possibly truncated) claim name, not a kernel-attested
/// identity; see the `RS_PHM_00118` limitation.
///
/// @note Spec `phm/identity`, requirement "Security event channel".
struct SecurityEvent {
    SecurityEventKind kind{SecurityEventKind::ClaimRejected}; ///< What triggered the event.
    char process[k_claim_name_length]{}; ///< Claimed process name; may be full and unterminated.
    std::uint32_t pid{0};                ///< Claimed pid.
    std::uint32_t uid{0};                ///< Claimed uid.

    /// @brief Cold-path diagnostic rendering.
    ///
    /// @return Text of the form
    ///         `SecurityEvent(kind=<name>, process=<bounded name>, pid=<pid>,
    ///         uid=<uid>)`.
    /// @note Mandate 9 (universal `to_string`); allocates, so cold path only.
    ///       The name is read through `bounded_name`, so a full buffer cannot
    ///       overrun the sample.
    [[nodiscard]] std::string to_string() const {
        return std::format("SecurityEvent(kind={}, process={}, pid={}, uid={})",
                           nexigen::phm::to_string(kind), bounded_name(process, sizeof(process)),
                           pid, uid);
    }
};

static_assert(core::concepts::Message<SecurityEvent>);

/// @brief Free helper mirroring `SecurityEvent::to_string()` (mandate 9).
///
/// @param event Event to render.
/// @return The member diagnostic text.
/// @note Mandate 9; allocates, so cold path only. Spec `phm/identity`,
///       requirement "Security event channel".
[[nodiscard]] inline std::string to_string(const SecurityEvent& event) {
    return event.to_string();
}

} // namespace nexigen::phm

/// @brief `std::formatter` for a security event (project mandate 11).
///
/// Makes `std::format("{}", event)` equal `to_string(event)`.
///
/// @note Mandate 11 (universal `std::formatter`); spec `phm/identity`,
///       requirement "Security event channel".
template <> struct std::formatter<nexigen::phm::SecurityEvent> : std::formatter<std::string_view> {
    /// @brief Write the event diagnostic into the format context.
    ///
    /// @tparam Ctx `std::format` context type, for example
    ///         `std::format_context`.
    /// @param event Event to format.
    /// @param ctx Output context supplied by `std::format`.
    /// @return Iterator one past the last written character.
    /// @note Mandate 11; spec `phm/identity`, requirement "Security event
    ///       channel".
    template <class Ctx> auto format(const nexigen::phm::SecurityEvent& event, Ctx& ctx) const {
        return std::formatter<std::string_view>::format(event.to_string(), ctx);
    }
};
