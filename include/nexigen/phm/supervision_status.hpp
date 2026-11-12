// nexigen::phm — supervision status machine (change add-phm; spec
// phm/supervision).
//
// The per-process status the supervision engine derives from Alive, Logical,
// and Deadline results. Own enum with the full project conversion set.
#pragma once

#include <array>
#include <cstdint>
#include <string_view>

#include <nlohmann/json.hpp>

#include <nexigen/core/enum_traits.hpp>

namespace nexigen::phm {

/// @brief Aggregated supervision status of one process.
///
/// The state machine derived from Alive, Logical, and Deadline results: a
/// failure within `failed_supervision_cycles_tolerance` yields `Failed`, a
/// failure streak beyond it yields `Expired`, and a clean cycle returns to
/// `Ok`. `Stopped` marks a deliberate shutdown. A plain `std::uint8_t` enum
/// with the project conversion set.
///
/// @note Spec `phm/supervision`, requirement "Status aggregation". Mandates
///       9/10/11 cover `to_string`, the ADL JSON pair, and the generic
///       `std::formatter`.
enum class SupervisionStatus : std::uint8_t {
    Ok,      ///< All checks pass.
    Failed,  ///< A single failure within the tolerance.
    Expired, ///< Failures exceeded the tolerance; recovery is triggered.
    Stopped, ///< The process stopped deliberately.
};

/// @brief Canonical name of a supervision status.
///
/// @param value Status to render; an out-of-range cast renders `Unknown`.
/// @return The canonical enumerator name.
/// @note Mandate 9 (universal `to_string`); allocation-free and `constexpr`.
///       Spec `phm/supervision`, requirement "Status aggregation".
[[nodiscard]] constexpr std::string_view to_string(SupervisionStatus value) noexcept {
    return core::enum_name(value);
}

/// @brief Parse a canonical supervision-status name.
///
/// @param name Candidate canonical name, as produced by `to_string`.
/// @param out Receives the parsed value; left UNCHANGED when parsing fails.
/// @return `true` when `name` matched an enumerator, `false` otherwise.
/// @note Mandate 9; allocation-free and `constexpr`. Spec `phm/supervision`,
///       requirement "Status aggregation".
[[nodiscard]] constexpr bool from_string(std::string_view name, SupervisionStatus& out) noexcept {
    return core::enum_value(name, out);
}

/// @brief ADL JSON writer for a supervision status.
///
/// The encoded value is exactly `to_string(value)`.
///
/// @param j Destination JSON value; overwritten.
/// @param value Status to encode.
/// @note Mandate 10 (ADL JSON pair); cold path only. Spec `phm/supervision`,
///       requirement "Status aggregation".
inline void to_json(nlohmann::json& j, const SupervisionStatus value) {
    core::mandate::enum_to_json(j, value);
}

/// @brief ADL JSON reader for a supervision status.
///
/// Strictly non-throwing: a malformed or unknown name leaves `out` unchanged.
///
/// @param j Source JSON value.
/// @param out Receives the parsed value; left UNCHANGED on failure.
/// @note Mandate 10; validated under `JSON_NOEXCEPTION=1`. Spec
///       `phm/supervision`, requirement "Status aggregation".
inline void from_json(const nlohmann::json& j, SupervisionStatus& out) noexcept {
    core::mandate::enum_from_json(j, out);
}

} // namespace nexigen::phm

namespace nexigen::core {

/// @brief Vocabulary table binding `phm::SupervisionStatus` to its canonical
///        names.
///
/// Consumed by `core::enum_name`/`core::enum_value`; entry order is the
/// canonical round-trip order.
///
/// @note Mandate 9. Spec `phm/supervision`, requirement "Status aggregation".
template <> struct EnumTraits<nexigen::phm::SupervisionStatus> {
    /// @brief One value/name pair of the vocabulary table.
    struct Entry {
        nexigen::phm::SupervisionStatus value; ///< Enumerator.
        std::string_view name;                 ///< Canonical enumerator name.
    };
    /// @brief Canonical table; exactly one entry per enumerator.
    static constexpr std::array<Entry, 4> entries{{
        {nexigen::phm::SupervisionStatus::Ok, "Ok"},
        {nexigen::phm::SupervisionStatus::Failed, "Failed"},
        {nexigen::phm::SupervisionStatus::Expired, "Expired"},
        {nexigen::phm::SupervisionStatus::Stopped, "Stopped"},
    }};
};

} // namespace nexigen::core
