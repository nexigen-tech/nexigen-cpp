// nexigen::phm — fault kind vocabulary (change add-phm; spec phm/recovery
// "Fault storage").
//
// The supervision domain a recorded violation belongs to. Own enum with the
// full project conversion set.
#pragma once

#include <array>
#include <cstdint>
#include <string_view>

#include <nlohmann/json.hpp>

#include <nexigen/core/enum_traits.hpp>

namespace nexigen::phm {

/// @brief The supervision domain that detected a fault.
///
/// Labels every record in the asynchronous fault log with the check that
/// failed, so an operator can tell aliveness gaps from determinism violations.
/// A plain `std::uint8_t` enum with the project conversion set: canonical
/// `to_string`/`from_string`, ADL `to_json`/`from_json`, and the shared generic
/// `std::formatter`.
///
/// @note Spec `phm/recovery`, requirement "Fault storage": a fault record
///       carries its fault type, timestamp, process id, and checkpoint id.
///       Mandates 9/10/11 cover the string, JSON, and formatter surfaces.
enum class FaultKind : std::uint8_t {
    AliveFailure,    ///< Missed aliveness window (Alive supervision).
    LogicalFailure,  ///< Checkpoint order violated the transition graph (Logical supervision).
    DeadlineFailure, ///< Source->target elapsed outside min/max (Deadline supervision).
};

/// @brief Canonical name of a fault kind.
///
/// @param value Fault kind to render; an out-of-range cast renders `Unknown`.
/// @return The canonical enumerator name (`AliveFailure`, and so on).
/// @note Mandate 9 (universal `to_string`); allocation-free and `constexpr`, so
///       it may be called on a hot path. Spec `phm/recovery`, requirement
///       "Fault storage".
[[nodiscard]] constexpr std::string_view to_string(FaultKind value) noexcept {
    return core::enum_name(value);
}

/// @brief Parse a canonical fault-kind name.
///
/// @param name Candidate canonical name, as produced by `to_string`.
/// @param out Receives the parsed value; left UNCHANGED when parsing fails.
/// @return `true` when `name` matched an enumerator, `false` otherwise.
/// @note Mandate 9; allocation-free and `constexpr`. Spec `phm/recovery`,
///       requirement "Fault storage".
[[nodiscard]] constexpr bool from_string(std::string_view name, FaultKind& out) noexcept {
    return core::enum_value(name, out);
}

/// @brief ADL JSON writer for a fault kind.
///
/// The encoded value is exactly `to_string(value)`.
///
/// @param j Destination JSON value; overwritten.
/// @param value Fault kind to encode.
/// @note Mandate 10 (ADL JSON pair); cold path only. Spec `phm/recovery`,
///       requirement "Fault storage".
inline void to_json(nlohmann::json& j, const FaultKind value) {
    core::mandate::enum_to_json(j, value);
}

/// @brief ADL JSON reader for a fault kind.
///
/// Strictly non-throwing: a malformed or unknown name leaves `out` unchanged.
///
/// @param j Source JSON value.
/// @param out Receives the parsed value; left UNCHANGED on failure.
/// @note Mandate 10; validated under `JSON_NOEXCEPTION=1`. Spec
///       `phm/recovery`, requirement "Fault storage".
inline void from_json(const nlohmann::json& j, FaultKind& out) noexcept {
    core::mandate::enum_from_json(j, out);
}

} // namespace nexigen::phm

namespace nexigen::core {

/// @brief Vocabulary table binding `phm::FaultKind` to its canonical names.
///
/// Consumed by `core::enum_name`/`core::enum_value`; entry order is the
/// canonical round-trip order.
///
/// @note Mandate 9. Spec `phm/recovery`, requirement "Fault storage".
template <> struct EnumTraits<nexigen::phm::FaultKind> {
    /// @brief One value/name pair of the vocabulary table.
    struct Entry {
        nexigen::phm::FaultKind value; ///< Enumerator.
        std::string_view name;         ///< Canonical enumerator name.
    };
    /// @brief Canonical table; exactly one entry per enumerator.
    static constexpr std::array<Entry, 3> entries{{
        {nexigen::phm::FaultKind::AliveFailure, "AliveFailure"},
        {nexigen::phm::FaultKind::LogicalFailure, "LogicalFailure"},
        {nexigen::phm::FaultKind::DeadlineFailure, "DeadlineFailure"},
    }};
};

} // namespace nexigen::core
