// nexigen::sm — EM RootFG lifecycle client
// (change machinefg-as-em-function-group; spec sm/state-machine).
//
// The SM uses this helper after it starts as a RootFG process: report its
// own readiness, read the EM's initial RootFG transition result, and command
// the Verify/Run/Shutdown steps through the standard StateClient channel. The
// helper is cold path only; the arbitration loop never touches it.
#pragma once

#include <chrono>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <utility>

#include <nexigen/com/client.hpp>
#include <nexigen/com/node.hpp>
#include <nexigen/core/result.hpp>
#include <nexigen/exec/application_state.hpp>
#include <nexigen/exec/root_state_query.hpp>
#include <nexigen/exec/state_ack.hpp>
#include <nexigen/exec/state_command.hpp>
#include <nexigen/exec/state_result.hpp>

namespace nexigen::sm {

/// @brief SM-side client for the EM `RootFG` lifecycle.
///
/// Owns the readiness reporter, the initial-transition query client, and the
/// ordinary `StateCommand` client. All operations are blocking and belong to
/// the SM startup/shutdown path, never to the arbitration loop.
///
/// @note Spec `sm/state-machine`, requirement "Startup and shutdown
///       sequencing".
/// @warning The node passed to `create` must outlive the client.
class RootLifecycleClient {
public:
    /// @brief Open the readiness, query, and command channels.
    ///
    /// @param node Node that owns the transport; must outlive the client.
    /// @param app_name SM application name reported to the EM.
    /// @return A ready client, or the first mapped com error.
    [[nodiscard]] static com::Result<RootLifecycleClient> create(com::Node& node,
                                                                 std::string app_name) noexcept;

    /// @brief Copying is disabled: the client owns transport handles.
    RootLifecycleClient(const RootLifecycleClient&) = delete;
    /// @brief Copy assignment is disabled.
    RootLifecycleClient& operator=(const RootLifecycleClient&) = delete;

    /// @brief Move-construct a client, transferring every handle.
    RootLifecycleClient(RootLifecycleClient&&) noexcept = default;
    /// @brief Move-assign a client, transferring every handle.
    /// @return This client.
    RootLifecycleClient& operator=(RootLifecycleClient&&) noexcept = default;

    /// @brief Report the SM's own `Running` readiness to the EM.
    ///
    /// @return Empty status, or the com error from the report.
    [[nodiscard]] com::Status report_ready() noexcept;

    /// @brief Read the EM's initial `RootFG` transition result.
    ///
    /// Retries while the EM reports `InProgress` until the timeout expires.
    ///
    /// @param timeout Total budget for the query.
    /// @return The terminal `StateResult`, or the last com error/timeout.
    [[nodiscard]] com::Result<exec::StateResult>
    initial_transition_result(std::chrono::milliseconds timeout) noexcept;

    /// @brief Command one `RootFG` state and wait for the terminal result.
    ///
    /// Retries while the EM acknowledges `InProgress`.
    ///
    /// @param target `RootFG` state name (`Verify`, `Run`, `Shutdown`).
    /// @param timeout Total budget for the transition.
    /// @return `Success` or the terminal failure result, or the last com error.
    [[nodiscard]] com::Result<exec::StateResult>
    request_state(std::string_view target, std::chrono::milliseconds timeout) noexcept;

    /// @brief Whether the client owns a working readiness reporter.
    /// @return `true` when readiness reporting is available.
    [[nodiscard]] bool available() const noexcept { return reporter_.has_value(); }

private:
    RootLifecycleClient(
        std::string app_name, std::optional<exec::ApplicationStateReporter> reporter,
        com::Client<std::monostate, exec::InitialRootStateTransitionResultResponse> query,
        com::Client<exec::StateCommand, exec::StateAck> command) noexcept;

    std::string app_name_{};
    std::optional<exec::ApplicationStateReporter> reporter_{};
    com::Client<std::monostate, exec::InitialRootStateTransitionResultResponse> query_;
    com::Client<exec::StateCommand, exec::StateAck> command_;
    std::uint64_t next_request_id_{1};
};

} // namespace nexigen::sm
