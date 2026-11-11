// Тесты nexigen::phm — харнес как в остальных модулях (NX_CHECK + run_suite).
// Сборка только при NEXIGEN_ENABLE_COM=ON.
//
// Наборы: health_channel (каналы + чекпоинты), supervision (alive/logical/
// deadline/tolerance), recovery (RecoveryClient против mock-EM + fault
// storage), manifest (round-trip + hostile input).
#include <chrono>
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
#include <nexigen/core/serialization_mandate.hpp>
#include <nexigen/phm/checkpoint.hpp>
#include <nexigen/phm/fault_kind.hpp>
#include <nexigen/phm/fault_storage.hpp>
#include <nexigen/phm/health_channel.hpp>
#include <nexigen/phm/health_state.hpp>
#include <nexigen/phm/phm_manifest.hpp>
#include <nexigen/phm/recovery.hpp>
#include <nexigen/phm/recovery_action.hpp>
#include <nexigen/phm/recovery_request.hpp>
#include <nexigen/phm/recovery_response.hpp>
#include <nexigen/phm/supervision_status.hpp>
#include <nexigen/phm/supervised_entity.hpp>
#include <nexigen/phm/supervisor.hpp>
#include <nexigen/sm/group_state_payload.hpp>
#include <nexigen/sm/recovery_notification.hpp>

namespace com = nexigen::com;
namespace phm = nexigen::phm;

namespace {

int g_checks = 0;
int g_failures = 0;

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

template <class E>
void check_enum_round_trip(std::initializer_list<E> values) {
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
    check_enum_round_trip({phm::HealthState::Ok, phm::HealthState::Warning,
                           phm::HealthState::Error, phm::HealthState::Critical});
    check_enum_round_trip({phm::SupervisionStatus::Ok, phm::SupervisionStatus::Failed,
                           phm::SupervisionStatus::Expired, phm::SupervisionStatus::Stopped});
    check_enum_round_trip({phm::RecoveryAction::RestartProcess,
                           phm::RecoveryAction::SwitchFunctionGroupState,
                           phm::RecoveryAction::TriggerWatchdogReset});
    check_enum_round_trip({phm::FaultKind::AliveFailure, phm::FaultKind::LogicalFailure,
                           phm::FaultKind::DeadlineFailure});
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
            observed = id.as_value() ==
                       static_cast<std::uint64_t>(phm::HealthState::Error);
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
        const auto collected = listener->try_wait([&](com::EventId id) {
            observed = id.as_value() == 7U;
        });
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
}

void test_recovery() {
    auto node = com::Node::create();
    NX_CHECK(node.has_value());
    const auto em_name = com::ServiceName::create("Nexigen/Test/Recovery");
    const auto sm_notif_name = com::ServiceName::create("Nexigen/Test/SmRecovery");
    const auto sm_event_name = com::ServiceName::create("Nexigen/Test/SmRecoveryEvent");
    NX_CHECK(em_name.has_value());
    NX_CHECK(sm_notif_name.has_value());
    NX_CHECK(sm_event_name.has_value());

    // Mock EM responder: a com::Server that accepts every request and answers
    // accepted=1 (the real StateClient lives in nexigen::exec).
    auto client = phm::RecoveryClient::create(*node, *em_name, *sm_notif_name, *sm_event_name);
    NX_CHECK(client.has_value());
    auto server = com::Server<phm::RecoveryRequest, phm::RecoveryResponse>::create(*node,
                                                                                   *em_name);
    NX_CHECK(server.has_value());

    std::thread mock_em([&server] {
        for (int i = 0; i < 200; ++i) {
            auto request = server->receive();
            if (request.has_value() && request->has_value()) {
                NX_CHECK((*request)->send_copy(phm::RecoveryResponse{1}).has_value());
                return;
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(5));
        }
    });

    // Local recovery: restart a failed process by identity (direct PHM->EM).
    const auto restart = client->restart_process(42, "camera", std::chrono::milliseconds(2000));
    mock_em.join();
    NX_CHECK(restart.has_value());
    NX_CHECK(restart->accepted == 1U);
    NX_CHECK(client->last_action() == phm::RecoveryAction::RestartProcess);
    const std::string text = client->to_string();
    NX_CHECK(text.find("RestartProcess") != std::string::npos);
    NX_CHECK(std::format("{}", *client) == text);

    // SafeState escalation (re-routed per HLR-SM-030): the fault notification
    // is published to the SM channel, not to the EM.
    {
        phm::SupervisedProcess process;
        process.name = "steering";
        process.safety_critical = true;
        process.recovery_rules.push_back(
            {phm::RecoveryAction::SwitchFunctionGroupState, "motion_control"});

        auto sm_subscriber =
            com::Subscriber<nexigen::sm::RecoveryNotification>::create(*node, *sm_notif_name);
        auto sm_listener = com::EventListener::create(*node, *sm_event_name);
        NX_CHECK(sm_subscriber.has_value());
        NX_CHECK(sm_listener.has_value());

        const auto escalated = client->apply_rule(process, std::chrono::milliseconds(2000));
        NX_CHECK(escalated.has_value());
        NX_CHECK(client->last_action() == phm::RecoveryAction::SwitchFunctionGroupState);

        // The SM channel carries the failed function group.
        bool observed = false;
        for (int i = 0; i < 200 && !observed; ++i) {
            auto received = sm_subscriber->receive();
            NX_CHECK(received.has_value());
            if (received->has_value()) {
                observed = std::string((*received)->payload().function_group) ==
                           "motion_control";
            } else {
                std::this_thread::sleep_for(std::chrono::milliseconds(10));
            }
        }
        NX_CHECK(observed);
        // The wake event fires so the SM's WaitSet wakes without polling.
        bool woken = false;
        const auto collected = sm_listener->try_wait([&](com::EventId) { woken = true; });
        NX_CHECK(collected.has_value());
        NX_CHECK(woken);
    }

    // Fault storage: records land in the log asynchronously.
    {
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
}

void test_manifest() {
    phm::PhmManifest manifest;
    phm::SupervisedProcess process = make_process("camera");
    process.recovery_rules.push_back(
        {phm::RecoveryAction::RestartProcess, "ignored_group"});
    manifest.processes.push_back(process);

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
    NX_CHECK(back.processes[0].recovery_rules[0].action ==
             phm::RecoveryAction::RestartProcess);

    const std::string text = manifest.to_string();
    NX_CHECK(text.find("camera") != std::string::npos);
    NX_CHECK(to_string(manifest) == text);
    NX_CHECK(std::format("{}", manifest) == text);

    // Hostile input leaves the target unchanged.
    from_json(nlohmann::json::array(), manifest);
    NX_CHECK(manifest.processes.size() == 1U);
    from_json(nlohmann::json{{"processes", nlohmann::json::array({42})}}, manifest);
    NX_CHECK(manifest.processes.size() == 1U);
}

void test_supervised_entity() {
    auto node = com::Node::create();
    NX_CHECK(node.has_value());
    const auto checkpoint_name = com::ServiceName::create("Nexigen/Test/AppCheckpoints");
    const auto health_name = com::ServiceName::create("Nexigen/Test/AppHealth");
    NX_CHECK(checkpoint_name.has_value());
    NX_CHECK(health_name.has_value());

    auto entity =
        phm::SupervisedEntity::create(*node, "camera", *checkpoint_name, *health_name);
    NX_CHECK(entity.has_value());
    NX_CHECK(entity->app_name() == "camera");

    // Checkpoint reporting reaches the daemon-side listener.
    auto checkpoint_listener = com::EventListener::create(*node, *checkpoint_name);
    NX_CHECK(checkpoint_listener.has_value());
    NX_CHECK(entity->report_checkpoint(7).has_value());
    bool checkpoint_seen = false;
    const auto collected = checkpoint_listener->try_wait([&](com::EventId id) {
        checkpoint_seen = id.as_value() == 7U;
    });
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
    auto sm_field = com::Field<nexigen::sm::GroupStatePayload>::create(*node, *group_service,
                                                                      initial);
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

    // Diagnostics.
    const std::string text = entity->to_string();
    NX_CHECK(text.find("camera") != std::string::npos);
    NX_CHECK(text.find("last_checkpoint=7") != std::string::npos);
    NX_CHECK(text.find("Error") != std::string::npos);
    NX_CHECK(phm::to_string(*entity) == text);
    NX_CHECK(std::format("{}", *entity) == text);
}

} // namespace

int main() {
    run_suite("vocabulary", test_vocabulary);
    run_suite("health_channel", test_health_channel);
    run_suite("supervision", test_supervision);
    run_suite("recovery", test_recovery);
    run_suite("manifest", test_manifest);
    run_suite("supervised_entity", test_supervised_entity);
    std::printf("TOTAL %d checks, %d failed\n", g_checks, g_failures);
    return g_failures == 0 ? 0 : 1;
}
