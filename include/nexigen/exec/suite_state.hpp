// nexigen::exec — SuiteState enum.
#pragma once
#include <array>
#include <cstdint>
#include <string_view>

#include <nlohmann/json.hpp>

#include <nexigen/core/enum_traits.hpp>
namespace nexigen::exec {

enum class SuiteState : std::uint8_t {
    Running,
    Degraded,
    Failed,
    Stopped,
    Unknown,
};

} // namespace nexigen::exec

// Vocabulary table for core::enum_name/enum_value (change refactor-enum-traits-helper).
namespace nexigen::core {

template <> struct EnumTraits<nexigen::exec::SuiteState> {
    struct Entry {
        nexigen::exec::SuiteState value;
        std::string_view name;
    };
    static constexpr std::array<Entry, 5> entries{{
        {nexigen::exec::SuiteState::Running, "running"},
        {nexigen::exec::SuiteState::Degraded, "degraded"},
        {nexigen::exec::SuiteState::Failed, "failed"},
        {nexigen::exec::SuiteState::Stopped, "stopped"},
        {nexigen::exec::SuiteState::Unknown, "unknown"},
    }};
    static constexpr std::string_view fallback = "unknown";
};

} // namespace nexigen::core

namespace nexigen::exec {

[[nodiscard]] constexpr std::string_view to_string(SuiteState value) noexcept {
    return core::enum_name(value);
}

[[nodiscard]] constexpr bool from_string(std::string_view name, SuiteState& out) noexcept {
    return core::enum_value(name, out);
}

void to_json(nlohmann::json& j, SuiteState value);
void from_json(const nlohmann::json& j, SuiteState& value) noexcept;

} // namespace nexigen::exec
