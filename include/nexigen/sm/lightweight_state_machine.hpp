// nexigen::sm — lightweight declarative state machine
// (change p3-lightweight-state-machine; spec sm/lightweight-state-machine).
//
// A single-threaded interpreter over a transition table, ordered action lists,
// and an error-recovery table. The action sink keeps it free of COM and makes
// the execution order observable in tests.
#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <format>
#include <string>
#include <string_view>
#include <vector>

#include <nlohmann/json.hpp>

#include <nexigen/core/enum_traits.hpp>
#include <nexigen/core/result.hpp>
#include <nexigen/core/serialization_mandate.hpp>
#include <nexigen/sm/transition_result.hpp>

namespace nexigen::sm {

/// @brief Action kind executed after a transition.
enum class LsmActionType : std::uint8_t {
    None,
    RequestFunctionGroupState,
    PublishState,
};

/// @brief One declarative action.
struct LsmAction {
    LsmActionType type{LsmActionType::None}; ///< Kind of side effect.
    std::string function_group{};            ///< Target Function Group for request kinds.
    std::string target_state{};              ///< Target state name.

    /// @brief Render the action for cold-path diagnostics.
    /// @return Text naming the kind, Function Group, and target state.
    [[nodiscard]] std::string to_string() const;
};

/// @brief One allowed transition plus its ordered action indices.
struct LsmTransition {
    std::string from{};                   ///< Source state (empty for the initial entry).
    std::string to{};                     ///< Destination state.
    std::vector<std::uint32_t> actions{}; ///< Action indices executed in order.

    /// @brief Render the transition for cold-path diagnostics.
    /// @return Text naming the source, destination, and action count.
    [[nodiscard]] std::string to_string() const;
};

/// @brief One error-recovery rule.
struct LsmErrorRecovery {
    std::string from{};                   ///< State whose failure triggers recovery.
    std::string fallback_state{};         ///< State to adopt on recovery.
    std::vector<std::uint32_t> actions{}; ///< Action indices executed first.

    /// @brief Render the recovery rule for cold-path diagnostics.
    /// @return Text naming the source, fallback, and action count.
    [[nodiscard]] std::string to_string() const;
};

/// @brief Configuration of one lightweight state machine.
struct LightweightStateMachineConfig {
    std::string name{};                             ///< Machine name.
    std::string function_group{};                   ///< Owning Function Group.
    std::vector<std::string> states{};              ///< Declared state names.
    std::string initial_state{};                    ///< State adopted before the first request.
    std::vector<LsmAction> actions{};               ///< Action table.
    std::vector<LsmTransition> transitions{};       ///< Allowed transitions and action order.
    std::vector<LsmErrorRecovery> error_recovery{}; ///< Recovery rules.

    /// @brief Render the configuration for cold-path diagnostics.
    /// @return Text naming the machine, states, transitions, and actions.
    [[nodiscard]] std::string to_string() const;

    /// @brief Validate states and action indices.
    /// @return Empty status when the configuration is coherent, otherwise
    ///         `InvalidArgument` describing the first inconsistency.
    [[nodiscard]] core::Status validate() const noexcept;
};

/// @brief Side effects of the interpreter.
class LsmActionSink {
public:
    virtual ~LsmActionSink() = default;
    /// @brief Request a Function Group state change through the EM.
    /// @param group Function Group name.
    /// @param state Target state name.
    /// @param requester Requester identity forwarded to the EM.
    /// @return Empty status, or the transport/arbitration error.
    virtual core::Status request_state(std::string_view group, std::string_view state,
                                       std::string_view requester) = 0;
    /// @brief Publish a group state on the state channel.
    /// @param group Function Group name.
    /// @param state State name to publish.
    /// @return Empty status, or the publish error.
    virtual core::Status publish_state(std::string_view group, std::string_view state) = 0;
};

/// @brief Declarative transition-table interpreter.
class LightweightStateMachine {
public:
    /// @brief Build a machine from a validated configuration.
    ///
    /// @param config Configuration snapshot moved into the machine.
    /// @param sink Action sink; borrowed, must outlive the machine.
    /// @return The machine, or `InvalidArgument` for an invalid configuration.
    [[nodiscard]] static core::Result<LightweightStateMachine>
    create(LightweightStateMachineConfig config, LsmActionSink& sink) noexcept;

    /// @brief Request one target state and run its action list.
    ///
    /// @param target Target state name.
    /// @param requester_id Requester identity passed to the sink.
    /// @return `Success`, `InvalidGraph`, or the action error.
    [[nodiscard]] core::Result<TransitionResult> request(std::string_view target,
                                                         std::string_view requester_id);

    /// @brief Current state.
    /// @return The state adopted by the last successful transition.
    [[nodiscard]] std::string_view current_state() const noexcept { return current_; }

    /// @brief Align the machine with an existing group state.
    ///
    /// @param state State to adopt; must be declared by the configuration.
    /// @return `true` when the state is declared and adopted.
    [[nodiscard]] bool reset_to(std::string_view state) noexcept;

    /// @brief Cold-path diagnostic rendering.
    /// @return Text naming the machine, current state, and configured sizes.
    [[nodiscard]] std::string to_string() const;

private:
    LightweightStateMachine(LightweightStateMachineConfig config, LsmActionSink& sink) noexcept;

    /// @brief Execute one action-index list; stops at the first failure.
    [[nodiscard]] core::Status run_actions(const std::vector<std::uint32_t>& indices,
                                           std::string_view requester_id);

    /// @brief Apply the error-recovery table after a failed action.
    void recover(std::string_view failed_target, std::string_view requester_id);

    LightweightStateMachineConfig config_{};
    LsmActionSink* sink_{nullptr};
    std::string current_{};
};

[[nodiscard]] inline std::string to_string(const LsmAction& value) {
    return value.to_string();
}
[[nodiscard]] inline std::string to_string(const LsmTransition& value) {
    return value.to_string();
}
[[nodiscard]] inline std::string to_string(const LsmErrorRecovery& value) {
    return value.to_string();
}
[[nodiscard]] inline std::string to_string(const LightweightStateMachineConfig& value) {
    return value.to_string();
}
[[nodiscard]] inline std::string to_string(const LightweightStateMachine& value) {
    return value.to_string();
}

[[nodiscard]] constexpr std::string_view to_string(LsmActionType value) noexcept {
    return core::enum_name(value);
}
[[nodiscard]] constexpr bool from_string(std::string_view name, LsmActionType& out) noexcept {
    return core::enum_value(name, out);
}

inline void to_json(nlohmann::json& j, const LsmActionType value) {
    core::mandate::enum_to_json(j, value);
}
inline void from_json(const nlohmann::json& j, LsmActionType& out) noexcept {
    core::mandate::enum_from_json(j, out);
}

void to_json(nlohmann::json& j, const LsmAction& value);
void from_json(const nlohmann::json& j, LsmAction& value) noexcept;
void to_json(nlohmann::json& j, const LsmTransition& value);
void from_json(const nlohmann::json& j, LsmTransition& value) noexcept;
void to_json(nlohmann::json& j, const LsmErrorRecovery& value);
void from_json(const nlohmann::json& j, LsmErrorRecovery& value) noexcept;
void to_json(nlohmann::json& j, const LightweightStateMachineConfig& value);
void from_json(const nlohmann::json& j, LightweightStateMachineConfig& value) noexcept;

} // namespace nexigen::sm

namespace nexigen::core {

template <> struct EnumTraits<nexigen::sm::LsmActionType> {
    struct Entry {
        nexigen::sm::LsmActionType value;
        std::string_view name;
    };
    static constexpr std::array<Entry, 3> entries{{
        {nexigen::sm::LsmActionType::None, "None"},
        {nexigen::sm::LsmActionType::RequestFunctionGroupState, "RequestFunctionGroupState"},
        {nexigen::sm::LsmActionType::PublishState, "PublishState"},
    }};
};

} // namespace nexigen::core

template <> struct std::formatter<nexigen::sm::LsmActionType> : std::formatter<std::string_view> {
    template <class Ctx> auto format(const nexigen::sm::LsmActionType value, Ctx& ctx) const {
        return std::formatter<std::string_view>::format(nexigen::sm::to_string(value), ctx);
    }
};

template <> struct std::formatter<nexigen::sm::LsmAction> : std::formatter<std::string_view> {
    template <class Ctx> auto format(const nexigen::sm::LsmAction& value, Ctx& ctx) const {
        return std::formatter<std::string_view>::format(value.to_string(), ctx);
    }
};

template <> struct std::formatter<nexigen::sm::LsmTransition> : std::formatter<std::string_view> {
    template <class Ctx> auto format(const nexigen::sm::LsmTransition& value, Ctx& ctx) const {
        return std::formatter<std::string_view>::format(value.to_string(), ctx);
    }
};

template <>
struct std::formatter<nexigen::sm::LsmErrorRecovery> : std::formatter<std::string_view> {
    template <class Ctx> auto format(const nexigen::sm::LsmErrorRecovery& value, Ctx& ctx) const {
        return std::formatter<std::string_view>::format(value.to_string(), ctx);
    }
};

template <>
struct std::formatter<nexigen::sm::LightweightStateMachineConfig>
    : std::formatter<std::string_view> {
    template <class Ctx>
    auto format(const nexigen::sm::LightweightStateMachineConfig& value, Ctx& ctx) const {
        return std::formatter<std::string_view>::format(value.to_string(), ctx);
    }
};

template <>
struct std::formatter<nexigen::sm::LightweightStateMachine> : std::formatter<std::string_view> {
    template <class Ctx>
    auto format(const nexigen::sm::LightweightStateMachine& value, Ctx& ctx) const {
        return std::formatter<std::string_view>::format(value.to_string(), ctx);
    }
};
