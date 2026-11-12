// nexigen::exec — StateClient receiver (change add-sm; change
// add-em-orchestration adds Function Groups + SafeState priority;
// specs exec/state-client + exec/app-suite; design D3/D4).
//
// The EM-side receiver of the State Manager's SetState commands: a
// request-response server on the ordinary channel plus an emergency channel
// (`<channel>Emergency`) reserved for PHM SafeState commands, which
// serve_once drains first — SafeState always overtakes queued ordinary
// transitions. Commands map Function Groups onto the EM's managed processes
// (FunctionGroupRegistry) and execute through the existing ExecutionManager
// start/stop machinery.
//
// NOTE: the definition lives in src/exec/state_client.cpp, built into the
// NEXIGEN_ENABLE_COM-gated `nexigen_state_client` target (the class depends
// on nexigen::com, which only exists behind the gate).
#pragma once

#include <format>
#include <string>
#include <string_view>
#include <utility>

#include <nexigen/app/run_identity.hpp>
#include <nexigen/com/error.hpp>
#include <nexigen/com/node.hpp>
#include <nexigen/com/server.hpp>
#include <nexigen/com/service_name.hpp>
#include <nexigen/core/result.hpp>
#include <nexigen/exec/execution_manager.hpp>
#include <nexigen/exec/function_group_registry.hpp>
#include <nexigen/exec/state_ack.hpp>
#include <nexigen/exec/state_command.hpp>

namespace nexigen::exec {

/// @brief The EM-side receiver of `SetState` commands from the State Manager.
///
/// Owns the ordinary request-response server and its emergency companion
/// (`<name>Emergency`), borrows the `FunctionGroupRegistry` and
/// `ExecutionManager` that execute switches, and records the last command for
/// diagnostics. The caller pumps `serve_once` from one control thread; the
/// class is not synchronized internally. SafeState commands on the emergency
/// channel always overtake queued ordinary transitions. Cold path only.
///
/// @note Spec `exec/state-client`, requirement "StateClient command
///       reception".
/// @note Spec `exec/state-client`, requirement "SafeState priority".
/// @warning `registry` and `manager` must outlive the client, and the client
///          must be served from a single thread.
class StateClient {
public:
    /// @brief Open the ordinary command channel plus its emergency channel and
    ///        bind the execution dependencies.
    ///
    /// Creates a server on `name` and on `<name>Emergency`, then stores the
    /// group registry, the manager, and the run identity used to derive
    /// instance directories.
    ///
    /// @param node Com node owning both servers; must outlive the client.
    /// @param name Ordinary command service name.
    /// @param registry Function Group registry; borrowed, must outlive the
    ///        client.
    /// @param manager ExecutionManager used to start and stop processes;
    ///        borrowed, must outlive the client.
    /// @param identity Run identity used to derive `<run_dir>/<app>/<instance>`
    ///        directories.
    /// @return A ready client, or the com error from either server creation.
    /// @note Spec `exec/state-client`, requirement "StateClient command
    ///       reception".
    /// @note Spec `exec/state-client`, requirement "SafeState priority".
    [[nodiscard]] static com::Result<StateClient>
    create(com::Node& node, const com::ServiceName& name, FunctionGroupRegistry& registry,
           ExecutionManager& manager, const app::RunIdentity& identity) noexcept;

    /// @brief Move construct a client, transferring both servers.
    StateClient(StateClient&&) noexcept = default;

    /// @brief Move assign a client, transferring both servers.
    /// @return The destination client.
    StateClient& operator=(StateClient&&) noexcept = default;

    /// @brief Serve one pending command.
    ///
    /// Drains the emergency channel first (SafeState overtakes), then the
    /// ordinary channel. A handled command is executed and acknowledged; the
    /// last-command diagnostic is updated.
    ///
    /// @return `true` when a command was handled, `false` when none was
    ///         pending, or the com error from receive or acknowledge.
    /// @note Spec `exec/state-client`, requirement "SafeState priority".
    /// @note Spec `exec/state-client`, scenario "Ordinary transitions proceed afterwards".
    [[nodiscard]] com::Result<bool> serve_once() noexcept;

    /// @brief Cold-path diagnostic (mandate 9): service name + last command.
    ///
    /// @return `StateClient(service=<name>, last_command=<text>)`.
    /// @note Spec `exec/state-client`, requirement "StateClient diagnostics".
    [[nodiscard]] std::string to_string() const;

private:
    /// @brief Construct a client from prepared servers and borrowed services.
    ///
    /// @param name Ordinary command service name.
    /// @param server Ordinary request-response server.
    /// @param emergency_server Emergency SafeState server.
    /// @param registry Borrowed Function Group registry.
    /// @param manager Borrowed execution manager.
    /// @param identity Run identity for instance directory derivation.
    StateClient(com::ServiceName name, com::Server<StateCommand, StateAck> server,
                com::Server<StateCommand, StateAck> emergency_server,
                FunctionGroupRegistry* registry, ExecutionManager* manager,
                app::RunIdentity identity) noexcept;

    /// @brief Receive from one channel, execute the command, and acknowledge.
    ///
    /// @param server Channel to poll.
    /// @return `true` when a command was received and acknowledged, `false`
    ///         when none was pending, or a com error.
    [[nodiscard]] com::Result<bool>
    serve_from(com::Server<StateCommand, StateAck>& server) noexcept;

    /// @brief Execute one `SetState` command through the `ExecutionManager`.
    ///
    /// Stops the group's bound processes for Off-like targets and starts them
    /// otherwise. An unmapped group is rejected; a failing start/stop
    /// acknowledges rejection. On full success the registry's group state is
    /// updated.
    ///
    /// @param command Command to execute.
    /// @return `true` when the whole group switched, `false` on an unknown
    ///         group or a failed process step.
    /// @note Spec `exec/state-client`, requirement "Function Group execution".
    [[nodiscard]] bool execute(const StateCommand& command) noexcept;

    /// @brief Control instance directory of one application.
    ///
    /// @param app_name Application name.
    /// @return `<run_dir>/<app_name>/<instance_id>` from the stored identity.
    [[nodiscard]] std::string instance_dir_for(std::string_view app_name) const;

    com::ServiceName service_name_;                        ///< Ordinary service name.
    com::Server<StateCommand, StateAck> server_;           ///< Ordinary command server.
    com::Server<StateCommand, StateAck> emergency_server_; ///< Emergency SafeState server.
    FunctionGroupRegistry* registry_{nullptr};             ///< Borrowed group registry.
    ExecutionManager* manager_{nullptr};                   ///< Borrowed execution manager.
    app::RunIdentity identity_;                            ///< Run identity for instance paths.
    std::string last_command_{};                           ///< Last handled command diagnostic.
};

/// @brief Free helper (mandate 9): mirrors the member.
///
/// @param client Client to render.
/// @return The same text as `client.to_string()`.
[[nodiscard]] inline std::string to_string(const StateClient& client) {
    return client.to_string();
}

} // namespace nexigen::exec

/// @brief The StateClient formats as its diagnostic string (mandate 11).
template <> struct std::formatter<nexigen::exec::StateClient> : std::formatter<std::string_view> {
    /// @brief Format the client through its `to_string()` member.
    ///
    /// @tparam Ctx `std::formatter` context type.
    /// @param client Client to format.
    /// @param ctx Format context receiving the text.
    /// @return The context's output iterator.
    template <class Ctx> auto format(const nexigen::exec::StateClient& client, Ctx& ctx) const {
        const std::string text = client.to_string();
        return std::formatter<std::string_view>::format(text, ctx);
    }
};
