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
    LsmActionType type{LsmActionType::None};
    std::string function_group{};
    std::string target_state{};

    [[nodiscard]] std::string to_string() const;
};

/// @brief One allowed transition plus its ordered action indices.
struct LsmTransition {
    std::string from{};
    std::string to{};
    std::vector<std::uint32_t> actions{};

    [[nodiscard]] std::string to_string() const;
};

/// @brief One error-recovery rule.
struct LsmErrorRecovery {
    std::string from{};
    std::string fallback_state{};
    std::vector<std::uint32_t> actions{};

    [[nodiscard]] std::string to_string() const;
};

/// @brief Configuration of one lightweight state machine.
struct LightweightStateMachineConfig {
    std::string name{};
    std::vector<std::string> states{};
    std::string initial_state{};
    std::vector<LsmAction> actions{};
    std::vector<LsmTransition> transitions{};
    std::vector<LsmErrorRecovery> error_recovery{};

    [[nodiscard]] std::string to_string() const;

    /// @brief Validate states and action indices.
    [[nodiscard]] core::Status validate() const noexcept;
};

/// @brief Side effects of the interpreter.
class LsmActionSink {
public:
    virtual ~LsmActionSink() = default;
    virtual core::Status request_state(std::string_view group, std::string_view state,
                                       std::string_view requester) = 0;
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
    [[nodiscard]] std::string_view current_state() const noexcept { return current_; }

    /// @brief Cold-path diagnostic rendering.
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
