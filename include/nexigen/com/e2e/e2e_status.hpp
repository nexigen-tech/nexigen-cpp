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
[[nodiscard]] constexpr std::string_view to_string(E2eStatus value) noexcept {
    return core::enum_name(value);
}

/// @brief Parse a canonical E2E status name.
[[nodiscard]] constexpr bool from_string(std::string_view name, E2eStatus& out) noexcept {
    return core::enum_value(name, out);
}

/// @brief ADL JSON writer for an E2E status.
inline void to_json(nlohmann::json& j, const E2eStatus value) {
    core::mandate::enum_to_json(j, value);
}

/// @brief ADL JSON reader for an E2E status (non-throwing).
inline void from_json(const nlohmann::json& j, E2eStatus& out) noexcept {
    core::mandate::enum_from_json(j, out);
}

} // namespace nexigen::com

namespace nexigen::core {

/// @brief `E2eStatus` vocabulary table.
template <> struct EnumTraits<nexigen::com::E2eStatus> {
    struct Entry {
        nexigen::com::E2eStatus value; ///< Enumerator.
        std::string_view name;         ///< Canonical name.
    };
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
    template <class Ctx> auto format(const nexigen::com::E2eStatus value, Ctx& ctx) const {
        return std::formatter<std::string_view>::format(nexigen::com::to_string(value), ctx);
    }
};
