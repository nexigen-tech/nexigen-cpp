// nexigen::phm — recovery outcome vocabulary (change
// complete-phm-safety-loop; spec phm/recovery "Recovery outcome vocabulary").
//
// The outcome half of a recovery answer: whether the addressee admitted the
// request, is still working on it, or refused it. Own enum with the full
// project conversion set.
#pragma once

#include <array>
#include <cstdint>
#include <string_view>

#include <nlohmann/json.hpp>

#include <nexigen/core/enum_traits.hpp>

namespace nexigen::phm {

/// @brief Outcome reported by a recovery addressee.
///
/// `Accepted` means the addressee took responsibility and the request no longer
/// needs a retry. `InProgress` means it is still working and the caller keeps
/// waiting inside the same budget. `Rejected` means it refused and the caller
/// escalates, using the paired `RecoveryReason` to say why.
///
/// @note Spec `phm/recovery`, requirement "Recovery outcome vocabulary".
///       Mandates 9/10/11 cover the string, JSON, and formatter surfaces.
enum class RecoveryOutcome : std::uint8_t {
    Accepted,   ///< Admitted; responsibility transferred.
    InProgress, ///< Admitted but not yet committed; keep waiting.
    Rejected,   ///< Refused; escalate with the paired reason.
};

/// @brief Canonical name of a recovery outcome.
///
/// @param value Outcome to render; an out-of-range cast renders `Unknown`.
/// @return The canonical enumerator name.
/// @note Mandate 9 (universal `to_string`); allocation-free and `constexpr`.
///       Spec `phm/recovery`, requirement "Recovery outcome vocabulary".
[[nodiscard]] constexpr std::string_view to_string(RecoveryOutcome value) noexcept {
    return core::enum_name(value);
}

/// @brief Parse a canonical recovery-outcome name.
///
/// @param name Candidate canonical name, as produced by `to_string`.
/// @param out Receives the parsed value; left UNCHANGED when parsing fails.
/// @return `true` when `name` matched an enumerator, `false` otherwise.
/// @note Mandate 9; allocation-free and `constexpr`. Spec `phm/recovery`,
///       requirement "Recovery outcome vocabulary".
[[nodiscard]] constexpr bool from_string(std::string_view name, RecoveryOutcome& out) noexcept {
    return core::enum_value(name, out);
}

/// @brief ADL JSON writer for a recovery outcome.
///
/// The encoded value is exactly `to_string(value)`.
///
/// @param j Destination JSON value; overwritten.
/// @param value Outcome to encode.
/// @note Mandate 10 (ADL JSON pair); cold path only. Spec `phm/recovery`,
///       requirement "Recovery outcome vocabulary".
inline void to_json(nlohmann::json& j, const RecoveryOutcome value) {
    core::mandate::enum_to_json(j, value);
}

/// @brief ADL JSON reader for a recovery outcome.
///
/// Strictly non-throwing: a malformed or unknown name leaves `out` unchanged.
///
/// @param j Source JSON value.
/// @param out Receives the parsed value; left UNCHANGED on failure.
/// @note Mandate 10; validated under `JSON_NOEXCEPTION=1`. Spec `phm/recovery`,
///       requirement "Recovery outcome vocabulary".
inline void from_json(const nlohmann::json& j, RecoveryOutcome& out) noexcept {
    core::mandate::enum_from_json(j, out);
}

} // namespace nexigen::phm

namespace nexigen::core {

/// @brief Vocabulary table binding `phm::RecoveryOutcome` to its canonical
///        names.
///
/// Consumed by `core::enum_name`/`core::enum_value`; entry order is the
/// canonical round-trip order.
///
/// @note Mandate 9. Spec `phm/recovery`, requirement "Recovery outcome
///       vocabulary".
template <> struct EnumTraits<nexigen::phm::RecoveryOutcome> {
    /// @brief One value/name pair of the vocabulary table.
    struct Entry {
        nexigen::phm::RecoveryOutcome value; ///< Enumerator.
        std::string_view name;               ///< Canonical enumerator name.
    };
    /// @brief Canonical table; exactly one entry per enumerator.
    static constexpr std::array<Entry, 3> entries{{
        {nexigen::phm::RecoveryOutcome::Accepted, "Accepted"},
        {nexigen::phm::RecoveryOutcome::InProgress, "InProgress"},
        {nexigen::phm::RecoveryOutcome::Rejected, "Rejected"},
    }};
};

} // namespace nexigen::core
