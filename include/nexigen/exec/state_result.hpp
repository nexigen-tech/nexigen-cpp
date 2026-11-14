// nexigen::exec — StateResult enum (change
// align-exec-process-model-with-autosar; spec exec/state-client).
//
// The result of one SetState request. `InProgress` is the retryable outcome
// while a Function Group transition is running; the remaining values are
// terminal success or failure outcomes.
#pragma once

#include <array>
#include <cstdint>
#include <format>
#include <string_view>

#include <nlohmann/json.hpp>

#include <nexigen/core/enum_traits.hpp>
#include <nexigen/core/serialization_mandate.hpp>

namespace nexigen::exec {

/// @brief Result of one `SetState` request.
///
/// @note Spec `exec/state-client`, requirement "StateClient command reception".
enum class StateResult : std::uint8_t {
    Success,               ///< The requested state is current.
    InProgress,            ///< The transition is still running; retry later.
    OperationFailed,       ///< A startup/restart budget was exhausted.
    OperationCanceled,     ///< A newer transition replaced this one.
    UnexpectedTermination, ///< A requested-state process died unexpectedly.
    InvalidTransition,     ///< The graph or current state forbids the request.
    InvalidArgument,       ///< Unknown group, unknown state, or bad payload.
};

} // namespace nexigen::exec

// Vocabulary table for core::enum_name/enum_value.
namespace nexigen::core {

/// @brief `StateResult` vocabulary table.
///
/// @note Spec `exec/state-client`, requirement "StateClient command reception".
template <> struct EnumTraits<nexigen::exec::StateResult> {
    /// @brief One value-to-name row.
    struct Entry {
        nexigen::exec::StateResult value; ///< Enum value.
        std::string_view name;            ///< Canonical lower-case spelling.
    };
    /// @brief Value/name rows in declaration order.
    static constexpr std::array<Entry, 7> entries{{
        {nexigen::exec::StateResult::Success, "success"},
        {nexigen::exec::StateResult::InProgress, "in_progress"},
        {nexigen::exec::StateResult::OperationFailed, "operation_failed"},
        {nexigen::exec::StateResult::OperationCanceled, "operation_canceled"},
        {nexigen::exec::StateResult::UnexpectedTermination, "unexpected_termination"},
        {nexigen::exec::StateResult::InvalidTransition, "invalid_transition"},
        {nexigen::exec::StateResult::InvalidArgument, "invalid_argument"},
    }};
    /// @brief Name returned when no entry matches.
    static constexpr std::string_view fallback = "unknown";
};

} // namespace nexigen::core

namespace nexigen::exec {

/// @brief Canonical name of a state result.
///
/// @param value Result to name.
/// @return Lower-case result name, or `unknown`.
/// @note Spec `exec/state-client`, requirement "StateClient command reception".
[[nodiscard]] constexpr std::string_view to_string(StateResult value) noexcept {
    return core::enum_name(value);
}

/// @brief Parse a canonical state-result name.
///
/// @param name Lower-case name.
/// @param out Receives the value only on success.
/// @return `true` when parsed; `false` leaves `out` unchanged.
/// @note Spec `exec/state-client`, requirement "StateClient command reception".
[[nodiscard]] constexpr bool from_string(std::string_view name, StateResult& out) noexcept {
    return core::enum_value(name, out);
}

/// @brief Serialize a state result as its canonical name.
///
/// @param j Destination JSON value.
/// @param value Result to serialize.
/// @note Spec `exec/state-client`, requirement "StateClient command reception".
void to_json(nlohmann::json& j, StateResult value);

/// @brief Parse a state result from JSON without throwing.
///
/// @param j Source JSON value.
/// @param value Target; unchanged on failure.
/// @note Spec `exec/state-client`, requirement "StateClient command reception".
void from_json(const nlohmann::json& j, StateResult& value) noexcept;

} // namespace nexigen::exec

/// @brief `std::formatter` for `StateResult`.
template <> struct std::formatter<nexigen::exec::StateResult> : std::formatter<std::string_view> {
    /// @brief Format through the canonical name.
    template <class Ctx> auto format(nexigen::exec::StateResult value, Ctx& ctx) const {
        return std::formatter<std::string_view>::format(nexigen::exec::to_string(value), ctx);
    }
};
