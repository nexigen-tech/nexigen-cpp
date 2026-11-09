// nexigen::runner — SuiteState enum.
#pragma once
#include <array>
#include <cstdint>
#include <string_view>

#include <nlohmann/json.hpp>

#include <nexigen/core/enum_traits.hpp>
namespace nexigen::runner {

enum class SuiteState : std::uint8_t {
    Running,
    Degraded,
    Failed,
    Stopped,
    Unknown,
};

} // namespace nexigen::runner

// Vocabulary table for core::enum_name/enum_value (change refactor-enum-traits-helper).
namespace nexigen::core {

template <> struct EnumTraits<nexigen::runner::SuiteState> {
    struct Entry {
        nexigen::runner::SuiteState value;
        std::string_view name;
    };
    static constexpr std::array<Entry, 5> entries{{
        {nexigen::runner::SuiteState::Running, "running"},
        {nexigen::runner::SuiteState::Degraded, "degraded"},
        {nexigen::runner::SuiteState::Failed, "failed"},
        {nexigen::runner::SuiteState::Stopped, "stopped"},
        {nexigen::runner::SuiteState::Unknown, "unknown"},
    }};
    static constexpr std::string_view fallback = "unknown";
};

} // namespace nexigen::core

namespace nexigen::runner {

[[nodiscard]] constexpr std::string_view to_string(SuiteState value) noexcept {
    return core::enum_name(value);
}

[[nodiscard]] constexpr bool from_string(std::string_view name, SuiteState& out) noexcept {
    return core::enum_value(name, out);
}

void to_json(nlohmann::json& j, SuiteState value);
void from_json(const nlohmann::json& j, SuiteState& value) noexcept;

} // namespace nexigen::runner
