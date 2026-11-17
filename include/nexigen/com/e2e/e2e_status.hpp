// nexigen::com — E2E check result vocabulary (change add-e2e-protection).
#pragma once

#include <array>
#include <cstdint>
#include <format>
#include <string_view>

#include <nlohmann/json.hpp>

#include <nexigen/core/enum_traits.hpp>
#include <nexigen/core/serialization_mandate.hpp>

namespace nexigen::com {

/// @brief Classification of one received E2E message.
///
/// @note Spec `com/e2e`, requirement "Receiver supervision".
enum class E2eStatus : std::uint8_t {
    Ok,            ///< Checksum, data id, length, and sequence are valid.
    Repeated,      ///< Same counter as the last accepted message.
    WrongSequence, ///< Gap above the configured maximum, or a counter going backwards.
    Error,         ///< Length, data id, or checksum failure; sequence state untouched.
};

/// @brief Canonical name of an E2E status.
///
/// @param value Status to render.
/// @return The canonical enumerator name; allocation-free and `constexpr`.
/// @note Spec `com/e2e`, requirement "Receiver supervision"; mandate 9.
[[nodiscard]] constexpr std::string_view to_string(E2eStatus value) noexcept {
    return core::enum_name(value);
}

/// @brief Parse a canonical E2E status name.
///
/// @param name Candidate canonical name, as produced by `to_string`.
/// @param out Receives the parsed status; left UNCHANGED when parsing fails.
/// @return `true` when `name` matched an enumerator, `false` otherwise.
/// @note Spec `com/e2e`, requirement "Receiver supervision"; mandate 9.
[[nodiscard]] constexpr bool from_string(std::string_view name, E2eStatus& out) noexcept {
    return core::enum_value(name, out);
}

/// @brief ADL JSON writer for an E2E status.
///
/// @param j Output JSON value, replaced by the canonical name `string`.
/// @param value Status to serialize.
/// @note Spec `com/e2e`, requirement "Receiver supervision"; mandate 10.
inline void to_json(nlohmann::json& j, const E2eStatus value) {
    core::mandate::enum_to_json(j, value);
}

/// @brief ADL JSON reader for an E2E status (non-throwing).
///
/// @param j Input JSON value; a non-string or unknown name leaves `out`
///        unchanged.
/// @param out Receives the parsed status on success.
/// @note Spec `com/e2e`, requirement "Receiver supervision"; mandate 10.
inline void from_json(const nlohmann::json& j, E2eStatus& out) noexcept {
    core::mandate::enum_from_json(j, out);
}

} // namespace nexigen::com

namespace nexigen::core {

/// @brief `E2eStatus` vocabulary table.
template <> struct EnumTraits<nexigen::com::E2eStatus> {
    /// @brief One value/name pair of the vocabulary table.
    struct Entry {
        nexigen::com::E2eStatus value; ///< Enumerator.
        std::string_view name;         ///< Canonical name.
    };
    /// @brief Canonical table; exactly one entry per enumerator.
    static constexpr std::array<Entry, 4> entries{{
        {nexigen::com::E2eStatus::Ok, "Ok"},
        {nexigen::com::E2eStatus::Repeated, "Repeated"},
        {nexigen::com::E2eStatus::WrongSequence, "WrongSequence"},
        {nexigen::com::E2eStatus::Error, "Error"},
    }};
};

} // namespace nexigen::core

/// @brief `std::formatter` for an E2E status (project mandate 11).
template <> struct std::formatter<nexigen::com::E2eStatus> : std::formatter<std::string_view> {
    /// @brief Write the canonical status name into the format context.
    /// @tparam Ctx `std::format` context type.
    /// @param value Status to format.
    /// @param ctx Output context supplied by `std::format`.
    /// @return Iterator one past the last written character.
    /// @note Spec `com/e2e`, requirement "Receiver supervision".
    template <class Ctx> auto format(const nexigen::com::E2eStatus value, Ctx& ctx) const {
        return std::formatter<std::string_view>::format(nexigen::com::to_string(value), ctx);
    }
};
