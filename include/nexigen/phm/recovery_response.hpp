// nexigen::phm — recovery response payload (change complete-phm-safety-loop;
// spec phm/recovery "Local process recovery" / "Recovery response carries a
// rejection reason").
//
// The Execution Manager's answer to one recovery request. It means "admitted",
// not "restarted": the restart itself happens afterwards and reports its
// outcome through process lifecycle events. Trivially copyable, so it travels
// zero-copy.
#pragma once

#include <cstdint>
#include <format>
#include <string>

#include <nlohmann/json.hpp>

#include <nexigen/com/concepts/message_concept.hpp>
#include <nexigen/core/detail/json_field.hpp>
#include <nexigen/phm/recovery_outcome.hpp>
#include <nexigen/phm/recovery_reason.hpp>

namespace nexigen::phm {

/// @brief The EM's answer to one recovery request.
///
/// Echoes the request's correlation id and reports whether the restart was
/// admitted, is still being worked on, or was refused and why.
///
/// @note Spec `phm/recovery`, requirement "Local process recovery".
struct RecoveryResponse {
    std::uint64_t request_id{0};                        ///< Echoed request correlation id.
    RecoveryOutcome outcome{RecoveryOutcome::Accepted}; ///< Admission outcome.
    RecoveryReason reason{RecoveryReason::None};        ///< Why a rejection happened.

    /// @brief Cold-path diagnostic reporting the acknowledgement.
    ///
    /// @return Text of the form `RecoveryResponse(request_id=..., outcome=...,
    ///         reason=...)`.
    /// @note Mandate 9 (universal `to_string`); allocates, so cold path only.
    ///       Spec `phm/recovery`, requirement "Local process recovery".
    [[nodiscard]] std::string to_string() const {
        return std::format("RecoveryResponse(request_id={}, outcome={}, reason={})", request_id,
                           nexigen::phm::to_string(outcome), nexigen::phm::to_string(reason));
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

/// @brief ADL JSON writer for a RecoveryResponse.
///
/// @param j Destination JSON value; overwritten with an object.
/// @param value Value to encode.
/// @note Mandate 10 (ADL JSON pair); cold path only. Spec `phm/recovery`,
///       requirement "Recovery outcome vocabulary".
inline void to_json(nlohmann::json& j, const RecoveryResponse& value) {
    j = nlohmann::json{{"request_id", value.request_id},
                       {"outcome", nexigen::phm::to_string(value.outcome)},
                       {"reason", nexigen::phm::to_string(value.reason)}};
}

/// @brief ADL JSON reader for a RecoveryResponse.
///
/// Strictly non-throwing: a non-object or a malformed field leaves `value`
/// unchanged.
///
/// @param j Source JSON value; a non-object is ignored.
/// @param value Receives the parsed fields; unchanged on malformed input.
/// @note Mandate 10; validated under `JSON_NOEXCEPTION=1`. Spec `phm/recovery`,
///       requirement "Recovery outcome vocabulary".
inline void from_json(const nlohmann::json& j, RecoveryResponse& value) noexcept {
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
