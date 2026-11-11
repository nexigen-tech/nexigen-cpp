// nexigen::exec — TerminatedBy enum.
#pragma once
#include <array>
#include <cstdint>
#include <string_view>

#include <nlohmann/json.hpp>

#include <nexigen/core/enum_traits.hpp>
namespace nexigen::exec {

enum class TerminatedBy : std::uint8_t {
    Self,             ///< The child exited on its own.
    ExecutionManager, ///< The execution_manager/stop command sent the terminating signal.
    Unknown,          ///< No attribution is available.
};

} // namespace nexigen::exec

// Vocabulary table for core::enum_name/enum_value (change refactor-enum-traits-helper).
namespace nexigen::core {

template <> struct EnumTraits<nexigen::exec::TerminatedBy> {
    struct Entry {
        nexigen::exec::TerminatedBy value;
        std::string_view name;
    };
    static constexpr std::array<Entry, 3> entries{{
        {nexigen::exec::TerminatedBy::Self, "self"},
        {nexigen::exec::TerminatedBy::ExecutionManager, "execution_manager"},
        {nexigen::exec::TerminatedBy::Unknown, "unknown"},
    }};
};

} // namespace nexigen::core

namespace nexigen::exec {

/// Canonical lower-case name used by exit.json.
[[nodiscard]] constexpr std::string_view to_string(TerminatedBy value) noexcept {
    return core::enum_name(value);
}

/// Parse a canonical name; false leaves `out` unchanged.
[[nodiscard]] constexpr bool from_string(std::string_view name, TerminatedBy& out) noexcept {
    return core::enum_value(name, out);
}

void to_json(nlohmann::json& j, TerminatedBy value);
void from_json(const nlohmann::json& j, TerminatedBy& value) noexcept;

} // namespace nexigen::exec
