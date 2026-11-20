// nexigen::phm — State Manager recovery acknowledgement (change
// complete-phm-safety-loop; spec sm/recovery-handler "Recovery acknowledgement
// event").
//
// The SM's answer to one PHM fault notification: the correlation id, whether
// the SM took the failed group over, and why not when it did not. Trivially
// copyable, so it travels zero-copy.
#pragma once

#include <cstdint>
#include <format>
#include <string>

#include <nlohmann/json.hpp>

#include <nexigen/core/concepts/message_concept.hpp>
#include <nexigen/core/detail/json_field.hpp>
#include <nexigen/phm/recovery_outcome.hpp>
#include <nexigen/phm/recovery_reason.hpp>

namespace nexigen::phm {

/// @brief The SM's answer to one recovery notification.
///
/// `Accepted` means the SM committed the group state, published it, and handed
/// the transition to the Execution Manager. `InProgress` means the Execution
/// Manager has not answered yet. `Rejected` names the reason the SM could not
/// take the group over, so the daemon can escalate instead of waiting.
///
/// @note Spec `sm/recovery-handler`, requirement "Recovery acknowledgement
///       event".
struct RecoveryAck {
    std::uint64_t request_id{0};                        ///< Echoed notification correlation id.
    RecoveryOutcome outcome{RecoveryOutcome::Accepted}; ///< Takeover outcome.
    RecoveryReason reason{RecoveryReason::None};        ///< Why a rejection happened.

    /// @brief Cold-path diagnostic reporting the acknowledgement.
    ///
    /// @return Text of the form `RecoveryAck(request_id=..., outcome=...,
    ///         reason=...)`.
    /// @note Mandate 9 (universal `to_string`); allocates, so cold path only.
    ///       Spec `sm/recovery-handler`, requirement "Recovery acknowledgement
    ///       event".
    [[nodiscard]] std::string to_string() const {
        return std::format("RecoveryAck(request_id={}, outcome={}, reason={})", request_id,
                           nexigen::phm::to_string(outcome), nexigen::phm::to_string(reason));
    }
};

/// @brief The payload contract: plain bytes, safe for zero-copy transport.
///
/// @note Spec `sm/recovery-handler`, requirement "Recovery acknowledgement
///       event".
static_assert(core::concepts::Message<RecoveryAck>);

/// @brief Free helper mirroring `RecoveryAck::to_string()` (mandate 9).
///
/// @param ack Acknowledgement to render.
/// @return The acknowledgement's diagnostic text.
/// @note Mandate 9; allocates, so cold path only.
[[nodiscard]] inline std::string to_string(const RecoveryAck& ack) {
    return ack.to_string();
}

/// @brief ADL JSON writer for a RecoveryAck.
///
/// @param j Destination JSON value; overwritten with an object.
/// @param value Value to encode.
/// @note Mandate 10 (ADL JSON pair); cold path only. Spec `phm/recovery`,
///       requirement "Recovery outcome vocabulary".
inline void to_json(nlohmann::json& j, const RecoveryAck& value) {
    j = nlohmann::json{{"request_id", value.request_id},
                       {"outcome", nexigen::phm::to_string(value.outcome)},
                       {"reason", nexigen::phm::to_string(value.reason)}};
}

/// @brief ADL JSON reader for a RecoveryAck.
///
/// Strictly non-throwing: a non-object or a malformed field leaves `value`
/// unchanged.
///
/// @param j Source JSON value; a non-object is ignored.
/// @param value Receives the parsed fields; unchanged on malformed input.
/// @note Mandate 10; validated under `JSON_NOEXCEPTION=1`. Spec `phm/recovery`,
///       requirement "Recovery outcome vocabulary".
inline void from_json(const nlohmann::json& j, RecoveryAck& value) noexcept {
    if (!j.is_object()) {
        return;
    }
    std::string outcome;
    if (!core::detail::read_string(j, "outcome", outcome)) {
        return;
    }
    RecoveryOutcome parsed_outcome = value.outcome;
    if (!from_string(outcome, parsed_outcome)) {
        return;
    }
    std::string reason;
    if (!core::detail::read_string(j, "reason", reason)) {
        return;
    }
    RecoveryReason parsed_reason = value.reason;
    if (!from_string(reason, parsed_reason)) {
        return;
    }
    static_cast<void>(core::detail::read_unsigned(j, "request_id", value.request_id));
    value.outcome = parsed_outcome;
    value.reason = parsed_reason;
}

} // namespace nexigen::phm

/// @brief Formats a `RecoveryAck` as its diagnostic string.
///
/// Delegates to the member `to_string`, so `std::format("{}", ack)` equals the
/// member output.
///
/// @note Mandate 11 (universal `std::formatter`); cold path only. Spec
///       `sm/recovery-handler`, requirement "Recovery acknowledgement event".
template <> struct std::formatter<nexigen::phm::RecoveryAck> : std::formatter<std::string_view> {
    /// @brief Write the acknowledgement's diagnostic text into the format context.
    ///
    /// @tparam Ctx `std::format` context type, for example
    ///         `std::format_context`.
    /// @param ack Acknowledgement to format.
    /// @param ctx Output context supplied by `std::format`.
    /// @return Iterator one past the last written character.
    template <class Ctx> auto format(const nexigen::phm::RecoveryAck& ack, Ctx& ctx) const {
        const std::string text = ack.to_string();
        return std::formatter<std::string_view>::format(text, ctx);
    }
};
