// nexigen::exec — COM-enabled suite control host
// (change unify-exec-suite-control-path).
//
// Compiled only into `nexigen_exec_control` when NEXIGEN_ENABLE_COM=ON. The
// suite supervisor owns ChildProcess/artifacts/restarts; this host owns the
// StateClient, readiness listeners, process table, and transition engine.
#include <memory>
#include <optional>
#include <string_view>
#include <utility>

#include <nexigen/app/run_identity.hpp>
#include <nexigen/com/node.hpp>
#include <nexigen/com/service_name.hpp>
#include <nexigen/core/result.hpp>
#include <nexigen/exec/control_host.hpp>
#include <nexigen/exec/execution_manager.hpp>
#include <nexigen/exec/execution_state_tracker.hpp>
#include <nexigen/exec/function_group_registry.hpp>
#include <nexigen/exec/state_client.hpp>
#include <nexigen/exec/suite_manifest.hpp>

namespace nexigen::exec {

namespace {

/// @brief Standard EM command channel hosted by the suite supervisor.
constexpr const char* control_service_name = "Nexigen/EM/StateCommand";

class ComControlHost final : public ControlHost {
public:
    ComControlHost(com::Node node, const SuiteManifest& manifest, const app::RunIdentity& identity)
        : node_(std::move(node)), registry_(manifest), identity_(identity) {
        const auto name = com::ServiceName::create(control_service_name);
        if (!name) {
            return;
        }
        auto client = StateClient::create(*node_, *name, registry_, manager_, identity_, &tracker_);
        if (client) {
            client_.emplace(std::move(*client));
        }
    }

    /// @brief Whether the StateClient was created successfully.
    [[nodiscard]] bool valid() const noexcept { return client_.has_value(); }

    void poll() noexcept override {
        if (client_) {
            static_cast<void>(client_->serve_once());
        }
    }

    [[nodiscard]] StateResult request_state(std::string_view group,
                                            std::string_view state) noexcept override {
        return client_ ? client_->request_state(group, state) : StateResult::InvalidArgument;
    }

    [[nodiscard]] std::string_view group_state(std::string_view group) const noexcept override {
        return client_ ? client_->group_state(group) : std::string_view{};
    }

    [[nodiscard]] bool transition_active(std::string_view group) const noexcept override {
        return client_ ? client_->transition_active(group) : false;
    }

    void reset_groups() noexcept override {
        if (client_) {
            client_->reset_groups();
        }
    }

    void set_start_handler(ProcessHandler handler) override {
        start_handler_ = std::move(handler);
        if (client_) {
            client_->set_process_handlers(start_handler_, stop_handler_);
        }
    }

    void set_stop_handler(ProcessHandler handler) override {
        stop_handler_ = std::move(handler);
        if (client_) {
            client_->set_process_handlers(start_handler_, stop_handler_);
        }
    }

    [[nodiscard]] bool ready(std::string_view app) const noexcept override {
        return tracker_.ready(app);
    }

    [[nodiscard]] ProcessState process_state(std::string_view app) const noexcept override {
        return tracker_.process_state(app);
    }

    [[nodiscard]] AppState app_state(std::string_view app) const noexcept override {
        return tracker_.state(app);
    }

    void on_launched(std::string_view app) noexcept override { tracker_.on_launch(app); }

    void on_exited(std::string_view app, int exit_code) noexcept override {
        static_cast<void>(tracker_.on_exit(app, exit_code));
    }

private:
    std::optional<com::Node> node_;
    FunctionGroupRegistry registry_;
    ExecutionStateTracker tracker_;
    ExecutionManager manager_;
    app::RunIdentity identity_;
    std::optional<StateClient> client_;
    ProcessHandler start_handler_{};
    ProcessHandler stop_handler_{};
};

std::unique_ptr<ControlHost> make_control_host(const SuiteManifest& manifest,
                                               const app::RunIdentity& identity) {
    auto node = com::Node::create();
    if (!node) {
        return nullptr;
    }
    auto host = std::make_unique<ComControlHost>(std::move(*node), manifest, identity);
    if (!host->valid()) {
        return nullptr;
    }
    return host;
}

} // namespace

void install_control_host_factory() noexcept {
    set_control_host_factory(&make_control_host);
}

} // namespace nexigen::exec
