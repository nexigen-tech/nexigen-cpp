// nexigen::phm — recovery rejection-reason vocabulary (change
// complete-phm-safety-loop; spec phm/recovery "Recovery outcome vocabulary").
//
// Why a recovery addressee did not accept a request. Meaningful only for
// `RecoveryOutcome::Rejected`; `None` accompanies every accepted outcome. Own
// enum with the full project conversion set.
#pragma once

#include <array>
#include <cstdint>
#include <string_view>

#include <nlohmann/json.hpp>

#include <nexigen/core/enum_traits.hpp>

namespace nexigen::phm {

/// @brief Cause of a recovery rejection.
///
/// Shared by the Execution Manager and the State Manager answers so one
/// diagnostic vocabulary covers both hops of the safety loop.
///
/// @note Spec `phm/recovery`, requirement "Recovery outcome vocabulary".
///       Mandates 9/10/11 cover the string, JSON, and formatter surfaces.
enum class RecoveryReason : std::uint8_t {
    None,              ///< No rejection; the outcome was accepted.
    UnknownProcess,    ///< The addressee does not know the named process.
    UnknownGroup,      ///< The addressee does not know the named group.
    NotOperational,    ///< The addressee is not in a state that can act.
    AlreadyRestarting, ///< The process is already being restarted.
    InvalidAction,     ///< The addressee does not serve this action.
    NoSafeState,       ///< Neither SafeState nor DegradedMode is declared.
    EmUnavailable,     ///< No Execution Manager channel is open.
    EmRejected,        ///< The Execution Manager refused the transition.
    SystemError,       ///< An unexpected failure prevented the action.
};

/// @brief Canonical name of a rejection reason.
///
/// @param value Reason to render; an out-of-range cast renders `Unknown`.
/// @return The canonical enumerator name.
/// @note Mandate 9 (universal `to_string`); allocation-free and `constexpr`.
///       Spec `phm/recovery`, requirement "Recovery outcome vocabulary".
[[nodiscard]] constexpr std::string_view to_string(RecoveryReason value) noexcept {
    return core::enum_name(value);
}

/// @brief Parse a canonical rejection-reason name.
///
/// @param name Candidate canonical name, as produced by `to_string`.
/// @param out Receives the parsed value; left UNCHANGED when parsing fails.
/// @return `true` when `name` matched an enumerator, `false` otherwise.
/// @note Mandate 9; allocation-free and `constexpr`. Spec `phm/recovery`,
///       requirement "Recovery outcome vocabulary".
[[nodiscard]] constexpr bool from_string(std::string_view name, RecoveryReason& out) noexcept {
    return core::enum_value(name, out);
}

/// @brief ADL JSON writer for a rejection reason.
///
/// The encoded value is exactly `to_string(value)`.
///
/// @param j Destination JSON value; overwritten.
/// @param value Reason to encode.
/// @note Mandate 10 (ADL JSON pair); cold path only. Spec `phm/recovery`,
///       requirement "Recovery outcome vocabulary".
inline void to_json(nlohmann::json& j, const RecoveryReason value) {
    core::mandate::enum_to_json(j, value);
}

/// @brief ADL JSON reader for a rejection reason.
///
/// Strictly non-throwing: a malformed or unknown name leaves `out` unchanged.
///
/// @param j Source JSON value.
/// @param out Receives the parsed value; left UNCHANGED on failure.
/// @note Mandate 10; validated under `JSON_NOEXCEPTION=1`. Spec `phm/recovery`,
///       requirement "Recovery outcome vocabulary".
inline void from_json(const nlohmann::json& j, RecoveryReason& out) noexcept {
    core::mandate::enum_from_json(j, out);
}

} // namespace nexigen::phm

namespace nexigen::core {

/// @brief Vocabulary table binding `phm::RecoveryReason` to its canonical
///        names.
///
/// Consumed by `core::enum_name`/`core::enum_value`; entry order is the
/// canonical round-trip order.
///
/// @note Mandate 9. Spec `phm/recovery`, requirement "Recovery outcome
///       vocabulary".
template <> struct EnumTraits<nexigen::phm::RecoveryReason> {
    /// @brief One value/name pair of the vocabulary table.
    struct Entry {
        nexigen::phm::RecoveryReason value; ///< Enumerator.
        std::string_view name;              ///< Canonical enumerator name.
    };
    /// @brief Canonical table; exactly one entry per enumerator.
    static constexpr std::array<Entry, 10> entries{{
        {nexigen::phm::RecoveryReason::None, "None"},
        {nexigen::phm::RecoveryReason::UnknownProcess, "UnknownProcess"},
        {nexigen::phm::RecoveryReason::UnknownGroup, "UnknownGroup"},
        {nexigen::phm::RecoveryReason::NotOperational, "NotOperational"},
        {nexigen::phm::RecoveryReason::AlreadyRestarting, "AlreadyRestarting"},
        {nexigen::phm::RecoveryReason::InvalidAction, "InvalidAction"},
        {nexigen::phm::RecoveryReason::NoSafeState, "NoSafeState"},
        {nexigen::phm::RecoveryReason::EmUnavailable, "EmUnavailable"},
        {nexigen::phm::RecoveryReason::EmRejected, "EmRejected"},
        {nexigen::phm::RecoveryReason::SystemError, "SystemError"},
    }};
};

} // namespace nexigen::core
