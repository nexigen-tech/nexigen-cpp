// Тесты nexigen::phm — харнес как в остальных модулях (NX_CHECK + run_suite).
// Сборка только при NEXIGEN_ENABLE_COM=ON.
//
// Наборы: health_channel (каналы + чекпоинты), supervision (alive/logical/
// deadline/tolerance), recovery (RecoveryClient против mock-EM + fault
// storage), manifest (round-trip + hostile input).
#include <array>
#include <chrono>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <unistd.h>
#include <cstdio>
#include <format>
#include <string>
#include <string_view>
#include <thread>
#include <vector>

#include <nlohmann/json.hpp>

#include <nexigen/com/event.hpp>
#include <nexigen/com/node.hpp>
#include <nexigen/com/server.hpp>
#include <nexigen/com/service_name.hpp>
#include <nexigen/com/subscriber.hpp>
#include <nexigen/core/affinity.hpp>
#include <nexigen/core/detail/fault_injection.hpp>
#include <nexigen/core/serialization_mandate.hpp>
#include <nexigen/core/thread_management.hpp>
#include <nexigen/phm/checkpoint.hpp>
#include <nexigen/phm/checkpoint_claim.hpp>
#include <nexigen/phm/checkpoint_identity.hpp>
#include <nexigen/phm/claim_publisher.hpp>
#include <nexigen/phm/claim_registry.hpp>
#include <nexigen/phm/em_lifecycle.hpp>
#include <nexigen/phm/fault_kind.hpp>
#include <nexigen/phm/fault_log_verify.hpp>
#include <nexigen/phm/fault_storage.hpp>
#include <nexigen/phm/health_channel.hpp>
#include <nexigen/phm/health_state.hpp>
#include <nexigen/phm/phm_manifest.hpp>
#include <nexigen/phm/posix_watchdog_device.hpp>
#include <nexigen/phm/recovery.hpp>
#include <nexigen/phm/recovery_ack.hpp>
#include <nexigen/phm/recovery_ack_tracker.hpp>
#include <nexigen/phm/recovery_action.hpp>
#include <nexigen/phm/recovery_request.hpp>
#include <nexigen/phm/recovery_response.hpp>
#include <nexigen/phm/security_event.hpp>
#include <nexigen/phm/security_event_kind.hpp>
#include <nexigen/phm/supervision_status.hpp>
#include <nexigen/phm/supervised_entity.hpp>
#include <nexigen/phm/supervisor.hpp>
#include <nexigen/phm/watchdog.hpp>
#include <nexigen/phm/watchdog_state.hpp>
#include <nexigen/sm/group_state_payload.hpp>
#include <nexigen/sm/recovery_notification.hpp>

#include "fake_watchdog_device.hpp"

namespace com = nexigen::com;
namespace phm = nexigen::phm;

namespace {
// Allocation detector: the C++ free store is re-backed by a fixed static bump
// arena, so any operator new in the process moves g_alloc_used. The supervision
// hot path must leave it flat. 32 MiB covers every manifest, diagnostic string,
// and transport wrapper the suite constructs.
alignas(16) unsigned char g_arena[32U << 20U]{};
} // namespace

std::size_t g_alloc_used = 0;

void* operator new(std::size_t size) {
    const std::size_t rounded = (size + 15U) & ~static_cast<std::size_t>(15U);
    if (g_alloc_used + rounded > sizeof(g_arena)) {
        std::abort();
    }
    void* p = g_arena + g_alloc_used;
    g_alloc_used += rounded;
    return p;
}

void* operator new[](std::size_t size) {
    return ::operator new(size);
}

void operator delete(void* ptr) noexcept {
    static_cast<void>(ptr);
}
void operator delete(void* ptr, std::size_t) noexcept {
    static_cast<void>(ptr);
}
void operator delete[](void* ptr) noexcept {
    static_cast<void>(ptr);
}
void operator delete[](void* ptr, std::size_t) noexcept {
    static_cast<void>(ptr);
}

namespace {

int g_checks = 0;
int g_failures = 0;

#if defined(NEXIGEN_ENABLE_FAULT_INJECTION)
nexigen::core::fault_injection::FaultPoint g_phm_fault_point =
    nexigen::core::fault_injection::FaultPoint::Phm_claim_send;
bool g_phm_fault_enabled = false;

bool phm_fault_hook(nexigen::core::fault_injection::FaultPoint point) noexcept {
    return g_phm_fault_enabled && point == g_phm_fault_point;
}
#endif

#define NX_CHECK(...)                                                                              \
    do {                                                                                           \
        ++g_checks;                                                                                \
        if (!(__VA_ARGS__)) {                                                                      \
            ++g_failures;                                                                          \
            std::fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #__VA_ARGS__);            \
        }                                                                                          \
    } while (false)

void run_suite(const char* name, void (*body)()) {
    const int checks_before = g_checks;
    const int failures_before = g_failures;
    body();
    std::printf("[%-22s] %d checks, %d failed\n", name, g_checks - checks_before,
                g_failures - failures_before);
}

template <class E> void check_enum_round_trip(std::initializer_list<E> values) {
    for (const E value : values) {
        const std::string_view name = to_string(value);
        NX_CHECK(!name.empty());
        E back{};
        NX_CHECK(from_string(name, back));
        NX_CHECK(back == value);
        nlohmann::json j;
        to_json(j, value);
        NX_CHECK(j == std::string(name));
        E parsed{};
        from_json(j, parsed);
        NX_CHECK(parsed == value);
        NX_CHECK(std::format("{}", value) == std::string(name));
    }
}

void test_vocabulary() {
    check_enum_round_trip({phm::HealthState::Ok, phm::HealthState::Warning, phm::HealthState::Error,
                           phm::HealthState::Critical});
    check_enum_round_trip({phm::SupervisionStatus::Ok, phm::SupervisionStatus::Failed,
                           phm::SupervisionStatus::Expired, phm::SupervisionStatus::Exhausted});
    check_enum_round_trip({phm::RecoveryAction::RestartProcess,
                           phm::RecoveryAction::SwitchFunctionGroupState,
                           phm::RecoveryAction::TriggerWatchdogReset});
    check_enum_round_trip({phm::FaultKind::AliveFailure, phm::FaultKind::LogicalFailure,
                           phm::FaultKind::DeadlineFailure, phm::FaultKind::UnknownGroup});
    check_enum_round_trip({phm::RecoveryOutcome::Accepted, phm::RecoveryOutcome::InProgress,
                           phm::RecoveryOutcome::Rejected});
    check_enum_round_trip(
        {phm::RecoveryReason::None, phm::RecoveryReason::UnknownProcess,
         phm::RecoveryReason::UnknownGroup, phm::RecoveryReason::NotOperational,
         phm::RecoveryReason::AlreadyRestarting, phm::RecoveryReason::InvalidAction,
         phm::RecoveryReason::NoSafeState, phm::RecoveryReason::EmUnavailable,
         phm::RecoveryReason::EmRejected, phm::RecoveryReason::SystemError});
    check_enum_round_trip({phm::EmLifecycleState::Unknown, phm::EmLifecycleState::Running,
                           phm::EmLifecycleState::Terminating, phm::EmLifecycleState::Terminated});
    check_enum_round_trip(
        {phm::WatchdogState::Disabled, phm::WatchdogState::Armed, phm::WatchdogState::Stopped});
    check_enum_round_trip({phm::SecurityEventKind::ClaimRejected});
    NX_CHECK(to_string(static_cast<phm::HealthState>(0x7f)) == "Unknown");
}

void test_health_channel() {
    auto node = com::Node::create();
    NX_CHECK(node.has_value());

    // Health channel: a state transition reaches the daemon-side listener.
    {
        const auto name = com::ServiceName::create("Nexigen/Test/Health");
        NX_CHECK(name.has_value());
        auto channel = phm::HealthChannel::create(*node, *name);
        NX_CHECK(channel.has_value());
        auto listener = com::EventListener::create(*node, *name);
        NX_CHECK(listener.has_value());

        NX_CHECK(channel->set_state(phm::HealthState::Error).has_value());
        NX_CHECK(channel->state() == phm::HealthState::Error);

        bool observed = false;
        const auto collected = listener->try_wait([&](com::EventId id) {
            observed = id.as_value() == static_cast<std::uint64_t>(phm::HealthState::Error);
        });
        NX_CHECK(collected.has_value());
        NX_CHECK(observed);

        const std::string text = channel->to_string();
        NX_CHECK(text.find("Error") != std::string::npos);
        NX_CHECK(std::format("{}", *channel) == text);
    }

    // Checkpoint emission: the emitted id reaches the listener.
    {
        const auto name = com::ServiceName::create("Nexigen/Test/Checkpoint");
        NX_CHECK(name.has_value());
        auto emitter = phm::CheckpointEmitter::create(*node, *name);
        NX_CHECK(emitter.has_value());
        auto listener = com::EventListener::create(*node, *name);
        NX_CHECK(listener.has_value());

        NX_CHECK(emitter->emit(7).has_value());
        bool observed = false;
        const auto collected =
            listener->try_wait([&](com::EventId id) { observed = id.as_value() == 7U; });
        NX_CHECK(collected.has_value());
        NX_CHECK(observed);
        NX_CHECK(emitter->to_string().find("Nexigen/Test/Checkpoint") != std::string::npos);
    }
}

phm::SupervisedProcess make_process(std::string name) {
    phm::SupervisedProcess process;
    process.name = std::move(name);
    process.service_name = "Nexigen/Test/" + process.name;
    process.alive_reference_cycle_ms = 100;
    process.alive_min_margin_ms = 1;
    process.alive_max_margin_ms = 50;
    process.failed_supervision_cycles_tolerance = 1;
    process.checkpoints = {1, 2};
    process.transitions = {{1, 2}};
    process.deadlines = {{1, 2, 10, 100}};
    return process;
}

void test_supervision() {
    // Healthy cycle: checkpoints 1->2 within limits, then an alive tick.
    {
        phm::PhmManifest manifest;
        manifest.processes.push_back(make_process("healthy"));
        phm::Supervisor supervisor(manifest);
        const auto t0 = phm::Supervisor::Clock::now();
        supervisor.on_checkpoint(0, 1, t0);
        supervisor.on_checkpoint(0, 2, t0 + std::chrono::milliseconds(50));
        supervisor.on_alive_tick(0, t0 + std::chrono::milliseconds(100));
        NX_CHECK(supervisor.status(0) == phm::SupervisionStatus::Ok);
    }

    // Alive failure: silence across a tick yields Failed, then Expired.
    {
        phm::PhmManifest manifest;
        manifest.processes.push_back(make_process("silent"));
        phm::Supervisor supervisor(manifest);
        const auto t0 = phm::Supervisor::Clock::now();
        supervisor.on_alive_tick(0, t0 + std::chrono::milliseconds(100));
        NX_CHECK(supervisor.status(0) == phm::SupervisionStatus::Failed);
        supervisor.on_alive_tick(0, t0 + std::chrono::milliseconds(200));
        NX_CHECK(supervisor.status(0) == phm::SupervisionStatus::Expired);
    }

    // A healthy cycle resets a single failure (tolerance semantics).
    {
        phm::PhmManifest manifest;
        manifest.processes.push_back(make_process("recovering"));
        phm::Supervisor supervisor(manifest);
        auto t0 = phm::Supervisor::Clock::now();
        supervisor.on_alive_tick(0, t0 + std::chrono::milliseconds(100));
        NX_CHECK(supervisor.status(0) == phm::SupervisionStatus::Failed);
        supervisor.on_checkpoint(0, 1, t0 + std::chrono::milliseconds(150));
        supervisor.on_checkpoint(0, 2, t0 + std::chrono::milliseconds(200));
        supervisor.on_alive_tick(0, t0 + std::chrono::milliseconds(200));
        NX_CHECK(supervisor.status(0) == phm::SupervisionStatus::Ok);
    }

    // Logical failure: 2->1 is not an allowed transition.
    {
        phm::PhmManifest manifest;
        manifest.processes.push_back(make_process("logic"));
        phm::Supervisor supervisor(manifest);
        const auto t0 = phm::Supervisor::Clock::now();
        supervisor.on_checkpoint(0, 2, t0);
        supervisor.on_checkpoint(0, 1, t0 + std::chrono::milliseconds(5));
        NX_CHECK(supervisor.status(0) == phm::SupervisionStatus::Failed);
    }

    // Deadline failure: target arrives faster than min (early violation).
    {
        phm::PhmManifest manifest;
        manifest.processes.push_back(make_process("early"));
        phm::Supervisor supervisor(manifest);
        const auto t0 = phm::Supervisor::Clock::now();
        supervisor.on_checkpoint(0, 1, t0);
        supervisor.on_checkpoint(0, 2, t0 + std::chrono::milliseconds(2));
        NX_CHECK(supervisor.status(0) == phm::SupervisionStatus::Failed);
    }

    // Deadline failure: target arrives after max (late violation).
    {
        phm::PhmManifest manifest;
        manifest.processes.push_back(make_process("late"));
        phm::Supervisor supervisor(manifest);
        const auto t0 = phm::Supervisor::Clock::now();
        supervisor.on_checkpoint(0, 1, t0);
        supervisor.on_checkpoint(0, 2, t0 + std::chrono::milliseconds(200));
        NX_CHECK(supervisor.status(0) == phm::SupervisionStatus::Failed);
    }

    // Diagnostics render per-process status.
    {
        phm::PhmManifest manifest;
        manifest.processes.push_back(make_process("diag"));
        phm::Supervisor supervisor(manifest);
        const std::string text = supervisor.to_string();
        NX_CHECK(text.find("diag=Ok") != std::string::npos);
        NX_CHECK(to_string(supervisor) == text);
        NX_CHECK(std::format("{}", supervisor) == text);
    }

    // EM lifecycle: Running arms, Terminating/Terminated suspend.
    {
        phm::PhmManifest manifest;
        manifest.processes.push_back(make_process("lifecycle"));
        phm::Supervisor supervisor(manifest);
        const auto t0 = phm::Supervisor::Clock::now();
        supervisor.on_execution_state(0, phm::EmLifecycleState::Running);
        NX_CHECK(supervisor.em_lifecycle(0) == phm::EmLifecycleState::Running);
        supervisor.on_alive_tick(0, t0 + std::chrono::milliseconds(100));
        NX_CHECK(supervisor.status(0) == phm::SupervisionStatus::Failed);

        supervisor.on_execution_state(0, phm::EmLifecycleState::Terminating);
        NX_CHECK(supervisor.status(0) == phm::SupervisionStatus::Ok);
        supervisor.on_alive_tick(0, t0 + std::chrono::milliseconds(200));
        NX_CHECK(supervisor.status(0) == phm::SupervisionStatus::Ok);
        supervisor.on_checkpoint(0, 2, t0 + std::chrono::milliseconds(250));
        NX_CHECK(supervisor.status(0) == phm::SupervisionStatus::Ok);

        supervisor.on_execution_state(0, phm::EmLifecycleState::Terminated);
        supervisor.on_alive_tick(0, t0 + std::chrono::milliseconds(300));
        NX_CHECK(supervisor.status(0) == phm::SupervisionStatus::Ok);
        NX_CHECK(supervisor.lifecycle_notifications() == 3U);
        NX_CHECK(supervisor.to_string().find("lifecycle=3") != std::string::npos);
    }

    // Alive counts: expected indications with inclusive count margins.
    {
        phm::PhmManifest manifest;
        phm::SupervisedProcess process;
        process.name = "counted";
        process.alive_min_margin_ms = 0;
        process.alive_expected_indications = 3;
        process.failed_supervision_cycles_tolerance = 1;
        process.transitions = {{5, 5}};
        manifest.processes.push_back(process);
        phm::Supervisor supervisor(manifest);
        const auto t0 = phm::Supervisor::Clock::now();

        // Too few: two checkpoints where three are expected.
        supervisor.on_checkpoint(0, 5, t0);
        supervisor.on_checkpoint(0, 5, t0 + std::chrono::milliseconds(10));
        supervisor.on_alive_tick(0, t0 + std::chrono::milliseconds(100));
        NX_CHECK(supervisor.status(0) == phm::SupervisionStatus::Failed);

        // Exactly the expected count passes and clears the failure.
        for (int i = 0; i < 3; ++i) {
            supervisor.on_checkpoint(0, 5, t0 + std::chrono::milliseconds(110 + i * 10));
        }
        supervisor.on_alive_tick(0, t0 + std::chrono::milliseconds(200));
        NX_CHECK(supervisor.status(0) == phm::SupervisionStatus::Ok);

        // Too many: four checkpoints exceed expected + max margin (3).
        for (int i = 0; i < 4; ++i) {
            supervisor.on_checkpoint(0, 5, t0 + std::chrono::milliseconds(210 + i * 10));
        }
        supervisor.on_alive_tick(0, t0 + std::chrono::milliseconds(300));
        NX_CHECK(supervisor.status(0) == phm::SupervisionStatus::Failed);

        // Inclusive margins: [2, 4] accepts both ends.
        manifest.processes[0].alive_min_margin_cycles = 1;
        manifest.processes[0].alive_max_margin_cycles = 1;
        phm::Supervisor wide(manifest);
        wide.on_checkpoint(0, 5, t0);
        wide.on_checkpoint(0, 5, t0 + std::chrono::milliseconds(10));
        wide.on_alive_tick(0, t0 + std::chrono::milliseconds(100));
        NX_CHECK(wide.status(0) == phm::SupervisionStatus::Ok);
        for (int i = 0; i < 4; ++i) {
            wide.on_checkpoint(0, 5, t0 + std::chrono::milliseconds(110 + i * 10));
        }
        wide.on_alive_tick(0, t0 + std::chrono::milliseconds(200));
        NX_CHECK(wide.status(0) == phm::SupervisionStatus::Ok);
    }

    // Fork/join: every declared branch must be entered before the join.
    {
        phm::SupervisedProcess process;
        process.name = "forked";
        process.alive_min_margin_ms = 0;
        process.failed_supervision_cycles_tolerance = 0;
        process.checkpoints = {10, 11, 12, 13};
        process.transitions = {{10, 11}, {11, 12}, {12, 13}};
        process.logical_forks = {{10, {11, 12}, 13}};
        phm::PhmManifest manifest;
        manifest.processes.push_back(process);

        // Complete fork: both branches entered, join accepted.
        {
            phm::Supervisor supervisor(manifest);
            const auto t0 = phm::Supervisor::Clock::now();
            supervisor.on_checkpoint(0, 10, t0);
            supervisor.on_checkpoint(0, 11, t0 + std::chrono::milliseconds(10));
            supervisor.on_checkpoint(0, 12, t0 + std::chrono::milliseconds(20));
            supervisor.on_checkpoint(0, 13, t0 + std::chrono::milliseconds(30));
            NX_CHECK(supervisor.status(0) == phm::SupervisionStatus::Ok);
        }

        // Missing branch: join reached with one branch unentered.
        {
            phm::PhmManifest sparse = manifest;
            sparse.processes[0].transitions = {{10, 11}, {11, 13}};
            phm::Supervisor supervisor(sparse);
            const auto t0 = phm::Supervisor::Clock::now();
            supervisor.on_checkpoint(0, 10, t0);
            supervisor.on_checkpoint(0, 11, t0 + std::chrono::milliseconds(10));
            supervisor.on_checkpoint(0, 13, t0 + std::chrono::milliseconds(20));
            NX_CHECK(supervisor.status(0) != phm::SupervisionStatus::Ok);
        }

        // Out-of-order join: the inner join never opened its frame.
        {
            phm::PhmManifest nested = manifest;
            nested.processes[0].checkpoints = {10, 11, 12, 13, 20, 21, 22};
            nested.processes[0].logical_forks = {{10, {11}, 12}, {20, {21}, 22}};
            nested.processes[0].transitions = {{10, 20}, {20, 21}, {21, 12}};
            phm::Supervisor supervisor(nested);
            const auto t0 = phm::Supervisor::Clock::now();
            supervisor.on_checkpoint(0, 10, t0);
            supervisor.on_checkpoint(0, 20, t0 + std::chrono::milliseconds(10));
            supervisor.on_checkpoint(0, 21, t0 + std::chrono::milliseconds(20));
            supervisor.on_checkpoint(0, 12, t0 + std::chrono::milliseconds(30));
            NX_CHECK(supervisor.status(0) != phm::SupervisionStatus::Ok);
        }

        // Nested forks unwind in order.
        {
            phm::PhmManifest nested = manifest;
            nested.processes[0].checkpoints = {10, 11, 12, 20, 21, 22};
            nested.processes[0].logical_forks = {{10, {11}, 12}, {20, {21}, 22}};
            nested.processes[0].transitions = {{10, 20}, {20, 21}, {21, 22}, {22, 11}, {11, 12}};
            phm::Supervisor supervisor(nested);
            const auto t0 = phm::Supervisor::Clock::now();
            supervisor.on_checkpoint(0, 10, t0);
            supervisor.on_checkpoint(0, 20, t0 + std::chrono::milliseconds(10));
            supervisor.on_checkpoint(0, 21, t0 + std::chrono::milliseconds(20));
            supervisor.on_checkpoint(0, 22, t0 + std::chrono::milliseconds(30));
            supervisor.on_checkpoint(0, 11, t0 + std::chrono::milliseconds(40));
            supervisor.on_checkpoint(0, 12, t0 + std::chrono::milliseconds(50));
            NX_CHECK(supervisor.status(0) == phm::SupervisionStatus::Ok);
        }

        // A process without forks is unaffected.
        {
            phm::Supervisor supervisor(manifest);
            const auto t0 = phm::Supervisor::Clock::now();
            supervisor.on_checkpoint(0, 10, t0);
            supervisor.on_checkpoint(0, 11, t0 + std::chrono::milliseconds(10));
            NX_CHECK(supervisor.status(0) == phm::SupervisionStatus::Ok);
        }
    }

    // Hot path with eight declared forks: allocation-free and bounded.
    {
        phm::SupervisedProcess process;
        process.name = "bounded";
        process.alive_min_margin_ms = 0;
        process.failed_supervision_cycles_tolerance = 1;
        for (std::uint32_t i = 0; i < phm::k_max_logical_forks; ++i) {
            const std::uint32_t fork = 4U * i + 1U;
            process.logical_forks.push_back({fork, {fork + 1U, fork + 2U}, fork + 3U});
            process.checkpoints.push_back(fork);
            process.checkpoints.push_back(fork + 1U);
            process.checkpoints.push_back(fork + 2U);
            process.checkpoints.push_back(fork + 3U);
            process.transitions.push_back({fork, fork + 1U});
            process.transitions.push_back({fork + 1U, fork + 2U});
            process.transitions.push_back({fork + 2U, fork + 3U});
            if (i + 1U < phm::k_max_logical_forks) {
                process.transitions.push_back({fork + 3U, fork + 4U});
            }
        }
        process.transitions.push_back({4U * phm::k_max_logical_forks, 1U});
        phm::PhmManifest manifest;
        manifest.processes.push_back(process);
        phm::Supervisor supervisor(manifest);
        const auto t0 = phm::Supervisor::Clock::now();
        const std::size_t before = g_alloc_used;
        for (int cycle = 0; cycle < 1000; ++cycle) {
            for (std::uint32_t id = 1; id <= 4U * phm::k_max_logical_forks; ++id) {
                supervisor.on_checkpoint(
                    0, id, t0 + std::chrono::microseconds(cycle * 32 + static_cast<int>(id)));
            }
        }
        NX_CHECK(g_alloc_used == before);
        NX_CHECK(supervisor.status(0) == phm::SupervisionStatus::Ok);
    }
}

void test_recovery_restart() {
    auto node = com::Node::create();
    NX_CHECK(node.has_value());
    const auto em_name = com::ServiceName::create("Nexigen/Test/Recovery");
    const auto sm_notif_name = com::ServiceName::create("Nexigen/Test/SmRecovery");
    const auto sm_event_name = com::ServiceName::create("Nexigen/Test/SmRecoveryEvent");
    NX_CHECK(em_name.has_value() && sm_notif_name.has_value() && sm_event_name.has_value());

    // Mock EM responder: accepts every request and answers accepted=1.
    auto client = phm::RecoveryClient::create(*node, *em_name, *sm_notif_name, *sm_event_name);
    NX_CHECK(client.has_value());
    auto server = com::Server<phm::RecoveryRequest, phm::RecoveryResponse>::create(*node, *em_name);
    NX_CHECK(server.has_value());
    std::thread mock_em([&server] {
        for (int i = 0; i < 200; ++i) {
            auto request = server->receive();
            if (request.has_value() && request->has_value()) {
                NX_CHECK((*request)
                             ->send_copy(phm::RecoveryResponse{
                                 (*request)->payload().request_id,
                                 phm::RecoveryOutcome::Accepted, phm::RecoveryReason::None})
                             .has_value());
                return;
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(5));
        }
    });

    const auto restart = client->restart_process(42, "camera", std::chrono::milliseconds(2000));
    mock_em.join();
    NX_CHECK(restart.has_value());
    NX_CHECK(restart->outcome == phm::RecoveryOutcome::Accepted);
    NX_CHECK(restart->request_id != 0U);
    NX_CHECK(client->last_action() == phm::RecoveryAction::RestartProcess);
    const std::string text = client->to_string();
    NX_CHECK(text.find("RestartProcess") != std::string::npos);
    NX_CHECK(std::format("{}", *client) == text);

    // SafeState escalation publishes to the SM channel, not to the EM.
    phm::SupervisedProcess process;
    process.name = "steering";
    process.safety_critical = true;
    process.recovery_rules.push_back(
        {phm::RecoveryAction::SwitchFunctionGroupState, "motion_control"});
    auto sm_subscriber =
        com::Subscriber<nexigen::sm::RecoveryNotification>::create(*node, *sm_notif_name);
    auto sm_listener = com::EventListener::create(*node, *sm_event_name);
    NX_CHECK(sm_subscriber.has_value() && sm_listener.has_value());

    NX_CHECK(client->apply_rule(process, "motion_control", std::chrono::milliseconds(2000)).has_value());
    NX_CHECK(client->last_action() == phm::RecoveryAction::SwitchFunctionGroupState);

    bool observed = false;
    for (int i = 0; i < 200 && !observed; ++i) {
        auto received = sm_subscriber->receive();
        NX_CHECK(received.has_value());
        if (received->has_value()) {
            observed = std::string((*received)->payload().function_group) == "motion_control";
        } else {
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
        }
    }
    NX_CHECK(observed);
    bool woken = false;
    const auto collected = sm_listener->try_wait([&](com::EventId) { woken = true; });
    NX_CHECK(collected.has_value());
    NX_CHECK(woken);
}

void test_recovery_watchdog() {
    auto node = com::Node::create();
    NX_CHECK(node.has_value());
    const auto em_name = com::ServiceName::create("Nexigen/Test/RecoveryWatchdog");
    const auto sm_notif_name = com::ServiceName::create("Nexigen/Test/SmWatchdog");
    const auto sm_event_name = com::ServiceName::create("Nexigen/Test/SmWatchdogEvent");
    NX_CHECK(em_name.has_value() && sm_notif_name.has_value() && sm_event_name.has_value());
    auto client = phm::RecoveryClient::create(*node, *em_name, *sm_notif_name, *sm_event_name);
    NX_CHECK(client.has_value());

    // No EM server exists on this channel: a watchdog action that tried the
    // transport would time out instead of firing the callback.
    phm::SupervisedProcess process;
    process.name = "brake";
    process.recovery_rules.push_back({phm::RecoveryAction::TriggerWatchdogReset, ""});
    bool stopped = false;
    const auto status =
        client->apply_rule(process, "motion_control", std::chrono::milliseconds(50),
                          [&stopped] { stopped = true; });
    NX_CHECK(status.has_value());
    NX_CHECK(stopped);
    NX_CHECK(client->last_action() == phm::RecoveryAction::TriggerWatchdogReset);
}

void test_recovery_rejected() {
    auto node = com::Node::create();
    NX_CHECK(node.has_value());
    const auto em_name = com::ServiceName::create("Nexigen/Test/RecoveryReject");
    const auto sm_notif_name = com::ServiceName::create("Nexigen/Test/SmReject");
    const auto sm_event_name = com::ServiceName::create("Nexigen/Test/SmRejectEvent");
    NX_CHECK(em_name.has_value() && sm_notif_name.has_value() && sm_event_name.has_value());
    auto client = phm::RecoveryClient::create(*node, *em_name, *sm_notif_name, *sm_event_name);
    NX_CHECK(client.has_value());
    auto server = com::Server<phm::RecoveryRequest, phm::RecoveryResponse>::create(*node, *em_name);
    NX_CHECK(server.has_value());
    auto subscriber =
        com::Subscriber<nexigen::sm::RecoveryNotification>::create(*node, *sm_notif_name);
    NX_CHECK(subscriber.has_value());

    std::thread reject_em([&server] {
        for (int i = 0; i < 200; ++i) {
            auto request = server->receive();
            if (request.has_value() && request->has_value()) {
                NX_CHECK((*request)
                             ->send_copy(phm::RecoveryResponse{
                                 (*request)->payload().request_id, phm::RecoveryOutcome::Rejected,
                                 phm::RecoveryReason::AlreadyRestarting})
                             .has_value());
                return;
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(5));
        }
    });

    phm::SupervisedProcess process;
    process.name = "steering";
    process.recovery_rules = {{phm::RecoveryAction::RestartProcess, ""},
                              {phm::RecoveryAction::SwitchFunctionGroupState, "motion_control"}};
    NX_CHECK(client->apply_rule(process, "motion_control", std::chrono::milliseconds(2000)).has_value());
    reject_em.join();
    NX_CHECK(client->last_action() == phm::RecoveryAction::SwitchFunctionGroupState);

    int notifications = 0;
    for (int i = 0; i < 200; ++i) {
        auto received = subscriber->receive();
        NX_CHECK(received.has_value());
        if (received->has_value()) {
            ++notifications;
            continue;
        }
        if (notifications > 0) {
            break;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }
    NX_CHECK(notifications == 1);
}

void test_recovery_timeout() {
    auto node = com::Node::create();
    NX_CHECK(node.has_value());
    const auto em_name = com::ServiceName::create("Nexigen/Test/RecoveryDead");
    const auto sm_notif_name = com::ServiceName::create("Nexigen/Test/SmDead");
    const auto sm_event_name = com::ServiceName::create("Nexigen/Test/SmDeadEvent");
    NX_CHECK(em_name.has_value() && sm_notif_name.has_value() && sm_event_name.has_value());
    auto client = phm::RecoveryClient::create(*node, *em_name, *sm_notif_name, *sm_event_name);
    NX_CHECK(client.has_value());
    auto subscriber =
        com::Subscriber<nexigen::sm::RecoveryNotification>::create(*node, *sm_notif_name);
    NX_CHECK(subscriber.has_value());

    phm::SupervisedProcess process;
    process.name = "throttle";
    process.recovery_rules = {{phm::RecoveryAction::RestartProcess, ""},
                              {phm::RecoveryAction::SwitchFunctionGroupState, "powertrain"}};
    NX_CHECK(client->apply_rule(process, "powertrain", std::chrono::milliseconds(50)).has_value());
    NX_CHECK(client->last_action() == phm::RecoveryAction::SwitchFunctionGroupState);

    bool observed = false;
    for (int i = 0; i < 200 && !observed; ++i) {
        auto received = subscriber->receive();
        NX_CHECK(received.has_value());
        if (received->has_value()) {
            observed = std::string((*received)->payload().function_group) == "powertrain";
        } else {
            std::this_thread::sleep_for(std::chrono::milliseconds(5));
        }
    }
    NX_CHECK(observed);
}


void test_recovery_ack_budget() {
    using Clock = phm::RecoveryAckTracker::Clock;
    const auto start = Clock::now();
    const auto budget = std::chrono::milliseconds(100);

    // Accepted clears the entry and demands nothing.
    {
        phm::RecoveryAckTracker tracker;
        NX_CHECK(tracker.expect(7, "motion", start + budget));
        NX_CHECK(tracker.pending() == 1U);
        const auto decision = tracker.on_ack(
            phm::RecoveryAck{7, phm::RecoveryOutcome::Accepted, phm::RecoveryReason::None});
        NX_CHECK(decision.matched);
        NX_CHECK(!decision.watchdog_reaction);
        NX_CHECK(tracker.pending() == 0U);
        NX_CHECK(!tracker.expire(start + budget * 2));
    }

    // Rejected demands the watchdog immediately.
    {
        phm::RecoveryAckTracker tracker;
        NX_CHECK(tracker.expect(8, "motion", start + budget));
        const auto decision = tracker.on_ack(
            phm::RecoveryAck{8, phm::RecoveryOutcome::Rejected, phm::RecoveryReason::NoSafeState});
        NX_CHECK(decision.matched);
        NX_CHECK(decision.watchdog_reaction);
        NX_CHECK(tracker.pending() == 0U);
    }

    // InProgress buys no extra time: the deadline still fires.
    {
        phm::RecoveryAckTracker tracker;
        NX_CHECK(tracker.expect(9, "motion", start + budget));
        const auto decision = tracker.on_ack(
            phm::RecoveryAck{9, phm::RecoveryOutcome::InProgress, phm::RecoveryReason::None});
        NX_CHECK(decision.matched);
        NX_CHECK(!decision.watchdog_reaction);
        NX_CHECK(tracker.pending() == 1U);
        NX_CHECK(!tracker.expire(start + budget / 2));
        NX_CHECK(tracker.expire(start + budget));
        NX_CHECK(tracker.pending() == 0U);
    }

    // An absent acknowledgement expires into the watchdog.
    {
        phm::RecoveryAckTracker tracker;
        NX_CHECK(tracker.expect(10, "motion", start + budget));
        NX_CHECK(!tracker.expire(start));
        NX_CHECK(tracker.expire(start + budget + std::chrono::milliseconds(1)));
        NX_CHECK(tracker.pending() == 0U);
    }

    // Stale ids do not match and do not clear a live entry.
    {
        phm::RecoveryAckTracker tracker;
        NX_CHECK(tracker.expect(11, "motion", start + budget));
        const auto stale = tracker.on_ack(
            phm::RecoveryAck{12, phm::RecoveryOutcome::Accepted, phm::RecoveryReason::None});
        NX_CHECK(!stale.matched);
        NX_CHECK(tracker.pending() == 1U);
    }

    // Concurrent escalations are distinguished by id.
    {
        phm::RecoveryAckTracker tracker;
        NX_CHECK(tracker.expect(13, "motion", start + budget));
        NX_CHECK(tracker.expect(14, "powertrain", start + budget));
        NX_CHECK(tracker.pending() == 2U);
        const auto first = tracker.on_ack(
            phm::RecoveryAck{13, phm::RecoveryOutcome::Accepted, phm::RecoveryReason::None});
        NX_CHECK(first.matched);
        NX_CHECK(tracker.pending() == 1U);
        NX_CHECK(tracker.on_ack(phm::RecoveryAck{14, phm::RecoveryOutcome::Accepted,
                                                 phm::RecoveryReason::None})
                      .matched);
    }
}

void test_recovery_storage() {
    phm::FaultStorage storage("/tmp/opencode/phm_fault_test.log");
    for (std::uint32_t i = 0; i < 3; ++i) {
        storage.record(phm::FaultRecord{phm::FaultKind::AliveFailure, 0, i, 1000 + i});
    }
    for (int i = 0; i < 200 && storage.recorded() < 3U; ++i) {
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    NX_CHECK(storage.recorded() == 3U);
    NX_CHECK(storage.dropped() == 0U);
    NX_CHECK(storage.to_string().find("recorded=3") != std::string::npos);
}

void test_recovery() {
    test_recovery_restart();
    test_recovery_watchdog();
    test_recovery_rejected();
    test_recovery_timeout();
    test_recovery_storage();
}

void test_supervision_exhaustion() {
    phm::PhmManifest manifest;
    phm::SupervisedProcess process = make_process("brake");
    manifest.processes.push_back(process);
    phm::Supervisor supervisor(std::move(manifest));
    const auto now = phm::Supervisor::Clock::now();

    // No lifecycle event yet: the group is unknown.
    NX_CHECK(supervisor.function_group(0).empty());

    supervisor.on_execution_state(0, phm::EmLifecycleState::Running, "motion_control");
    NX_CHECK(supervisor.function_group(0) == "motion_control");
    NX_CHECK(supervisor.status(0) == phm::SupervisionStatus::Ok);

    // A logical violation is journaled as a logical fault, not as aliveness.
    NX_CHECK(process.transitions.size() == 1U);
    supervisor.on_checkpoint(0, 2U, now);
    NX_CHECK(supervisor.last_fault(0) == phm::FaultKind::AliveFailure);
    supervisor.on_checkpoint(0, 1U, now + std::chrono::milliseconds(200));
    NX_CHECK(supervisor.last_fault(0) == phm::FaultKind::LogicalFailure);

    // A spent budget is terminal: the process is Exhausted and ignores
    // further checkpoints and ticks.
    supervisor.on_execution_state(0, phm::EmLifecycleState::Failed, "motion_control");
    NX_CHECK(supervisor.status(0) == phm::SupervisionStatus::Exhausted);
    NX_CHECK(supervisor.exhausted(0));
    supervisor.on_checkpoint(0, 1U, now + std::chrono::milliseconds(400));
    supervisor.on_alive_tick(0, now + std::chrono::milliseconds(500));
    NX_CHECK(supervisor.status(0) == phm::SupervisionStatus::Exhausted);

    // A later Running re-arms supervision.
    supervisor.on_execution_state(0, phm::EmLifecycleState::Running, "motion_control");
    NX_CHECK(supervisor.status(0) == phm::SupervisionStatus::Ok);
    NX_CHECK(!supervisor.exhausted(0));
}

void test_manifest() {
    phm::PhmManifest manifest;
    phm::SupervisedProcess process = make_process("camera");
    process.recovery_rules.push_back({phm::RecoveryAction::RestartProcess, "ignored_group"});
    manifest.processes.push_back(process);
    manifest.bg_thread.set_cpus(nexigen::core::CpuSet::from_core_mask(0b10ULL));
    NX_CHECK(manifest.bg_thread.set_name("phm-bg").has_value());
    manifest.fault_storage_thread.set_cpus(nexigen::core::CpuSet::from_core_mask(1ULL));
    NX_CHECK(manifest.fault_storage_thread.set_name("phm-faults").has_value());

    nlohmann::json j;
    to_json(j, manifest);
    phm::PhmManifest back;
    from_json(j, back);
    NX_CHECK(back.processes.size() == 1U);
    NX_CHECK(back.processes[0].name == "camera");
    NX_CHECK(back.processes[0].service_name == "Nexigen/Test/camera");
    NX_CHECK(back.processes[0].alive_reference_cycle_ms == 100U);
    NX_CHECK(back.processes[0].failed_supervision_cycles_tolerance == 1U);
    NX_CHECK(back.processes[0].checkpoints.size() == 2U);
    NX_CHECK(back.processes[0].transitions.size() == 1U);
    NX_CHECK(back.processes[0].transitions[0].from_id == 1U);
    NX_CHECK(back.processes[0].transitions[0].to_id == 2U);
    NX_CHECK(back.processes[0].deadlines.size() == 1U);
    NX_CHECK(back.processes[0].deadlines[0].min_ms == 10U);
    NX_CHECK(back.processes[0].deadlines[0].max_ms == 100U);
    NX_CHECK(back.processes[0].recovery_rules.size() == 1U);
    NX_CHECK(back.processes[0].recovery_rules[0].action == phm::RecoveryAction::RestartProcess);
    NX_CHECK(back.bg_thread == manifest.bg_thread);
    NX_CHECK(back.fault_storage_thread == manifest.fault_storage_thread);

    // Backward compatibility: an old manifest without thread fields parses
    // with empty specs (inherited attributes).
    phm::PhmManifest legacy;
    from_json(nlohmann::json{{"processes", nlohmann::json::array()}}, legacy);
    NX_CHECK(!legacy.bg_thread.has_requests());
    NX_CHECK(!legacy.fault_storage_thread.has_requests());

    // Every manifest diagnostic is part of the public contract.
    NX_CHECK(manifest.processes[0].to_string().find("camera") != std::string::npos);
    NX_CHECK(phm::CheckpointTransition{1, 2}.to_string().find("from=1") != std::string::npos);
    NX_CHECK(phm::DeadlinePair{1, 2, 10, 100}.to_string().find("min_ms=10") != std::string::npos);
    NX_CHECK(phm::RecoveryRule{phm::RecoveryAction::RestartProcess, "group"}.to_string().find(
                 "RestartProcess") != std::string::npos);

    // A second process exercises the separator in the manifest diagnostic.
    phm::PhmManifest two;
    two.processes.push_back(make_process("alpha"));
    two.processes.push_back(make_process("beta"));
    NX_CHECK(two.to_string().find("alpha, beta") != std::string::npos);

    // Non-object JSON leaves each element at its defaults.
    phm::CheckpointTransition transition{};
    phm::from_json(nlohmann::json(7), transition);
    NX_CHECK(transition.from_id == 0U && transition.to_id == 0U);
    phm::DeadlinePair pair{};
    phm::from_json(nlohmann::json(7), pair);
    NX_CHECK(pair.source_id == 0U && pair.max_ms == 0U);
    phm::RecoveryRule rule{};
    phm::from_json(nlohmann::json(7), rule);
    NX_CHECK(rule.function_group.empty());

    // A malformed checkpoints array rejects the process parse.
    phm::SupervisedProcess malformed;
    phm::from_json(
        nlohmann::json{{"name", "bad"}, {"checkpoints", nlohmann::json::array({1, "nope"})}},
        malformed);
    NX_CHECK(malformed.checkpoints.empty());

    const std::string text = manifest.to_string();
    NX_CHECK(text.find("camera") != std::string::npos);
    NX_CHECK(text.find("fault_storage_thread") != std::string::npos);
    NX_CHECK(to_string(manifest) == text);
    NX_CHECK(std::format("{}", manifest) == text);

    // Hostile input leaves the target unchanged.
    from_json(nlohmann::json::array(), manifest);
    NX_CHECK(manifest.processes.size() == 1U);
    from_json(nlohmann::json{{"processes", nlohmann::json::array({42})}}, manifest);
    NX_CHECK(manifest.processes.size() == 1U);

    // Safety sections round-trip: alive counts, fork regions, watchdog.
    {
        phm::PhmManifest extended;
        phm::SupervisedProcess entry = make_process("extended");
        entry.alive_expected_indications = 4;
        entry.alive_min_margin_cycles = 1;
        entry.alive_max_margin_cycles = 2;
        entry.logical_forks.push_back({7, {8, 9}, 10});
        entry.expected_uid = 1000U;
        extended.processes.push_back(entry);
        extended.watchdog.enabled = true;
        extended.watchdog.device = "/dev/watchdog";
        extended.watchdog.timeout_ms = 1000;
        extended.watchdog.feed_period_ms = 200;

        nlohmann::json encoded;
        to_json(encoded, extended);
        phm::PhmManifest restored;
        from_json(encoded, restored);
        NX_CHECK(restored.processes[0].alive_expected_indications == 4);
        NX_CHECK(restored.processes[0].alive_min_margin_cycles == 1);
        NX_CHECK(restored.processes[0].alive_max_margin_cycles == 2);
        NX_CHECK(restored.processes[0].logical_forks.size() == 1U);
        NX_CHECK(restored.processes[0].logical_forks[0].fork_id == 7U);
        NX_CHECK(restored.processes[0].logical_forks[0].branch_entries.size() == 2U);
        NX_CHECK(restored.processes[0].logical_forks[0].branch_entries[1] == 9U);
        NX_CHECK(restored.processes[0].logical_forks[0].join_id == 10U);
        NX_CHECK(restored.processes[0].expected_uid.has_value());
        NX_CHECK(*restored.processes[0].expected_uid == 1000U);
        NX_CHECK(encoded["processes"][0].contains("expected_uid"));
        NX_CHECK(restored.watchdog.enabled);
        NX_CHECK(restored.watchdog.device == "/dev/watchdog");
        NX_CHECK(restored.watchdog.timeout_ms == 1000U);
        NX_CHECK(restored.watchdog.feed_period_ms == 200U);
        std::string watchdog_error;
        phm::PhmManifest watchdog_only;
        watchdog_only.watchdog = extended.watchdog;
        NX_CHECK(phm::validate_phm_manifest(watchdog_only, watchdog_error).has_value());
        NX_CHECK(back.to_string().find("watchdog=") != std::string::npos);
        NX_CHECK(std::format("{}", restored.watchdog).find("timeout_ms=1000") != std::string::npos);
    }

    // Recovery acknowledgement budget: default, explicit value, rejection.
    {
        phm::PhmManifest defaulted;
        NX_CHECK(defaulted.recovery_timeout_ms == 1000U);
        defaulted.processes.push_back(make_process("budget"));
        std::string budget_error;
        NX_CHECK(phm::validate_phm_manifest(defaulted, budget_error).has_value());

        defaulted.recovery_timeout_ms = 2500;
        nlohmann::json encoded;
        to_json(encoded, defaulted);
        phm::PhmManifest restored;
        from_json(encoded, restored);
        NX_CHECK(restored.recovery_timeout_ms == 2500U);
        NX_CHECK(defaulted.to_string().find("recovery_timeout_ms=2500") != std::string::npos);

        phm::PhmManifest zero;
        zero.processes.push_back(make_process("budget"));
        zero.recovery_timeout_ms = 0;
        std::string zero_error;
        NX_CHECK(!phm::validate_phm_manifest(zero, zero_error).has_value());
        NX_CHECK(zero_error.find("recovery_timeout_ms") != std::string::npos);
    }

    // Validation rejects each unsafe section and names the process.
    {
        std::string error;
        phm::PhmManifest valid;
        phm::SupervisedProcess entry;
        entry.name = "gate";
        entry.checkpoints = {1, 2, 3, 4};
        entry.transitions = {{1, 2}, {2, 3}, {3, 4}};
        entry.logical_forks = {{1, {2, 3}, 4}};
        valid.processes.push_back(entry);
        NX_CHECK(phm::validate_phm_manifest(valid, error).has_value());
        NX_CHECK(error.empty());

        auto rejects = [](phm::PhmManifest candidate, const char* needle) {
            std::string message;
            const auto status = phm::validate_phm_manifest(candidate, message);
            return !status.has_value() && message.find(needle) != std::string::npos;
        };

        phm::PhmManifest negative = valid;
        negative.processes[0].alive_expected_indications = -1;
        NX_CHECK(rejects(negative, "gate"));

        phm::PhmManifest zero_uid = valid;
        zero_uid.processes[0].expected_uid = 0U;
        NX_CHECK(rejects(zero_uid, "gate"));

        // A gated entry must have a name that fits the claim buffer; otherwise
        // its claim would always be truncated and it could never verify.
        phm::PhmManifest long_name = valid;
        long_name.processes[0].expected_uid = 7U;
        long_name.processes[0].name = std::string(phm::k_claim_name_length, 'n');
        NX_CHECK(rejects(long_name, "expected_uid requires"));

        phm::PhmManifest empty_name = valid;
        empty_name.processes[0].expected_uid = 7U;
        empty_name.processes[0].name.clear();
        NX_CHECK(rejects(empty_name, "expected_uid requires"));

        phm::PhmManifest max_name = valid;
        max_name.processes[0].expected_uid = 7U;
        max_name.processes[0].name = std::string(phm::k_claim_name_length - 1U, 'm');
        NX_CHECK(phm::validate_phm_manifest(max_name, error).has_value());

        phm::PhmManifest negative_json;
        from_json(nlohmann::json{{"processes",
                                  nlohmann::json::array(
                                      {{{"name", "neg"}, {"alive_max_margin_cycles", -3}}})}},
                  negative_json);
        NX_CHECK(rejects(negative_json, "neg"));

        phm::PhmManifest bad_branch = valid;
        bad_branch.processes[0].logical_forks[0].join_id = 99;
        NX_CHECK(rejects(bad_branch, "gate"));

        phm::PhmManifest unknown_branch = valid;
        unknown_branch.processes[0].logical_forks[0].branch_entries = {42};
        NX_CHECK(rejects(unknown_branch, "gate"));

        phm::PhmManifest empty_branch = valid;
        empty_branch.processes[0].logical_forks[0].branch_entries.clear();
        NX_CHECK(rejects(empty_branch, "gate"));

        phm::PhmManifest duplicate = valid;
        duplicate.processes[0].logical_forks.push_back({1, {2}, 3});
        NX_CHECK(rejects(duplicate, "gate"));

        phm::PhmManifest too_many = valid;
        too_many.processes[0].logical_forks.clear();
        too_many.processes[0].checkpoints.clear();
        for (std::uint32_t i = 0; i < phm::k_max_logical_forks + 1U; ++i) {
            const std::uint32_t fork = 100U + i * 3U;
            too_many.processes[0].logical_forks.push_back({fork, {fork + 1U}, fork + 2U});
            too_many.processes[0].checkpoints.push_back(fork);
            too_many.processes[0].checkpoints.push_back(fork + 1U);
            too_many.processes[0].checkpoints.push_back(fork + 2U);
        }
        NX_CHECK(rejects(too_many, "gate"));

        phm::PhmManifest slow_feed = valid;
        slow_feed.watchdog.enabled = true;
        slow_feed.watchdog.timeout_ms = 1000;
        slow_feed.watchdog.feed_period_ms = 600;
        NX_CHECK(rejects(slow_feed, "watchdog"));

        phm::PhmManifest zero_timeout = valid;
        zero_timeout.watchdog.enabled = true;
        zero_timeout.watchdog.timeout_ms = 0;
        NX_CHECK(rejects(zero_timeout, "watchdog"));
    }
}

void test_watchdog() {
    using namespace std::chrono;

    // Disabled by default: no device is touched.
    {
        nexigen::test::FakeWatchdogDevice device;
        auto created = phm::Watchdog::create(device, phm::WatchdogConfig{});
        NX_CHECK(created.has_value());
        NX_CHECK(!created->enabled());
        NX_CHECK(created->state() == phm::WatchdogState::Disabled);
        created->tick(phm::Watchdog::Clock::now(), false);
        NX_CHECK(device.opens == 0);
        NX_CHECK(device.feeds == 0);
    }

    phm::WatchdogConfig config;
    config.enabled = true;
    config.device = "/dev/watchdog";
    config.timeout_ms = 1000;
    config.feed_period_ms = 100;

    // Armed: the device is opened and the timeout negotiated.
    {
        nexigen::test::FakeWatchdogDevice device;
        auto created = phm::Watchdog::create(device, config);
        NX_CHECK(created.has_value());
        NX_CHECK(created->enabled());
        NX_CHECK(created->state() == phm::WatchdogState::Armed);
        NX_CHECK(created->timeout_ms() == 1000U);
        NX_CHECK(created->feeds() == 0U);
        NX_CHECK(device.opens == 1);
        NX_CHECK(device.path() == "/dev/watchdog");

        const auto t0 = phm::Watchdog::Clock::now();
        created->tick(t0, false);                     // first feed
        created->tick(t0 + milliseconds(100), false); // period elapsed
        created->tick(t0 + milliseconds(150), false); // too soon
        NX_CHECK(created->feeds() == 2U);

        created->tick(t0 + milliseconds(160), true); // expired stops the feed
        NX_CHECK(created->state() == phm::WatchdogState::Stopped);
        NX_CHECK(!created->feeding());
        NX_CHECK(created->feeds() == 2U);

        created->tick(t0 + milliseconds(200), false); // healthy resumes
        NX_CHECK(created->state() == phm::WatchdogState::Armed);
        NX_CHECK(created->feeds() == 3U);

        created->tick(t0 + milliseconds(700), false); // stalled loop: skip
        NX_CHECK(created->feeds() == 3U);
        created->tick(t0 + milliseconds(800), false); // clean cycle feeds
        NX_CHECK(created->feeds() == 4U);

        const std::string text = created->to_string();
        NX_CHECK(text.find("timeout_ms=1000") != std::string::npos);
        NX_CHECK(text.find("feeds=4") != std::string::npos);
        NX_CHECK(text.find("Armed") != std::string::npos);
        NX_CHECK(std::format("{}", *created) == text);

        created->stop_feeding();
        NX_CHECK(!created->feeding());
        NX_CHECK(created->state() == phm::WatchdogState::Stopped);
        created->disarm();
        NX_CHECK(device.disarms >= 1);
    }

    // The driver may grant a shorter timeout than requested.
    {
        nexigen::test::FakeWatchdogDevice device;
        device.max_granted = 500;
        auto created = phm::Watchdog::create(device, config);
        NX_CHECK(created.has_value());
        NX_CHECK(created->timeout_ms() == 500U);
        created->disarm();
    }

    // An enabled watchdog that cannot open its device aborts creation.
    {
        nexigen::test::FakeWatchdogDevice device;
        device.fail_open = true;
        const auto created = phm::Watchdog::create(device, config);
        NX_CHECK(!created.has_value());
    }

    // Real-device smoke test is opt-in; a developer machine must not reset.
    {
        const char* path = std::getenv("NEXIGEN_WATCHDOG_DEVICE");
        if (path == nullptr || ::access(path, F_OK) != 0) {
            std::printf("[watchdog-real       ] skipped (set NEXIGEN_WATCHDOG_DEVICE)\n");
        } else {
            phm::PosixWatchdogDevice device;
            NX_CHECK(device.open(path).has_value());
            const auto granted = device.set_timeout(2000);
            NX_CHECK(granted.has_value());
            NX_CHECK(device.feed().has_value());
            NX_CHECK(device.disarm().has_value());
        }
    }
}

std::string read_text_file(const std::string& path) {
    std::FILE* file = std::fopen(path.c_str(), "rb");
    if (file == nullptr) {
        return {};
    }
    std::string text;
    std::array<char, 4096> buffer{};
    for (;;) {
        const std::size_t count = std::fread(buffer.data(), 1, buffer.size(), file);
        text.append(buffer.data(), count);
        if (count < buffer.size()) {
            break;
        }
    }
    static_cast<void>(std::fclose(file));
    return text;
}

void write_text_file(const std::string& path, std::string_view text) {
    std::FILE* file = std::fopen(path.c_str(), "wb");
    if (file == nullptr) {
        return;
    }
    static_cast<void>(std::fwrite(text.data(), 1, text.size(), file));
    static_cast<void>(std::fclose(file));
}

void test_fault_log() {
    const std::string base =
        "/tmp/opencode/phm_fault_log_" + std::to_string(static_cast<long>(::getpid()));
    const std::string intact_path = base + "_intact.log";
    static_cast<void>(std::remove(intact_path.c_str()));

    {
        phm::FaultStorage storage(intact_path);
        for (std::uint32_t i = 0; i < 3; ++i) {
            storage.record(phm::FaultRecord{phm::FaultKind::AliveFailure, i, i + 1, 1000 + i});
        }
        for (int i = 0; i < 200 && storage.recorded() < 3U; ++i) {
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
        }
        NX_CHECK(storage.recorded() == 3U);
    }

    // Written records carry sequence and checksum and verify intact.
    {
        phm::FaultLogReport report{};
        NX_CHECK(phm::verify_fault_log(intact_path, report).has_value());
        NX_CHECK(report.intact == 3U);
        NX_CHECK(report.corrupted == 0U);
        NX_CHECK(report.truncated == 0U);
        NX_CHECK(report.legacy == 0U);
        NX_CHECK(report.to_string().find("intact=3") != std::string::npos);
        NX_CHECK(std::format("{}", report) == report.to_string());
    }

    const std::string text = read_text_file(intact_path);
    NX_CHECK(!text.empty());

    // One flipped checksum character is reported corrupted; the scan continues.
    {
        std::string corrupted = text;
        const std::size_t newline = corrupted.find('\n');
        NX_CHECK(newline != std::string::npos && newline > 0U);
        char& last = corrupted[newline - 1U];
        last = last == '0' ? '1' : '0';
        const std::string path = base + "_corrupt.log";
        write_text_file(path, corrupted);
        phm::FaultLogReport report{};
        NX_CHECK(phm::verify_fault_log(path, report).has_value());
        NX_CHECK(report.corrupted == 1U);
        NX_CHECK(report.intact == 2U);
    }

    // A tail cut before the checksum is truncated, not corrupted.
    {
        std::string truncated = text;
        const std::size_t final_newline = truncated.rfind('\n');
        NX_CHECK(final_newline != std::string::npos);
        truncated.erase(final_newline);
        const std::size_t last_space = truncated.rfind(' ');
        NX_CHECK(last_space != std::string::npos);
        truncated.erase(last_space);
        const std::string path = base + "_truncated.log";
        write_text_file(path, truncated);
        phm::FaultLogReport report{};
        NX_CHECK(phm::verify_fault_log(path, report).has_value());
        NX_CHECK(report.truncated == 1U);
        NX_CHECK(report.corrupted == 0U);
        NX_CHECK(report.intact == 2U);
    }

    // Pre-change four-field lines are accepted as legacy and unverified.
    {
        const std::string path = base + "_legacy.log";
        write_text_file(path, "AliveFailure 0 7 1234\n");
        phm::FaultLogReport report{};
        NX_CHECK(phm::verify_fault_log(path, report).has_value());
        NX_CHECK(report.legacy == 1U);
        NX_CHECK(report.corrupted == 0U);
    }

    static_cast<void>(std::remove(intact_path.c_str()));
}

void test_identity() {
    auto node = com::Node::create();
    NX_CHECK(node.has_value());
    const auto channel = com::ServiceName::create("Nexigen/Test/Claim");
    const auto event_name = com::ServiceName::create("Nexigen/Test/ClaimEvent");
    NX_CHECK(channel.has_value() && event_name.has_value());

    auto subscriber = com::Subscriber<phm::CheckpointClaim>::create(*node, *channel);
    auto listener = com::EventListener::create(*node, *event_name);
    auto publisher = phm::ClaimPublisher::create(*node, *channel, *event_name);
    NX_CHECK(subscriber.has_value() && listener.has_value() && publisher.has_value());

    NX_CHECK(
        publisher->claim("camera", static_cast<std::uint32_t>(::getpid()), ::getuid()).has_value());
    bool seen = false;
    for (int i = 0; i < 200 && !seen; ++i) {
        auto received = subscriber->receive();
        NX_CHECK(received.has_value());
        if (received->has_value()) {
            const auto& claim = (*received)->payload();
            NX_CHECK(std::string(claim.name) == "camera");
            NX_CHECK(claim.pid == static_cast<std::uint32_t>(::getpid()));
            NX_CHECK(claim.uid == ::getuid());
            seen = true;
        } else {
            std::this_thread::sleep_for(std::chrono::milliseconds(5));
        }
    }
    NX_CHECK(seen);

    // A sample service already on the claim name makes the claim publisher fail.
    {
        const auto conflict = com::ServiceName::create("Nexigen/Test/ClaimConflict");
        const auto conflict_event = com::ServiceName::create("Nexigen/Test/ClaimConflictEvent");
        NX_CHECK(conflict.has_value() && conflict_event.has_value());
        auto sample_publisher = com::Publisher<std::uint64_t>::create(*node, *conflict, 64);
        NX_CHECK(sample_publisher.has_value());
        NX_CHECK(!phm::ClaimPublisher::create(*node, *conflict, *conflict_event).has_value());
    }

    bool woken = false;
    const auto collected = listener->try_wait([&](com::EventId) { woken = true; });
    NX_CHECK(collected.has_value());
    NX_CHECK(woken);

    // Verification: matching uid passes; everything else is rejected.
    phm::CheckpointClaim claim{};
    std::snprintf(claim.name, sizeof(claim.name), "%s", "camera");
    claim.pid = static_cast<std::uint32_t>(::getpid());
    claim.uid = ::getuid();
    NX_CHECK(phm::checkpoint_claim_verdict(claim, ::getuid()).verified);
    NX_CHECK(!phm::checkpoint_claim_verdict(claim, ::getuid() + 1U).verified);
    phm::CheckpointClaim missing = claim;
    missing.pid = 999999U;
    NX_CHECK(!phm::checkpoint_claim_verdict(missing, ::getuid()).verified);
    phm::CheckpointClaim unnamed = claim;
    unnamed.name[0] = '\0';
    NX_CHECK(!phm::checkpoint_claim_verdict(unnamed, ::getuid()).verified);
    phm::CheckpointClaim zero_pid = claim;
    zero_pid.pid = 0U;
    const phm::ClaimVerdict zero_pid_verdict = phm::checkpoint_claim_verdict(zero_pid, ::getuid());
    NX_CHECK(!zero_pid_verdict.verified);
    NX_CHECK(zero_pid_verdict.reason.find("missing pid") != std::string::npos);

    // /proc status parsing is separated from the file read so malformed and
    // hostile text is testable without a live process.
    const auto real_uid = phm::parse_status_real_uid(
        "Name:\tcamera\nUid:\t1000\t1000\t1000\t1000\nGid:\t1000\t1000\t1000\t1000\n");
    NX_CHECK(real_uid.has_value() && *real_uid == 1000U);
    NX_CHECK(phm::parse_status_real_uid("Uid:\t42\nUid:\t43\n").value() == 42U);
    NX_CHECK(!phm::parse_status_real_uid("Name:\tcamera\n").has_value());
    NX_CHECK(!phm::parse_status_real_uid("Uid:\tabc\n").has_value());
    NX_CHECK(!phm::parse_status_real_uid("Uid:\t99999999999\n").has_value());
    NX_CHECK(!phm::parse_status_real_uid("").has_value());

    // The /proc half is testable without a process under a second uid.
    const std::string own_status = "Uid:\t" + std::to_string(::getuid()) + "\n";
    NX_CHECK(phm::checkpoint_claim_status_verdict(::getuid(), own_status).verified);
    const phm::ClaimVerdict mismatch =
        phm::checkpoint_claim_status_verdict(::getuid() + 1U, own_status);
    NX_CHECK(!mismatch.verified);
    NX_CHECK(mismatch.reason.find("process uid") != std::string::npos);
    const phm::ClaimVerdict unreadable =
        phm::checkpoint_claim_status_verdict(::getuid(), "Name:\tcamera\n");
    NX_CHECK(!unreadable.verified);
    NX_CHECK(unreadable.reason.find("unreadable") != std::string::npos);

    NX_CHECK(phm::checkpoint_claim_verdict(claim, ::getuid()).to_string().find("verified=true") !=
             std::string::npos);

#if defined(NEXIGEN_ENABLE_FAULT_INJECTION)
    // Transport failures on the claim path: the sample send and the wake event.
    nexigen::core::fault_injection::set_fault_hook(phm_fault_hook);
    g_phm_fault_enabled = true;
    // Both channel factories fail before touching the transport.
    g_phm_fault_point = nexigen::core::fault_injection::FaultPoint::Phm_claim_channel;
    NX_CHECK(!phm::ClaimPublisher::create(*node, *channel, *event_name).has_value());
    g_phm_fault_point = nexigen::core::fault_injection::FaultPoint::Phm_claim_event_channel;
    NX_CHECK(!phm::ClaimPublisher::create(*node, *channel, *event_name).has_value());
    g_phm_fault_point = nexigen::core::fault_injection::FaultPoint::Phm_claim_send;
    NX_CHECK(!publisher->claim("camera", static_cast<std::uint32_t>(::getpid()), ::getuid())
                  .has_value());
    g_phm_fault_point = nexigen::core::fault_injection::FaultPoint::Phm_claim_event;
    NX_CHECK(!publisher->claim("camera", static_cast<std::uint32_t>(::getpid()), ::getuid())
                  .has_value());
    g_phm_fault_enabled = false;
    nexigen::core::fault_injection::set_fault_hook(nullptr);
#endif

    // Samples come from shared memory: a producer that fills the buffer without
    // a NUL must not make diagnostics read past the sample.
    phm::CheckpointClaim full{};
    std::memset(full.name, 'x', sizeof(full.name));
    full.pid = 1U;
    full.uid = 2U;
    NX_CHECK(phm::bounded_name(full.name, sizeof(full.name)).size() == sizeof(full.name));
    NX_CHECK(full.to_string().find("xxxx") != std::string::npos);
    NX_CHECK(full.to_string().size() < 200U);

    phm::SecurityEvent event{};
    std::memset(event.process, 'y', sizeof(event.process));
    event.pid = 3U;
    event.uid = 4U;
    const std::string event_text = event.to_string();
    NX_CHECK(event_text.find("ClaimRejected") != std::string::npos);
    NX_CHECK(event_text.find("yyyy") != std::string::npos);
    NX_CHECK(std::format("{}", event) == event_text);

    // Registry: legacy entries pass, gated entries need a verified claim, and a
    // stopping incarnation must claim again.
    phm::PhmManifest manifest;
    phm::SupervisedProcess gated = make_process("camera");
    gated.expected_uid = ::getuid();
    manifest.processes.push_back(gated);
    manifest.processes.push_back(make_process("legacy"));

    phm::ClaimRegistry registry(manifest);
    NX_CHECK(registry.requires_claim(0));
    NX_CHECK(!registry.requires_claim(1));
    NX_CHECK(!registry.accepts(0));
    NX_CHECK(registry.accepts(1));
    NX_CHECK(!registry.accepts(99));
    NX_CHECK(registry.to_string().find("verified=0") != std::string::npos);

    phm::CheckpointClaim unknown = claim;
    std::snprintf(unknown.name, sizeof(unknown.name), "%s", "ghost");
    NX_CHECK(registry.apply(unknown) == phm::SecurityEventKind::ClaimRejected);

    phm::CheckpointClaim foreign = claim;
    foreign.uid = ::getuid() + 1U;
    NX_CHECK(registry.apply(foreign) == phm::SecurityEventKind::ClaimRejected);
    NX_CHECK(!registry.accepts(0));

    phm::CheckpointClaim gone = claim;
    gone.pid = 999999U;
    NX_CHECK(registry.apply(gone) == phm::SecurityEventKind::ClaimRejected);

    // A legacy entry ignores claims entirely: no event, no state change.
    phm::CheckpointClaim legacy_claim = claim;
    std::snprintf(legacy_claim.name, sizeof(legacy_claim.name), "%s", "legacy");
    NX_CHECK(!registry.apply(legacy_claim).has_value());
    NX_CHECK(registry.accepts(1));

    // Verified, then reset by a lifecycle stop: the next incarnation must claim.
    NX_CHECK(!registry.apply(claim).has_value());
    NX_CHECK(registry.accepts(0));
    NX_CHECK(registry.verified_count() == 1U);
    NX_CHECK(registry.to_string().find("verified=1") != std::string::npos);
    // A re-claim for the verified incarnation is idempotent (no /proc read).
    NX_CHECK(!registry.apply(claim).has_value());
    NX_CHECK(!registry.apply(claim).has_value());
    NX_CHECK(registry.accepts(0));
    NX_CHECK(registry.verified_count() == 1U);
    registry.reset(0);
    NX_CHECK(!registry.accepts(0));
    NX_CHECK(registry.verified_count() == 0U);

    NX_CHECK(!registry.apply(claim).has_value());
    registry.reset_all();
    NX_CHECK(!registry.accepts(0));
    NX_CHECK(registry.verified_count() == 0U);
}

void test_supervised_entity() {
    auto node = com::Node::create();
    NX_CHECK(node.has_value());
    const auto checkpoint_name = com::ServiceName::create("Nexigen/Test/AppCheckpoints");
    const auto health_name = com::ServiceName::create("Nexigen/Test/AppHealth");
    NX_CHECK(checkpoint_name.has_value());
    NX_CHECK(health_name.has_value());

    auto entity = phm::SupervisedEntity::create(*node, "camera", *checkpoint_name, *health_name);
    NX_CHECK(entity.has_value());
    NX_CHECK(entity->app_name() == "camera");

    // Checkpoint reporting reaches the daemon-side listener.
    auto checkpoint_listener = com::EventListener::create(*node, *checkpoint_name);
    NX_CHECK(checkpoint_listener.has_value());
    NX_CHECK(entity->report_checkpoint(7).has_value());
    bool checkpoint_seen = false;
    const auto collected = checkpoint_listener->try_wait(
        [&](com::EventId id) { checkpoint_seen = id.as_value() == 7U; });
    NX_CHECK(collected.has_value());
    NX_CHECK(checkpoint_seen);

    // Health reporting reaches the daemon-side listener.
    auto health_listener = com::EventListener::create(*node, *health_name);
    NX_CHECK(health_listener.has_value());
    NX_CHECK(entity->report_health(phm::HealthState::Error).has_value());
    bool health_seen = false;
    const auto collected_health = health_listener->try_wait([&](com::EventId id) {
        health_seen = id.as_value() == static_cast<std::uint64_t>(phm::HealthState::Error);
    });
    NX_CHECK(collected_health.has_value());
    NX_CHECK(health_seen);

    // Function Group state: read what the SM publishes through the Field.
    const auto group_service = com::ServiceName::create("Nexigen/SM/State/motion_control");
    NX_CHECK(group_service.has_value());
    nexigen::sm::GroupStatePayload initial{};
    std::snprintf(initial.state, sizeof(initial.state), "Active");
    auto sm_field =
        com::Field<nexigen::sm::GroupStatePayload>::create(*node, *group_service, initial);
    NX_CHECK(sm_field.has_value());
    NX_CHECK(sm_field->set(nexigen::sm::GroupStatePayload{"SafeState"}).has_value());

    const auto state = entity->current_group_state("motion_control");
    NX_CHECK(state.has_value());
    NX_CHECK(state->has_value());
    NX_CHECK(std::string((*state)->state) == "SafeState");

    // An unknown (unpublished) group reports no state without blocking.
    const auto unknown = entity->current_group_state("unpublished_group");
    NX_CHECK(unknown.has_value());
    NX_CHECK(!unknown->has_value());

    // Identity claim: creation publishes the claim, and on-demand claiming
    // re-publishes it (the daemon uses the same path after a restart).
    const auto claim_service = com::ServiceName::create("Nexigen/Test/EntityClaim");
    const auto claim_event = com::ServiceName::create("Nexigen/Test/EntityClaimEvent");
    NX_CHECK(claim_service.has_value() && claim_event.has_value());
    auto claim_subscriber = com::Subscriber<phm::CheckpointClaim>::create(*node, *claim_service);
    NX_CHECK(claim_subscriber.has_value());
    const phm::ClaimConfig claim_config{*claim_service, *claim_event};
    auto claiming = phm::SupervisedEntity::create(*node, "camera", *checkpoint_name, *health_name,
                                                  claim_config);
    NX_CHECK(claiming.has_value());
    bool entity_claim_seen = false;
    for (int i = 0; i < 200 && !entity_claim_seen; ++i) {
        auto received = claim_subscriber->receive();
        NX_CHECK(received.has_value());
        if (received->has_value()) {
            const auto& sample = (*received)->payload();
            entity_claim_seen = phm::bounded_name(sample.name, sizeof(sample.name)) == "camera" &&
                                sample.pid == static_cast<std::uint32_t>(::getpid()) &&
                                sample.uid == ::getuid();
        } else {
            std::this_thread::sleep_for(std::chrono::milliseconds(5));
        }
    }
    NX_CHECK(entity_claim_seen);
    NX_CHECK(claiming->claim_identity().has_value());

    // A legacy entity has no claim channel: an explicit claim is a no-op that
    // still reports success.
    NX_CHECK(entity->claim_identity().has_value());

    // A zero interval re-publishes the claim on every checkpoint report.
    const auto periodic_service = com::ServiceName::create("Nexigen/Test/PeriodicClaim");
    const auto periodic_event = com::ServiceName::create("Nexigen/Test/PeriodicClaimEvent");
    NX_CHECK(periodic_service.has_value() && periodic_event.has_value());
    auto periodic_subscriber =
        com::Subscriber<phm::CheckpointClaim>::create(*node, *periodic_service);
    NX_CHECK(periodic_subscriber.has_value());
    const phm::ClaimConfig periodic_config{*periodic_service, *periodic_event,
                                           std::chrono::milliseconds{0}};
    auto periodic = phm::SupervisedEntity::create(*node, "periodic", *checkpoint_name, *health_name,
                                                  periodic_config);
    NX_CHECK(periodic.has_value());
    const auto receive_claim = [&]() {
        for (int i = 0; i < 200; ++i) {
            auto received = periodic_subscriber->receive();
            NX_CHECK(received.has_value());
            if (received->has_value()) {
                return true;
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(5));
        }
        return false;
    };
    NX_CHECK(receive_claim()); // claim published during creation
    NX_CHECK(periodic->report_checkpoint(1).has_value());
    NX_CHECK(receive_claim()); // re-claim published by the zero interval

    // A 1 s interval suppresses an immediate re-claim (the early-return branch).
    NX_CHECK(claiming->report_checkpoint(2).has_value());

#if defined(NEXIGEN_ENABLE_FAULT_INJECTION)
    // Each channel open is faultable, so every creation failure branch is
    // reachable without an incompatible live service.
    {
        const auto fault_claim = com::ServiceName::create("Nexigen/Test/EntityClaimFault");
        const auto fault_event = com::ServiceName::create("Nexigen/Test/EntityClaimFaultEvent");
        NX_CHECK(fault_claim.has_value() && fault_event.has_value());
        const phm::ClaimConfig claim_config_fault{*fault_claim, *fault_event};
        nexigen::core::fault_injection::set_fault_hook(phm_fault_hook);
        g_phm_fault_enabled = true;
        g_phm_fault_point = nexigen::core::fault_injection::FaultPoint::Phm_checkpoint_channel;
        NX_CHECK(!phm::SupervisedEntity::create(*node, "conflict", *checkpoint_name, *health_name)
                      .has_value());
        g_phm_fault_point = nexigen::core::fault_injection::FaultPoint::Phm_health_channel;
        NX_CHECK(!phm::SupervisedEntity::create(*node, "conflict", *checkpoint_name, *health_name)
                      .has_value());
        g_phm_fault_point = nexigen::core::fault_injection::FaultPoint::Phm_claim_channel;
        NX_CHECK(!phm::SupervisedEntity::create(*node, "conflict", *checkpoint_name, *health_name,
                                                claim_config_fault)
                      .has_value());
        g_phm_fault_point = nexigen::core::fault_injection::FaultPoint::Phm_claim_send;
        NX_CHECK(!phm::SupervisedEntity::create(*node, "conflict", *checkpoint_name, *health_name,
                                                claim_config_fault)
                      .has_value());
        g_phm_fault_enabled = false;
        nexigen::core::fault_injection::set_fault_hook(nullptr);
    }
#endif

    // An invalid group name is a caller error, not an absent state.
    NX_CHECK(!entity->current_group_state(std::string(300, 'x')).has_value());

    // Diagnostics.
    const std::string text = entity->to_string();
    NX_CHECK(text.find("camera") != std::string::npos);
    NX_CHECK(text.find("last_checkpoint=7") != std::string::npos);
    NX_CHECK(text.find("Error") != std::string::npos);
    NX_CHECK(phm::to_string(*entity) == text);
    NX_CHECK(std::format("{}", *entity) == text);
}

bool wait_thread_ready(const phm::FaultStorage& storage) {
    for (int i = 0; i < 2000 && !storage.thread_ready(); ++i) {
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    return storage.thread_ready();
}

void test_thread_spec() {
    std::uint64_t available = 0U;
    if (!nexigen::core::affinity::self_affinity(available) || available == 0U) {
        return;
    }
    const std::uint64_t first = available & (~available + 1ULL);
    const std::uint64_t rest = available & ~first;
    const std::uint64_t second = rest == 0U ? first : (rest & (~rest + 1ULL));

    nexigen::core::ThreadSpec bg{};
    NX_CHECK(bg.set_name("phm-bg").has_value());
    bg.set_cpus(nexigen::core::CpuSet::from_core_mask(first));

    nexigen::core::ThreadSpec storage{};
    NX_CHECK(storage.set_name("phm-store").has_value());
    storage.set_cpus(nexigen::core::CpuSet::from_core_mask(second));

    const std::string path_a =
        "/tmp/opencode/phm_thread_spec_a_" + std::to_string(static_cast<unsigned long>(::getpid()));
    const std::string path_b =
        "/tmp/opencode/phm_thread_spec_b_" + std::to_string(static_cast<unsigned long>(::getpid()));
    const std::string path_c =
        "/tmp/opencode/phm_thread_spec_c_" + std::to_string(static_cast<unsigned long>(::getpid()));
    {
        phm::FaultStorage primary(path_a, nexigen::core::effective_thread_spec(storage, bg));
        NX_CHECK(wait_thread_ready(primary));
        NX_CHECK((primary.thread_report().applied() &
                  nexigen::core::ThreadApplyReport::name_mask) != 0U);
        NX_CHECK((primary.thread_mask() & second) != 0U);
    }
    {
        phm::FaultStorage fallback(
            path_b, nexigen::core::effective_thread_spec(nexigen::core::ThreadSpec{}, bg));
        NX_CHECK(wait_thread_ready(fallback));
        NX_CHECK((fallback.thread_report().applied() &
                  nexigen::core::ThreadApplyReport::name_mask) != 0U);
        NX_CHECK((fallback.thread_mask() & first) != 0U);
    }
    std::uint64_t inherited = 0U;
    NX_CHECK(nexigen::core::affinity::self_affinity(inherited).has_value());
    {
        phm::FaultStorage neutral(
            path_c, nexigen::core::effective_thread_spec(nexigen::core::ThreadSpec{},
                                                         nexigen::core::ThreadSpec{}));
        NX_CHECK(wait_thread_ready(neutral));
        NX_CHECK(neutral.thread_report().requested() == 0U);
        NX_CHECK(neutral.thread_mask() == inherited);
    }
    static_cast<void>(std::remove(path_a.c_str()));
    static_cast<void>(std::remove(path_b.c_str()));
    static_cast<void>(std::remove(path_c.c_str()));
}

} // namespace


void test_recovery_payloads() {
    phm::RecoveryRequest request{};
    request.action = phm::RecoveryAction::RestartProcess;
    request.request_id = 7;
    request.pid = 42;
    std::snprintf(request.name, sizeof(request.name), "%s", "camera");
    std::snprintf(request.function_group, sizeof(request.function_group), "%s", "adas");
    nlohmann::json request_json;
    to_json(request_json, request);
    phm::RecoveryRequest request_back{};
    from_json(request_json, request_back);
    NX_CHECK(request_back.request_id == 7U);
    NX_CHECK(request_back.pid == 42U);
    NX_CHECK(std::string_view(request_back.name) == "camera");
    NX_CHECK(std::string_view(request_back.function_group) == "adas");
    NX_CHECK(request_back.action == phm::RecoveryAction::RestartProcess);
    phm::RecoveryRequest untouched{};
    untouched.request_id = 99;
    from_json(nlohmann::json::array(), untouched);
    NX_CHECK(untouched.request_id == 99U);

    phm::RecoveryResponse response{7, phm::RecoveryOutcome::Rejected,
                                   phm::RecoveryReason::NotOperational};
    nlohmann::json response_json;
    to_json(response_json, response);
    phm::RecoveryResponse response_back{};
    from_json(response_json, response_back);
    NX_CHECK(response_back.request_id == 7U);
    NX_CHECK(response_back.outcome == phm::RecoveryOutcome::Rejected);
    NX_CHECK(response_back.reason == phm::RecoveryReason::NotOperational);
    NX_CHECK(response.to_string().find("NotOperational") != std::string::npos);
    NX_CHECK(std::format("{}", response) == response.to_string());

    phm::RecoveryAck ack{9, phm::RecoveryOutcome::Accepted, phm::RecoveryReason::None};
    nlohmann::json ack_json;
    to_json(ack_json, ack);
    phm::RecoveryAck ack_back{};
    from_json(ack_json, ack_back);
    NX_CHECK(ack_back.request_id == 9U);
    NX_CHECK(ack_back.outcome == phm::RecoveryOutcome::Accepted);
    NX_CHECK(std::format("{}", ack) == ack.to_string());

    nexigen::sm::RecoveryNotification notification{};
    notification.request_id = 11;
    std::snprintf(notification.function_group, sizeof(notification.function_group), "%s",
                  "motion");
    NX_CHECK(notification.to_string().find("motion") != std::string::npos);
    NX_CHECK(std::format("{}", notification) == notification.to_string());
}

int main() {
    run_suite("vocabulary", test_vocabulary);
    run_suite("health_channel", test_health_channel);
    run_suite("supervision", test_supervision);
    run_suite("recovery", test_recovery);
    run_suite("recovery_payloads", test_recovery_payloads);
    run_suite("recovery_ack_budget", test_recovery_ack_budget);
    run_suite("manifest", test_manifest);
    run_suite("supervision_exhaustion", test_supervision_exhaustion);
    run_suite("watchdog", test_watchdog);
    run_suite("fault_log", test_fault_log);
    run_suite("identity", test_identity);
    run_suite("thread_spec", test_thread_spec);
    run_suite("supervised_entity", test_supervised_entity);
    std::printf("TOTAL %d checks, %d failed\n", g_checks, g_failures);
    return g_failures == 0 ? 0 : 1;
}
