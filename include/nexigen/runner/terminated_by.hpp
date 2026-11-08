// nexigen::runner — TerminatedBy enum.
#pragma once
#include <array>
#include <cstdint>
#include <string_view>

#include <nlohmann/json.hpp>

#include <nexigen/core/enum_traits.hpp>
namespace nexigen::runner {

enum class TerminatedBy : std::uint8_t {
    Self,    ///< The child exited on its own.
    Runner,  ///< The runner/stop command sent the terminating signal.
    Unknown, ///< No attribution is available.
};

} // namespace nexigen::runner

namespace nexigen::runner {

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

} // namespace nexigen::runner

// Vocabulary table for core::enum_name/enum_value (change refactor-enum-traits-helper).
namespace nexigen::core {

template <> struct EnumTraits<nexigen::runner::TerminatedBy> {
    struct Entry {
        nexigen::runner::TerminatedBy value;
        std::string_view name;
    };
    static constexpr std::array<Entry, 3> entries{{
        {nexigen::runner::TerminatedBy::Self, "self"},
        {nexigen::runner::TerminatedBy::Runner, "runner"},
        {nexigen::runner::TerminatedBy::Unknown, "unknown"},
    }};
};

} // namespace nexigen::core
