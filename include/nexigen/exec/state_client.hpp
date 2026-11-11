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

/// The EM-side receiver of SetState commands from the State Manager.
class StateClient {
public:
    /// Open the ordinary command channel plus its emergency channel
    /// (`<name>Emergency`) and bind the group registry plus the
    /// ExecutionManager used to execute switches.
    [[nodiscard]] static com::Result<StateClient>
    create(com::Node& node, const com::ServiceName& name, FunctionGroupRegistry& registry,
           ExecutionManager& manager, const app::RunIdentity& identity) noexcept;

    StateClient(StateClient&&) noexcept = default;
    StateClient& operator=(StateClient&&) noexcept = default;

    /// Serve one pending command: emergency channel first (SafeState
    /// overtakes), then the ordinary channel. Returns true when a command was
    /// handled, false when none was pending.
    [[nodiscard]] com::Result<bool> serve_once() noexcept;

    /// Cold-path diagnostic (mandate 9): service name + last command.
    [[nodiscard]] std::string to_string() const;

private:
    StateClient(com::ServiceName name, com::Server<StateCommand, StateAck> server,
                com::Server<StateCommand, StateAck> emergency_server,
                FunctionGroupRegistry* registry, ExecutionManager* manager,
                app::RunIdentity identity) noexcept;

    /// Receive from one channel, execute the command, and acknowledge.
    [[nodiscard]] com::Result<bool>
    serve_from(com::Server<StateCommand, StateAck>& server) noexcept;

    /// Execute one SetState command through the ExecutionManager: stop the
    /// group's bound processes for Off-like targets, start them otherwise. An
    /// unmapped group is rejected.
    [[nodiscard]] bool execute(const StateCommand& command) noexcept;

    [[nodiscard]] std::string instance_dir_for(std::string_view app_name) const;

    com::ServiceName service_name_;
    com::Server<StateCommand, StateAck> server_;
    com::Server<StateCommand, StateAck> emergency_server_;
    FunctionGroupRegistry* registry_{nullptr};
    ExecutionManager* manager_{nullptr};
    app::RunIdentity identity_;
    std::string last_command_{};
};

/// Free helper (mandate 9): mirrors the member.
[[nodiscard]] inline std::string to_string(const StateClient& client) {
    return client.to_string();
}

} // namespace nexigen::exec

/// The StateClient formats as its diagnostic string (mandate 11).
template <> struct std::formatter<nexigen::exec::StateClient> : std::formatter<std::string_view> {
    template <class Ctx> auto format(const nexigen::exec::StateClient& client, Ctx& ctx) const {
        const std::string text = client.to_string();
        return std::formatter<std::string_view>::format(text, ctx);
    }
};
