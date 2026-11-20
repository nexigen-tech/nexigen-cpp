// nexigen::exec — process lifecycle notifications for PHM
// (change em-phm-lifecycle-events; spec exec/process-lifecycle-events).
//
// The EM publishes these fixed-size PODs when the process table moves a
// process to Running, when a deliberate stop starts, and after the reap. PHM
// uses them to arm and suspend supervision without misreading an intentional
// stop as an Alive failure.
#pragma once

#include <array>
#include <cstdint>
#include <format>
#include <string>
#include <string_view>

#include <nlohmann/json.hpp>

#include <nexigen/core/enum_traits.hpp>
#include <nexigen/core/serialization_mandate.hpp>

namespace nexigen::exec {

/// @brief Length of the application-name and Function Group fields in a
///        lifecycle event.
inline constexpr std::size_t k_process_lifecycle_name_length = 32;

/// @brief Lifecycle state reported to PHM.
enum class ProcessLifecycleState : std::uint8_t {
    Running,     ///< The process confirmed readiness (or needs no report).
    Terminating, ///< The EM started a deliberate stop.
    Terminated,  ///< The process was reaped.
    Failed,      ///< The restart budget is spent; no further launch will happen.
};

/// @brief Canonical name of a lifecycle state.
[[nodiscard]] constexpr std::string_view to_string(ProcessLifecycleState value) noexcept {
    return core::enum_name(value);
}

/// @brief Parse a canonical lifecycle state name.
[[nodiscard]] constexpr bool from_string(std::string_view name,
                                         ProcessLifecycleState& out) noexcept {
    return core::enum_value(name, out);
}

/// @brief ADL JSON writer for the lifecycle state.
inline void to_json(nlohmann::json& j, const ProcessLifecycleState value) {
    core::mandate::enum_to_json(j, value);
}

/// @brief ADL JSON reader for the lifecycle state (non-throwing).
inline void from_json(const nlohmann::json& j, ProcessLifecycleState& out) noexcept {
    core::mandate::enum_from_json(j, out);
}

/// @brief One process lifecycle notification.
///
/// Fixed-size and trivially copyable, so it is a valid `core::concepts::Message`
/// without serialization or heap allocation.
struct ProcessLifecycleEvent {
    char app[k_process_lifecycle_name_length]{}; ///< NUL-terminated application name.
    /// NUL-terminated Function Group the application belongs to; empty when the
    /// publisher could not resolve it.
    char function_group[k_process_lifecycle_name_length]{};
    ProcessLifecycleState state{ProcessLifecycleState::Running}; ///< Reported state.

    /// @brief Cold-path diagnostic rendering.
    [[nodiscard]] std::string to_string() const;
};

/// @brief Free diagnostic helper (project mandate 9).
[[nodiscard]] inline std::string to_string(const ProcessLifecycleEvent& event) {
    return event.to_string();
}

/// @brief Serialize one lifecycle event.
void to_json(nlohmann::json& j, const ProcessLifecycleEvent& event);

/// @brief Parse one lifecycle event without throwing under `JSON_NOEXCEPTION=1`.
void from_json(const nlohmann::json& j, ProcessLifecycleEvent& event) noexcept;

} // namespace nexigen::exec

// Vocabulary table for core::enum_name/enum_value.
namespace nexigen::core {

/// @brief `ProcessLifecycleState` vocabulary table.
template <> struct EnumTraits<nexigen::exec::ProcessLifecycleState> {
    struct Entry {
        nexigen::exec::ProcessLifecycleState value;
        std::string_view name;
    };
    static constexpr std::array<Entry, 4> entries{{
        {nexigen::exec::ProcessLifecycleState::Running, "Running"},
        {nexigen::exec::ProcessLifecycleState::Terminating, "Terminating"},
        {nexigen::exec::ProcessLifecycleState::Terminated, "Terminated"},
        {nexigen::exec::ProcessLifecycleState::Failed, "Failed"},
    }};
};

} // namespace nexigen::core

/// @brief `std::formatter` for the lifecycle state (project mandate 11).
template <>
struct std::formatter<nexigen::exec::ProcessLifecycleState> : std::formatter<std::string_view> {
    template <class Ctx>
    auto format(const nexigen::exec::ProcessLifecycleState value, Ctx& ctx) const {
        return std::formatter<std::string_view>::format(nexigen::exec::to_string(value), ctx);
    }
};

/// @brief `std::formatter` for a lifecycle event (project mandate 11).
template <>
struct std::formatter<nexigen::exec::ProcessLifecycleEvent> : std::formatter<std::string_view> {
    template <class Ctx>
    auto format(const nexigen::exec::ProcessLifecycleEvent& event, Ctx& ctx) const {
        return std::formatter<std::string_view>::format(nexigen::exec::to_string(event), ctx);
    }
};
