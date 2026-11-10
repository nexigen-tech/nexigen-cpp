// nexigen::exec — StateClient receiver (change add-sm; spec exec/state-client;
// design D4).
//
// The EM-side receiver of the State Manager's SetState commands: a
// request-response server that maps Function Groups onto the EM's managed
// processes and executes the switch through the existing ExecutionManager
// start/stop machinery, acknowledging completion or rejection. The EM calls
// serve_once() from its control loop — never a polling loop of its own.
//
// NOTE: the definition lives in src/exec/state_client.cpp, built into the
// NEXIGEN_ENABLE_COM-gated `nexigen_state_client` target (the class depends
// on nexigen::com, which only exists behind the gate).
#pragma once

#include <format>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include <nexigen/app/run_identity.hpp>
#include <nexigen/com/error.hpp>
#include <nexigen/com/node.hpp>
#include <nexigen/com/server.hpp>
#include <nexigen/com/service_name.hpp>
#include <nexigen/core/result.hpp>
#include <nexigen/exec/execution_manager.hpp>
#include <nexigen/exec/state_ack.hpp>
#include <nexigen/exec/state_command.hpp>

namespace nexigen::exec {

/// The EM-side receiver of SetState commands from the State Manager.
class StateClient {
public:
    /// One Function Group -> managed processes mapping.
    struct GroupMapping {
        std::string function_group{};
        std::vector<std::string> app_names{};
        std::string binary{};  ///< Launch command; empty = app_name itself.
    };

    /// Open (or create) the state-command channel on the node and bind the
    /// group mappings plus the ExecutionManager used to execute switches.
    [[nodiscard]] static com::Result<StateClient> create(
        com::Node& node, const com::ServiceName& name, std::vector<GroupMapping> mappings,
        ExecutionManager& manager, const app::RunIdentity& identity) noexcept;

    StateClient(StateClient&&) noexcept = default;
    StateClient& operator=(StateClient&&) noexcept = default;

    /// Serve one pending command: receive, execute, acknowledge. Returns true
    /// when a command was handled, false when none was pending.
    [[nodiscard]] com::Result<bool> serve_once() noexcept;

    /// Cold-path diagnostic (mandate 9): service name + last command.
    [[nodiscard]] std::string to_string() const;

private:
    StateClient(com::ServiceName name, com::Server<StateCommand, StateAck> server,
                std::vector<GroupMapping> mappings, ExecutionManager* manager,
                app::RunIdentity identity) noexcept;

    /// Execute one SetState command through the ExecutionManager: stop the
    /// group's processes for Off-like targets, start them otherwise. An
    /// unmapped group is rejected.
    [[nodiscard]] bool execute(const StateCommand& command) noexcept;

    [[nodiscard]] static bool is_off_target(std::string_view target) noexcept;

    [[nodiscard]] std::string instance_dir_for(std::string_view app_name) const;

    com::ServiceName service_name_;
    com::Server<StateCommand, StateAck> server_;
    std::vector<GroupMapping> mappings_;
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
    template <class Ctx>
    auto format(const nexigen::exec::StateClient& client, Ctx& ctx) const {
        const std::string text = client.to_string();
        return std::formatter<std::string_view>::format(text, ctx);
    }
};
