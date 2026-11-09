// nexigen::exec — AppState enum.
#pragma once
#include <array>
#include <cstdint>
#include <string_view>

#include <nlohmann/json.hpp>

#include <nexigen/core/enum_traits.hpp>
namespace nexigen::exec {

enum class AppState : std::uint8_t {
    Running,
    Restarting,
    Exited,
    Stopped,
    Failed,
    Unknown,
};

} // namespace nexigen::exec

// Vocabulary table for core::enum_name/enum_value (change refactor-enum-traits-helper).
namespace nexigen::core {

template <> struct EnumTraits<nexigen::exec::AppState> {
    struct Entry {
        nexigen::exec::AppState value;
        std::string_view name;
    };
    static constexpr std::array<Entry, 6> entries{{
        {nexigen::exec::AppState::Running, "running"},
        {nexigen::exec::AppState::Restarting, "restarting"},
        {nexigen::exec::AppState::Exited, "exited"},
        {nexigen::exec::AppState::Stopped, "stopped"},
        {nexigen::exec::AppState::Failed, "failed"},
        {nexigen::exec::AppState::Unknown, "unknown"},
    }};
    static constexpr std::string_view fallback = "unknown";
};

} // namespace nexigen::core

namespace nexigen::exec {

[[nodiscard]] constexpr std::string_view to_string(AppState value) noexcept {
    return core::enum_name(value);
}

[[nodiscard]] constexpr bool from_string(std::string_view name, AppState& out) noexcept {
    return core::enum_value(name, out);
}

void to_json(nlohmann::json& j, AppState value);
void from_json(const nlohmann::json& j, AppState& value) noexcept;

} // namespace nexigen::exec
