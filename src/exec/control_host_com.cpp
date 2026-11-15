// nexigen::exec — COM-enabled suite control host
// (change unify-exec-suite-control-path).
//
// Compiled only into `nexigen_exec_control` when NEXIGEN_ENABLE_COM=ON. The
// suite supervisor owns ChildProcess/artifacts/restarts; this host owns the
// StateClient, readiness listeners, process table, and transition engine.
#include <cstdio>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <variant>
#include <vector>

#include <nexigen/app/run_identity.hpp>
#include <nexigen/com/event.hpp>
#include <nexigen/com/node.hpp>
#include <nexigen/com/publisher.hpp>
#include <nexigen/com/service_name.hpp>
#include <nexigen/core/result.hpp>
#include <nexigen/exec/control_host.hpp>
#include <nexigen/exec/execution_manager.hpp>
#include <nexigen/exec/execution_state_tracker.hpp>
#include <nexigen/exec/function_group_registry.hpp>
#include <nexigen/exec/root_function_group.hpp>
#include <nexigen/exec/root_state_query.hpp>
#include <nexigen/exec/process_lifecycle.hpp>
#include <nexigen/exec/recovery_handler.hpp>
#include <nexigen/exec/state_client.hpp>
#include <nexigen/exec/suite_manifest.hpp>

namespace nexigen::exec {

namespace {

/// @brief Standard EM command channel hosted by the suite supervisor.
constexpr const char* control_service_name = "Nexigen/EM/StateCommand";
/// @brief Initial RootFG transition result query channel.
constexpr const char* root_query_service_name = "Nexigen/EM/InitialRootStateTransitionResult";
/// @brief EM→PHM process lifecycle sample channel.
constexpr const char* lifecycle_service_name = "Nexigen/PHM/ExecutionState";
/// @brief EM→PHM lifecycle wake event.
constexpr const char* lifecycle_event_name = "Nexigen/PHM/ExecutionStateEvent";
/// @brief PHM→EM recovery request channel.
constexpr const char* recovery_service_name = "Nexigen/PHM/Recovery";
/// @brief EM→PHM health heartbeat (checkpoint id 1), mirroring SM's channel.
constexpr const char* health_service_name = "Nexigen/EM/Health";

using RootQueryServer = com::Server<std::monostate, InitialRootStateTransitionResultResponse>;
using LifecyclePublisher = com::Publisher<ProcessLifecycleEvent>;

/// @brief Last lifecycle state published for one application.
struct PublishedLifecycle {
    std::string app{};
    std::string function_group{};
    ProcessState last{ProcessState::Idle};
};

class ComControlHost final : public ControlHost {
public:
    ComControlHost(com::Node node, const SuiteManifest& manifest, const app::RunIdentity& identity)
        : node_(std::move(node)), registry_(manifest), identity_(identity) {
        for (const FunctionGroupConfig& group : manifest.function_groups) {
            if (is_root_function_group(group.name)) {
                has_machine_group_ = true;
                break;
            }
        }
        published_.reserve(manifest.apps.size());
        for (const AppSpec& app : manifest.apps) {
            published_.push_back(PublishedLifecycle{app.app, app.function_group,
                                                    ProcessState::Idle});
        }
        const auto lifecycle_name = com::ServiceName::create(lifecycle_service_name);
        const auto lifecycle_event = com::ServiceName::create(lifecycle_event_name);
        if (lifecycle_name && lifecycle_event) {
            auto publisher = LifecyclePublisher::create(*node_, *lifecycle_name, 64);
            auto event = com::EventNotifier::create(*node_, *lifecycle_event);
            if (publisher && event) {
                lifecycle_publisher_.emplace(std::move(*publisher));
                lifecycle_event_.emplace(std::move(*event));
            }
        }
        const auto query_name = com::ServiceName::create(root_query_service_name);
        if (query_name) {
            auto server = RootQueryServer::create(*node_, *query_name);
            if (server) {
                query_server_.emplace(std::move(*server));
            }
        }
        const auto recovery_name = com::ServiceName::create(recovery_service_name);
        if (recovery_name) {
            auto handler = RecoveryHandler::create(*node_, *recovery_name, manifest);
            if (handler) {
                recovery_.emplace(std::move(*handler));
            }
        }
        open_health_channel();
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
        if (health_event_) {
            static_cast<void>(health_event_->notify_with(com::EventId{1}));
        }
        serve_root_query();
        if (recovery_) {
            static_cast<void>(recovery_->serve_once());
            static_cast<void>(recovery_->run_pending());
        }
        if (client_) {
            static_cast<void>(client_->serve_once());
        }
        publish_lifecycle_changes();
    }

    [[nodiscard]] StateResult request_state(std::string_view group,
                                            std::string_view state) noexcept override {
        if (!client_) {
            return StateResult::InvalidArgument;
        }
        // The suite supervisor is the EM-internal caller: it may request the
        // EM-owned RootFG Off state. External SetState still rejects it.
        if (has_machine_group_ && is_root_function_group(group) && state == k_root_state_off) {
            client_->shutdown_machine();
            if (!initial_machine_settled_) {
                initial_machine_result_ = StateResult::Success;
                initial_machine_settled_ = true;
            }
            return StateResult::Success;
        }
        const StateResult result = client_->request_state(group, state);
        if (has_machine_group_ && !initial_machine_settled_ && is_root_function_group(group) &&
            result != StateResult::InProgress) {
            initial_machine_result_ = result;
            initial_machine_settled_ = true;
        }
        return result;
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
        initial_machine_settled_ = false;
        initial_machine_result_ = StateResult::InvalidArgument;
    }

    void shutdown_machine() noexcept override {
        if (client_) {
            client_->shutdown_machine();
        }
    }

    void set_start_handler(ProcessHandler handler) override {
        start_handler_ = std::move(handler);
        if (client_) {
            client_->set_process_handlers(start_handler_, stop_handler_);
        }
    }

    void set_stop_handler(ProcessHandler handler) override {
        stop_handler_ = [this, handler = std::move(handler)](
                            std::string_view app, std::string_view state) -> core::Status {
            mark_terminating(app);
            return handler(app, state);
        };
        if (client_) {
            client_->set_process_handlers(start_handler_, stop_handler_);
        }
    }

    void set_restart_handler(RestartHandler handler) override {
        restart_handler_ = std::move(handler);
        if (recovery_) {
            recovery_->set_restart([this](std::string_view app) { return restart_process(app); });
            recovery_->set_validate(
                [this](std::string_view app) { return admit_recovery(app); });
        }
    }

    void set_recovery_admission(RecoveryAdmissionFn fn) noexcept override {
        recovery_admission_ = std::move(fn);
        if (recovery_ && restart_handler_) {
            recovery_->set_validate(
                [this](std::string_view app) { return admit_recovery(app); });
        }
    }

    /// @brief Side-effect-free admission check for one recovery restart.
    [[nodiscard]] nexigen::phm::RecoveryReason admit_recovery(std::string_view app) noexcept {
        if (!restart_handler_) {
            return nexigen::phm::RecoveryReason::SystemError;
        }
        if (recovery_admission_) {
            const nexigen::phm::RecoveryReason verdict = recovery_admission_(app);
            if (verdict != nexigen::phm::RecoveryReason::None) {
                return verdict;
            }
        }
        const ProcessState tracked = process_state(app);
        const PublishedLifecycle* entry = published_for(app);
        const ProcessState published = entry != nullptr ? entry->last : ProcessState::Idle;
        if (tracked == ProcessState::Starting || published == ProcessState::Terminating) {
            return nexigen::phm::RecoveryReason::AlreadyRestarting;
        }
        return nexigen::phm::RecoveryReason::None;
    }

    [[nodiscard]] core::Status restart_process(std::string_view app) noexcept override {
        if (!restart_handler_) {
            return std::unexpected(core::ErrorCode::InvalidArgument);
        }
        const ProcessState tracked = process_state(app);
        const PublishedLifecycle* entry = published_for(app);
        const ProcessState published = entry != nullptr ? entry->last : ProcessState::Idle;
        if (tracked == ProcessState::Starting || published == ProcessState::Terminating) {
            return std::unexpected(core::ErrorCode::Conflict);
        }
        // PHM sees a Terminating/Starting pair for the forced restart, so the
        // supervision window is suspended instead of looking like a fault.
        mark_terminating(app);
        return restart_handler_(app);
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

    void on_launched(std::string_view app) noexcept override {
        tracker_.on_launch(app);
        if (PublishedLifecycle* entry = published_for(app); entry != nullptr) {
            entry->last = ProcessState::Starting;
        }
    }

    void on_exited(std::string_view app, int exit_code) noexcept override {
        static_cast<void>(tracker_.on_exit(app, exit_code));
        publish_lifecycle_changes();
    }

    void on_failed(std::string_view app) noexcept override {
        publish_lifecycle(ProcessLifecycleState::Failed, app);
    }

private:
    /// @brief Open the EM→PHM heartbeat channel; absence disables the signal.
    void open_health_channel() noexcept {
        const auto health_name = com::ServiceName::create(health_service_name);
        if (!health_name) {
            return;
        }
        auto health = com::EventNotifier::create(*node_, *health_name);
        if (health) {
            health_event_.emplace(std::move(*health));
        }
    }

    /// @brief Find the published-state entry for one application.
    [[nodiscard]] PublishedLifecycle* published_for(std::string_view app) noexcept {
        for (PublishedLifecycle& entry : published_) {
            if (entry.app == app) {
                return &entry;
            }
        }
        return nullptr;
    }

    /// @brief Publish one lifecycle sample and wake PHM.
    void publish_lifecycle(ProcessLifecycleState state, std::string_view app) noexcept {
        if (!lifecycle_publisher_ || !lifecycle_event_) {
            return;
        }
        ProcessLifecycleEvent event{};
        std::snprintf(event.app, sizeof(event.app), "%.*s", static_cast<int>(app.size()),
                      app.data());
        if (const PublishedLifecycle* entry = published_for(app); entry != nullptr) {
            std::snprintf(event.function_group, sizeof(event.function_group), "%s",
                          entry->function_group.c_str());
        }
        event.state = state;
        auto loaned = lifecycle_publisher_->loan_uninit();
        if (!loaned) {
            return;
        }
        auto sample = loaned->write_payload(std::move(event));
        if (!lifecycle_publisher_->send(std::move(sample))) {
            return;
        }
        static_cast<void>(lifecycle_event_->notify());
    }

    /// @brief Publish the Terminating transition before the stop handler runs.
    void mark_terminating(std::string_view app) noexcept {
        if (PublishedLifecycle* entry = published_for(app); entry != nullptr) {
            entry->last = ProcessState::Terminating;
        }
        publish_lifecycle(ProcessLifecycleState::Terminating, app);
    }

    /// @brief Publish every process-table transition since the last poll.
    void publish_lifecycle_changes() noexcept {
        for (PublishedLifecycle& entry : published_) {
            const ProcessState now = tracker_.process_state(entry.app);
            if (now == entry.last) {
                continue;
            }
            entry.last = now;
            switch (now) {
                case ProcessState::Running:
                    publish_lifecycle(ProcessLifecycleState::Running, entry.app);
                    break;
                case ProcessState::Terminating:
                    publish_lifecycle(ProcessLifecycleState::Terminating, entry.app);
                    break;
                case ProcessState::Terminated:
                    publish_lifecycle(ProcessLifecycleState::Terminated, entry.app);
                    break;
                case ProcessState::Idle:
                case ProcessState::Starting: break;
            }
        }
    }

    /// @brief Serve one `GetInitialRootStateTransitionResult` request.
    void serve_root_query() noexcept {
        if (!query_server_) {
            return;
        }
        auto received = query_server_->receive();
        if (!received || !received->has_value()) {
            return;
        }
        InitialRootStateTransitionResultResponse response{};
        if (!has_machine_group_) {
            response.result = StateResult::InvalidArgument;
        } else if (!initial_machine_settled_) {
            response.result = StateResult::InProgress;
        } else {
            response.result = initial_machine_result_;
        }
        static_cast<void>((*received)->send_copy(response));
    }
    std::optional<com::Node> node_;
    FunctionGroupRegistry registry_;
    ExecutionStateTracker tracker_;
    ExecutionManager manager_;
    app::RunIdentity identity_;
    std::optional<StateClient> client_;
    std::optional<RootQueryServer> query_server_;
    std::optional<RecoveryHandler> recovery_;
    std::optional<com::EventNotifier> health_event_;
    std::optional<LifecyclePublisher> lifecycle_publisher_;
    std::optional<com::EventNotifier> lifecycle_event_;
    std::vector<PublishedLifecycle> published_{};
    bool has_machine_group_{false};
    bool initial_machine_settled_{false};
    StateResult initial_machine_result_{StateResult::InvalidArgument};
    RecoveryAdmissionFn recovery_admission_{};
    ProcessHandler start_handler_{};
    ProcessHandler stop_handler_{};
    RestartHandler restart_handler_{};
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
