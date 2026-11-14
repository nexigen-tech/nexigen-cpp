// nexigen::phm — EM execution lifecycle observed by the supervisor
// (change em-phm-lifecycle-events; spec phm/supervision).
#pragma once

#include <array>
#include <cstdint>
#include <format>
#include <string_view>

#include <nlohmann/json.hpp>

#include <nexigen/core/enum_traits.hpp>
#include <nexigen/core/serialization_mandate.hpp>

namespace nexigen::phm {

/// @brief EM lifecycle state of one supervised process.
///
/// `Unknown` is the pre-change state: supervision follows checkpoints and the
/// alive tick as before. `Running` arms supervision, while `Terminating` and
/// `Terminated` suspend it so a deliberate EM stop cannot become an `Expired`
/// fault.
enum class EmLifecycleState : std::uint8_t {
    Unknown,
    Running,
    Terminating,
    Terminated,
};

/// @brief Canonical name of an EM lifecycle state.
[[nodiscard]] constexpr std::string_view to_string(EmLifecycleState value) noexcept {
    return core::enum_name(value);
}

/// @brief Parse a canonical EM lifecycle state name.
[[nodiscard]] constexpr bool from_string(std::string_view name, EmLifecycleState& out) noexcept {
    return core::enum_value(name, out);
}

/// @brief ADL JSON writer for the EM lifecycle state.
inline void to_json(nlohmann::json& j, const EmLifecycleState value) {
    core::mandate::enum_to_json(j, value);
}

/// @brief ADL JSON reader for the EM lifecycle state (non-throwing).
inline void from_json(const nlohmann::json& j, EmLifecycleState& out) noexcept {
    core::mandate::enum_from_json(j, out);
}

} // namespace nexigen::phm

// Vocabulary table for core::enum_name/enum_value.
namespace nexigen::core {

/// @brief `EmLifecycleState` vocabulary table.
template <> struct EnumTraits<nexigen::phm::EmLifecycleState> {
    struct Entry {
        nexigen::phm::EmLifecycleState value;
        std::string_view name;
    };
    static constexpr std::array<Entry, 4> entries{{
        {nexigen::phm::EmLifecycleState::Unknown, "Unknown"},
        {nexigen::phm::EmLifecycleState::Running, "Running"},
        {nexigen::phm::EmLifecycleState::Terminating, "Terminating"},
        {nexigen::phm::EmLifecycleState::Terminated, "Terminated"},
    }};
};

} // namespace nexigen::core

/// @brief `std::formatter` for the EM lifecycle state (project mandate 11).
template <>
struct std::formatter<nexigen::phm::EmLifecycleState> : std::formatter<std::string_view> {
    template <class Ctx> auto format(const nexigen::phm::EmLifecycleState value, Ctx& ctx) const {
        return std::formatter<std::string_view>::format(nexigen::phm::to_string(value), ctx);
    }
};
