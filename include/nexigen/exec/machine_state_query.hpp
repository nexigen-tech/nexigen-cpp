// nexigen::exec — initial MachineFG transition result query payloads
// (change machinefg-as-em-function-group; spec exec/machine-function-group).
//
// The request is `std::monostate` (an empty POD). The response is a
// fixed-size trivially copyable POD so the com transport needs no serialization
// and the control host allocates nothing while serving the query.
#pragma once

#include <format>
#include <string>
#include <string_view>

#include <nlohmann/json.hpp>

#include <nexigen/exec/state_result.hpp>

namespace nexigen::exec {

/// @brief Response of `GetInitialMachineStateTransitionResult`.
struct InitialMachineStateTransitionResultResponse {
    StateResult result{StateResult::InvalidArgument}; ///< Recorded terminal result.

    /// @brief Cold-path diagnostic rendering.
    ///
    /// @return `InitialMachineStateTransitionResultResponse(result=<name>)`.
    [[nodiscard]] std::string to_string() const;
};

/// @brief Free diagnostic helper (project mandate 9).
[[nodiscard]] inline std::string
to_string(const InitialMachineStateTransitionResultResponse& response) {
    return response.to_string();
}

/// @brief Serialize the response as its result name.
///
/// @param j Destination JSON value.
/// @param response Response to serialize.
/// @note Cold-path JSON only (project mandate 10).
void to_json(nlohmann::json& j, const InitialMachineStateTransitionResultResponse& response);

/// @brief Parse the response without throwing under `JSON_NOEXCEPTION=1`.
///
/// Hostile input leaves the destination unchanged.
///
/// @param j Source JSON value.
/// @param response Target response.
/// @note Cold-path JSON only (project mandate 10).
void from_json(const nlohmann::json& j,
               InitialMachineStateTransitionResultResponse& response) noexcept;

} // namespace nexigen::exec

/// @brief `std::formatter` for the query response (project mandate 11).
template <>
struct std::formatter<nexigen::exec::InitialMachineStateTransitionResultResponse>
    : std::formatter<std::string_view> {
    template <class Ctx>
    auto format(const nexigen::exec::InitialMachineStateTransitionResultResponse& response,
                Ctx& ctx) const {
        return std::formatter<std::string_view>::format(response.to_string(), ctx);
    }
};
