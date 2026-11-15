// Тесты nexigen::sm — харнес как в остальных модулях (NX_CHECK + run_suite).
// Сборка только при NEXIGEN_ENABLE_COM=ON.
//
// Наборы: vocabulary (enum'ы), state_machine (граф/арбитраж/sequencing),
// ipc (Field-публикация + prepare), recovery_handler (PHM fault → SafeState),
// telemetry (audit + метрики), manifest (round-trip).
#include <chrono>
#include <cstdint>
#include <cstdlib>
#include <unistd.h>
#include <atomic>
#include <cstdio>
#include <format>
#include <string>
#include <string_view>
#include <thread>
#include <variant>
#include <vector>

#include <nlohmann/json.hpp>

#include <nexigen/com/event.hpp>
#include <nexigen/com/field.hpp>
#include <nexigen/com/node.hpp>
#include <nexigen/com/publisher.hpp>
#include <nexigen/com/server.hpp>
#include <nexigen/com/service_name.hpp>
#include <nexigen/com/subscriber.hpp>
#include <nexigen/core/affinity.hpp>
#include <nexigen/core/serialization_mandate.hpp>
#include <nexigen/core/thread_management.hpp>
#include <nexigen/exec/root_state_query.hpp>
#include <nexigen/exec/state_ack.hpp>
#include <nexigen/exec/state_command.hpp>
#include <nexigen/sm/lightweight_state_machine.hpp>
#include <nexigen/sm/root_lifecycle.hpp>
#include <nexigen/sm/root_state.hpp>
#include <nexigen/sm/ucm.hpp>
#include <nexigen/sm/update_coordinator.hpp>
#include <nexigen/sm/recovery_handler.hpp>
#include <nexigen/sm/sm_manifest.hpp>
#include <nexigen/sm/sm_metrics.hpp>
#include <nexigen/sm/state_manager.hpp>
#include <nexigen/sm/state_publication.hpp>
#include <nexigen/sm/transition_audit.hpp>
#include <nexigen/sm/transition_preparation.hpp>
#include <nexigen/sm/transition_record.hpp>
#include <nexigen/sm/transition_result.hpp>
#include <nexigen/sm/trigger_request.hpp>
#include <nexigen/sm/trigger_response.hpp>

namespace com = nexigen::com;
namespace sm = nexigen::sm;

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

sm::SmManifest make_manifest() {
    sm::SmManifest manifest;
    sm::FunctionGroup motion;
    motion.name = "motion_control";
    motion.states = {"Active", "Parking", "Off", "SafeState"};
    motion.allowed_transitions = {
        {"Active", "Parking"}, {"Parking", "Active"},   {"Parking", "Off"},
        {"Off", "Parking"},    {"Active", "SafeState"}, {"Parking", "SafeState"},
    };
    motion.transition_max_time_ms = 500;
    motion.preparation_timeout_ms = 100;
    motion.application_group = false;
    manifest.function_groups.push_back(motion);

    // An application group gated on RootFG::Run (spec scenario
    // "Application groups wait for RootFG Run").
    sm::FunctionGroup adas;
    adas.name = "adas";
    adas.states = {"Off", "Active", "SafeState"};
    adas.allowed_transitions = {
        {"Off", "Active"},
        {"Active", "Off"},
        {"Active", "SafeState"},
    };
    adas.application_group = true;
    manifest.function_groups.push_back(adas);

    manifest.requester_priorities = {{"phm", 90}, {"diagnostics", 50}, {"app", 10}};
    manifest.metrics_flush_interval_ms = 100;
    manifest.startup_timeout_ms = 5000;
    return manifest;
}

[[nodiscard]] sm::StateManager make_manager() {
    auto result = sm::StateManager::create(make_manifest());
    NX_CHECK(result.has_value());
    if (!result) {
        std::fprintf(stderr, "invalid test manifest\n");
        std::exit(1);
    }
    return std::move(*result);
}

void test_vocabulary() {
    for (const sm::TransitionResult value :
         {sm::TransitionResult::Success, sm::TransitionResult::RejectedByPriority,
          sm::TransitionResult::RejectedByUpdate, sm::TransitionResult::InvalidGraph,
          sm::TransitionResult::Timeout}) {
        const std::string_view name = to_string(value);
        NX_CHECK(!name.empty());
        sm::TransitionResult back{};
        NX_CHECK(from_string(name, back));
        NX_CHECK(back == value);
        nlohmann::json j;
        to_json(j, value);
        NX_CHECK(j == std::string(name));
        sm::TransitionResult parsed{};
        from_json(j, parsed);
        NX_CHECK(parsed == value);
        NX_CHECK(std::format("{}", value) == std::string(name));
    }
    for (const sm::RootFG value :
         {sm::RootFG::Off, sm::RootFG::Startup, sm::RootFG::Verify, sm::RootFG::Run,
          sm::RootFG::Shutdown, sm::RootFG::Restart}) {
        const std::string_view name = to_string(value);
        NX_CHECK(!name.empty());
        sm::RootFG back{};
        NX_CHECK(from_string(name, back));
        NX_CHECK(back == value);
        nlohmann::json j;
        to_json(j, value);
        NX_CHECK(j == std::string(name));
        sm::RootFG parsed{};
        from_json(j, parsed);
        NX_CHECK(parsed == value);
        NX_CHECK(std::format("{}", value) == std::string(name));
    }
    sm::RootFG legacy_state = sm::RootFG::Off;
    NX_CHECK(!sm::from_string("NormalRunning", legacy_state));
    NX_CHECK(legacy_state == sm::RootFG::Off);
    NX_CHECK(to_string(static_cast<sm::RootFG>(0x7f)) == "Unknown");
    static_assert(com::concepts::Message<sm::TriggerRequest>);
    static_assert(com::concepts::Message<sm::TriggerResponse>);
    static_assert(com::concepts::Message<sm::RecoveryNotification>);
    static_assert(com::concepts::Message<sm::GroupStatePayload>);
    static_assert(com::concepts::Message<sm::TransitionPreparationPayload>);
    NX_CHECK(to_string(static_cast<sm::TransitionResult>(0x7f)) == "Unknown");
}

void test_state_machine() {
    // Invalid transition is rejected and the state stays.
    {
        sm::StateManager manager = make_manager();
        const sm::TransitionResult result = manager.request_transition(0, "Off", "app");
        NX_CHECK(result == sm::TransitionResult::InvalidGraph);
        NX_CHECK(manager.state(0) == "Active");
    }

    // Valid transition is applied.
    {
        sm::StateManager manager = make_manager();
        const sm::TransitionResult result = manager.request_transition(0, "Parking", "app");
        NX_CHECK(result == sm::TransitionResult::Success);
        NX_CHECK(manager.state(0) == "Parking");
    }

    // Arbitration: a higher-priority requester wins over the lower holder.
    {
        sm::StateManager manager = make_manager();
        NX_CHECK(manager.request_transition(0, "Parking", "app") == sm::TransitionResult::Success);
        // diagnostics (50) can move it further; app (10) cannot reverse it.
        NX_CHECK(manager.request_transition(0, "Off", "diagnostics") ==
                 sm::TransitionResult::Success);
        NX_CHECK(manager.request_transition(0, "Parking", "app") ==
                 sm::TransitionResult::RejectedByPriority);
        NX_CHECK(manager.state(0) == "Off");
        // phm (90) always wins.
        NX_CHECK(manager.request_transition(0, "Parking", "phm") == sm::TransitionResult::Success);
    }

    // Startup/shutdown sequencing: Off -> Startup -> Verify -> Run for
    // wake-up, Run -> Shutdown -> Off for shutdown, Restart -> Startup for a
    // fast reboot that does not pass through Off.
    {
        sm::StateManager manager = make_manager();
        NX_CHECK(manager.root_fg() == sm::RootFG::Off);
        NX_CHECK(!manager.advance_startup()); // out of order from Off
        NX_CHECK(manager.begin_startup());
        NX_CHECK(manager.root_fg() == sm::RootFG::Startup);
        NX_CHECK(manager.advance_startup());
        NX_CHECK(manager.root_fg() == sm::RootFG::Verify);
        NX_CHECK(manager.confirm_startup_step());
        NX_CHECK(manager.root_fg() == sm::RootFG::Run);

        NX_CHECK(manager.begin_restart());
        NX_CHECK(manager.root_fg() == sm::RootFG::Restart);
        NX_CHECK(manager.advance_restart());
        NX_CHECK(manager.root_fg() == sm::RootFG::Startup);
        NX_CHECK(manager.advance_startup());
        NX_CHECK(manager.confirm_startup_step());

        NX_CHECK(manager.request_transition(1, "Active", "app") == sm::TransitionResult::Success);
        manager.begin_shutdown();
        NX_CHECK(manager.root_fg() == sm::RootFG::Shutdown);
        NX_CHECK(manager.state(1) == "Off");
        NX_CHECK(manager.complete_shutdown());
        NX_CHECK(manager.root_fg() == sm::RootFG::Off);
        NX_CHECK(!manager.complete_shutdown());
    }

    // Application groups wait for RootFG::Run: no application group
    // starts while the root foundation is in Startup/Verify.
    {
        sm::StateManager manager = make_manager();
        NX_CHECK(manager.request_transition(1, "Active", "app") ==
                 sm::TransitionResult::InvalidGraph);
        NX_CHECK(manager.state(1) == "Off");
        NX_CHECK(manager.begin_startup());
        NX_CHECK(!manager.application_groups_may_start());
        NX_CHECK(manager.request_transition(1, "Active", "app") ==
                 sm::TransitionResult::InvalidGraph);
        NX_CHECK(manager.advance_startup());
        NX_CHECK(manager.request_transition(1, "Active", "app") ==
                 sm::TransitionResult::InvalidGraph);
        NX_CHECK(manager.state(1) == "Off");
        NX_CHECK(manager.confirm_startup_step());
        NX_CHECK(manager.application_groups_may_start());
        NX_CHECK(manager.request_transition(1, "Active", "app") == sm::TransitionResult::Success);
        NX_CHECK(manager.state(1) == "Active");
    }

    // Diagnostics render RootFG + group states.
    {
        sm::StateManager manager = make_manager();
        const std::string text = manager.to_string();
        NX_CHECK(text.find("root_fg=Off") != std::string::npos);
        NX_CHECK(text.find("motion_control=Active") != std::string::npos);
        NX_CHECK(to_string(manager) == text);
        NX_CHECK(std::format("{}", manager) == text);
    }
}

void test_ipc() {
    auto node = com::Node::create();
    NX_CHECK(node.has_value());

    // Current-state publication: a late reader reads the latest value.
    {
        sm::StateManager manager = make_manager();
        auto publication = sm::StatePublication::create(*node, make_manifest());
        NX_CHECK(publication.has_value());
        NX_CHECK(publication->publish(0, "Parking").has_value());

        const auto field_name = com::ServiceName::create("Nexigen/SM/State/motion_control");
        NX_CHECK(field_name.has_value());
        auto listener = com::FieldListener<sm::GroupStatePayload>::open(*node, *field_name);
        NX_CHECK(listener.has_value());
        NX_CHECK(std::string(listener->get().state) == "Parking");
        NX_CHECK(publication->to_string().find("groups=2") != std::string::npos);
    }

    // Prepare-for-transition: the pre-notification reaches a subscriber.
    {
        const auto prep_name = com::ServiceName::create("Nexigen/Test/Preparation");
        NX_CHECK(prep_name.has_value());
        auto preparation = sm::TransitionPreparation::create(*node, *prep_name);
        NX_CHECK(preparation.has_value());
        auto subscriber =
            com::Subscriber<sm::TransitionPreparationPayload>::create(*node, *prep_name);
        NX_CHECK(subscriber.has_value());

        NX_CHECK(preparation->notify("SafeState").has_value());
        bool observed = false;
        for (int i = 0; i < 200 && !observed; ++i) {
            auto received = subscriber->receive();
            NX_CHECK(received.has_value());
            if (received->has_value()) {
                observed = std::string((*received)->payload().target_state) == "SafeState";
            } else {
                std::this_thread::sleep_for(std::chrono::milliseconds(10));
            }
        }
        NX_CHECK(observed);
    }
}

void test_recovery_handler() {
    auto node = com::Node::create();
    NX_CHECK(node.has_value());
    const auto channel = com::ServiceName::create("Nexigen/Test/SmRecovery");
    const auto event = com::ServiceName::create("Nexigen/Test/SmRecoveryEvent");
    NX_CHECK(channel.has_value());
    NX_CHECK(event.has_value());

    sm::StateManager manager = make_manager();
    auto handler = sm::RecoveryHandler::create(*node, *channel, *event, manager);
    NX_CHECK(handler.has_value());

    // Mock PHM: publish a fault notification and raise the wake event.
    auto notifier = com::Publisher<sm::RecoveryNotification>::create(*node, *channel);
    auto event_notifier = com::EventNotifier::create(*node, *event);
    NX_CHECK(notifier.has_value());
    NX_CHECK(event_notifier.has_value());

    sm::RecoveryNotification fault{};
    std::snprintf(fault.function_group, sizeof(fault.function_group), "motion_control");
    auto loaned = notifier->loan_uninit();
    NX_CHECK(loaned.has_value());
    auto sample = loaned->write_payload(fault);
    NX_CHECK(notifier->send(std::move(sample)).has_value());
    NX_CHECK(event_notifier->notify().has_value());

    handler->handle_pending();
    NX_CHECK(manager.state(0) == "SafeState");
    NX_CHECK(handler->to_string().find("motion_control") != std::string::npos);
    NX_CHECK(std::format("{}", *handler) == handler->to_string());
}

void test_telemetry() {
    // Audit: records land in the log asynchronously without blocking.
    {
        sm::TransitionAudit audit("/tmp/opencode/sm_audit_test.log");
        for (std::uint32_t i = 0; i < 3; ++i) {
            sm::TransitionRecord record;
            record.timestamp_ns = 1000 + i;
            std::snprintf(record.function_group, sizeof(record.function_group), "motion_control");
            std::snprintf(record.old_state, sizeof(record.old_state), "Active");
            std::snprintf(record.new_state, sizeof(record.new_state), "Parking");
            std::snprintf(record.requester_id, sizeof(record.requester_id), "app");
            record.result = sm::TransitionResult::Success;
            audit.record(record);
        }
        for (int i = 0; i < 200 && audit.recorded() < 3U; ++i) {
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
        }
        NX_CHECK(audit.recorded() == 3U);
        NX_CHECK(audit.dropped() == 0U);
        NX_CHECK(audit.to_string().find("recorded=3") != std::string::npos);
    }

    // Metrics: counters reflect events and render through to_string/formatter.
    {
        sm::SmMetrics metrics;
        metrics.rejected_requests_total.fetch_add(2, std::memory_order_relaxed);
        metrics.emergency_transitions_total.fetch_add(1, std::memory_order_relaxed);
        metrics.transition_timeouts_total.fetch_add(1, std::memory_order_relaxed);
        const std::string text = metrics.to_string();
        NX_CHECK(text.find("rejected_requests_total=2") != std::string::npos);
        NX_CHECK(text.find("emergency_transitions_total=1") != std::string::npos);
        NX_CHECK(to_string(metrics) == text);
        NX_CHECK(std::format("{}", metrics) == text);
    }

    // Transition record renders all fields.
    {
        sm::TransitionRecord record;
        std::snprintf(record.function_group, sizeof(record.function_group), "motion_control");
        std::snprintf(record.old_state, sizeof(record.old_state), "Active");
        std::snprintf(record.new_state, sizeof(record.new_state), "SafeState");
        std::snprintf(record.requester_id, sizeof(record.requester_id), "phm");
        record.result = sm::TransitionResult::Success;
        NX_CHECK(record.to_string().find("SafeState") != std::string::npos);
        NX_CHECK(std::format("{}", record) == record.to_string());
    }
}

void test_manifest() {
    sm::SmManifest manifest = make_manifest();
    manifest.bg_thread.set_cpus(nexigen::core::CpuSet::from_core_mask(0b10ULL));
    NX_CHECK(manifest.bg_thread.set_name("sm-bg").has_value());
    manifest.audit_thread.set_cpus(nexigen::core::CpuSet::from_core_mask(1ULL));
    NX_CHECK(manifest.audit_thread.set_name("sm-audit").has_value());
    nlohmann::json j;
    to_json(j, manifest);
    sm::SmManifest back;
    from_json(j, back);
    NX_CHECK(back.function_groups.size() == 2U);
    NX_CHECK(back.function_groups[0].name == "motion_control");
    NX_CHECK(back.function_groups[0].states.size() == 4U);
    NX_CHECK(back.function_groups[0].allowed_transitions.size() == 6U);
    NX_CHECK(back.function_groups[0].transition_max_time_ms == 500U);
    NX_CHECK(back.function_groups[0].preparation_timeout_ms == 100U);
    NX_CHECK(!back.function_groups[0].application_group);
    NX_CHECK(back.function_groups[1].name == "adas");
    NX_CHECK(back.function_groups[1].application_group);
    NX_CHECK(back.requester_priorities.size() == 3U);
    NX_CHECK(back.requester_priorities[0].requester_id == "phm");
    NX_CHECK(back.requester_priorities[0].priority == 90);
    NX_CHECK(back.priority_of("phm") == 90);
    NX_CHECK(back.priority_of("unknown") == 0);
    NX_CHECK(back.bg_thread == manifest.bg_thread);
    NX_CHECK(back.audit_thread == manifest.audit_thread);

    sm::SmManifest legacy;
    from_json(nlohmann::json{{"function_groups", nlohmann::json::array()}}, legacy);
    NX_CHECK(!legacy.bg_thread.has_requests());
    NX_CHECK(!legacy.audit_thread.has_requests());

    const std::string text = manifest.to_string();
    NX_CHECK(text.find("motion_control") != std::string::npos);
    NX_CHECK(text.find("audit_thread") != std::string::npos);
    NX_CHECK(to_string(manifest) == text);
    NX_CHECK(std::format("{}", manifest) == text);

    // Hostile input leaves the target unchanged.
    from_json(nlohmann::json::array(), manifest);
    NX_CHECK(manifest.function_groups.size() == 2U);
    from_json(nlohmann::json{{"function_groups", nlohmann::json::array({42})}}, manifest);
    NX_CHECK(manifest.function_groups.size() == 1U);

    // Application groups must declare Off and start Off/SafeState
    // (spec sm/state-machine "Startup and shutdown sequencing").
    sm::SmManifest valid = make_manifest();
    NX_CHECK(sm::validate(valid));
    sm::SmManifest bypass = make_manifest();
    bypass.function_groups[1].states = {"Active", "Off"};
    NX_CHECK(!sm::validate(bypass));
    sm::SmManifest no_off = make_manifest();
    no_off.function_groups[1].states = {"SafeState", "Active"};
    NX_CHECK(!sm::validate(no_off));

    nlohmann::json invalid = j;
    invalid["function_groups"][1]["states"] = {"Active", "Off"};
    sm::SmManifest parsed_invalid;
    from_json(invalid, parsed_invalid);
    NX_CHECK(parsed_invalid.function_groups.size() == 2U);
    NX_CHECK(!sm::validate(parsed_invalid));

    // The production factory rejects an invalid manifest before constructing
    // a manager that could initialize an application group in the wrong state.
    NX_CHECK(sm::StateManager::create(make_manifest()).has_value());
    NX_CHECK(!sm::StateManager::create(parsed_invalid).has_value());
}

bool wait_thread_ready(const sm::TransitionAudit& audit) {
    for (int i = 0; i < 2000 && !audit.thread_ready(); ++i) {
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    return audit.thread_ready();
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
    NX_CHECK(bg.set_name("sm-bg").has_value());
    bg.set_cpus(nexigen::core::CpuSet::from_core_mask(first));

    nexigen::core::ThreadSpec audit_spec{};
    NX_CHECK(audit_spec.set_name("sm-audit").has_value());
    audit_spec.set_cpus(nexigen::core::CpuSet::from_core_mask(second));

    const std::string path_a =
        "/tmp/opencode/sm_thread_spec_a_" + std::to_string(static_cast<unsigned long>(::getpid()));
    const std::string path_b =
        "/tmp/opencode/sm_thread_spec_b_" + std::to_string(static_cast<unsigned long>(::getpid()));
    const std::string path_c =
        "/tmp/opencode/sm_thread_spec_c_" + std::to_string(static_cast<unsigned long>(::getpid()));
    {
        sm::TransitionAudit primary(path_a, nexigen::core::effective_thread_spec(audit_spec, bg));
        NX_CHECK(wait_thread_ready(primary));
        NX_CHECK((primary.thread_report().applied() &
                  nexigen::core::ThreadApplyReport::name_mask) != 0U);
        NX_CHECK((primary.thread_mask() & second) != 0U);
    }
    {
        sm::TransitionAudit fallback(
            path_b, nexigen::core::effective_thread_spec(nexigen::core::ThreadSpec{}, bg));
        NX_CHECK(wait_thread_ready(fallback));
        NX_CHECK((fallback.thread_report().applied() &
                  nexigen::core::ThreadApplyReport::name_mask) != 0U);
        NX_CHECK((fallback.thread_mask() & first) != 0U);
    }
    std::uint64_t inherited = 0U;
    NX_CHECK(nexigen::core::affinity::self_affinity(inherited).has_value());
    {
        sm::TransitionAudit neutral(
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

void test_root_lifecycle_client() {
    auto node = com::Node::create();
    NX_CHECK(node.has_value());

    const auto query_name =
        com::ServiceName::create("Nexigen/EM/InitialRootStateTransitionResult");
    const auto command_name = com::ServiceName::create("Nexigen/EM/StateCommand");
    NX_CHECK(query_name.has_value() && command_name.has_value());
    auto query_server =
        com::Server<std::monostate, nexigen::exec::InitialRootStateTransitionResultResponse>::
            create(*node, *query_name);
    auto command_server = com::Server<nexigen::exec::StateCommand, nexigen::exec::StateAck>::create(
        *node, *command_name);
    NX_CHECK(query_server.has_value() && command_server.has_value());

    std::atomic<bool> stop{false};
    std::atomic<int> command_calls{0};
    std::string last_requester;
    std::vector<std::uint64_t> request_ids;
    std::thread serve([&] {
        while (!stop.load(std::memory_order_acquire)) {
            auto query = query_server->receive();
            if (query && query->has_value()) {
                static_cast<void>(
                    (*query)->send_copy(nexigen::exec::InitialRootStateTransitionResultResponse{
                        nexigen::exec::StateResult::Success}));
            }
            auto command = command_server->receive();
            if (command && command->has_value()) {
                last_requester = (*command)->payload().requester_id;
                request_ids.push_back((*command)->payload().request_id);
                const int call = command_calls.fetch_add(1, std::memory_order_relaxed);
                const nexigen::exec::StateResult result =
                    call == 0 ? nexigen::exec::StateResult::InProgress
                              : nexigen::exec::StateResult::Success;
                static_cast<void>((*command)->send_copy(nexigen::exec::StateAck{1U, result}));
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
    });

    auto client = sm::RootLifecycleClient::create(*node, "sm_app");
    NX_CHECK(client.has_value());
    if (client) {
        NX_CHECK(client->available());
        NX_CHECK(client->report_ready().has_value());
        const auto initial = client->initial_transition_result(std::chrono::milliseconds(500));
        NX_CHECK(initial.has_value() && *initial == nexigen::exec::StateResult::Success);
        const auto verify = client->request_state("Verify", std::chrono::milliseconds(500));
        NX_CHECK(verify.has_value() && *verify == nexigen::exec::StateResult::Success);
        NX_CHECK(command_calls.load(std::memory_order_relaxed) >= 2);
    }

    stop.store(true, std::memory_order_release);
    serve.join();
    NX_CHECK(last_requester == "sm_app");
    NX_CHECK(request_ids.size() >= 2U);
    if (request_ids.size() >= 2U) {
        NX_CHECK(request_ids[0] != 0U);
        NX_CHECK(request_ids[0] == request_ids[1]);
    }
}

void test_ucm_coordination() {
    auto node = com::Node::create();
    NX_CHECK(node.has_value());
    sm::SmManifest manifest = make_manifest();
    sm::FunctionGroup ucm;
    ucm.name = "ucm_group";
    ucm.states = {"Off", "Run", "Update"};
    ucm.allowed_transitions = {{"Off", "Run"}, {"Run", "Off"}, {"Run", "Update"}};
    ucm.application_group = true;
    ucm.transition_max_time_ms = 500;
    manifest.function_groups.push_back(ucm);
    sm::FunctionGroup machine;
    machine.name = "RootFG";
    machine.states = {"Off", "Update"};
    machine.application_group = false;
    manifest.function_groups.push_back(machine);

    auto manager_result = sm::StateManager::create(manifest);
    NX_CHECK(manager_result.has_value());
    if (!manager_result) {
        return;
    }
    sm::StateManager& manager = *manager_result;
    NX_CHECK(manager.begin_startup());
    NX_CHECK(manager.advance_startup());
    NX_CHECK(manager.confirm_startup_step());

    const auto em_name = com::ServiceName::create("Nexigen/Test/UcmEm");
    NX_CHECK(em_name.has_value());
    auto em_server =
        com::Server<nexigen::exec::StateCommand, nexigen::exec::StateAck>::create(*node, *em_name);
    auto em_client =
        com::Client<nexigen::exec::StateCommand, nexigen::exec::StateAck>::create(*node, *em_name);
    NX_CHECK(em_server.has_value() && em_client.has_value());
    std::atomic<bool> stop{false};
    std::atomic<int> mode{0}; // 0 = Success, 1 = OperationFailed
    std::thread serve([&] {
        while (!stop.load(std::memory_order_acquire)) {
            auto request = em_server->receive();
            if (request && request->has_value()) {
                const auto result = mode.load(std::memory_order_relaxed) == 0
                                        ? nexigen::exec::StateResult::Success
                                        : nexigen::exec::StateResult::OperationFailed;
                static_cast<void>((*request)->send_copy(nexigen::exec::StateAck{1U, result}));
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
    });

    sm::UpdateCoordinator coordinator(manager, *em_client, "sm");
    auto request = [](sm::UpdateAction action, const char* id, const char* target) {
        sm::UpdateRequest value{};
        std::snprintf(value.function_group, sizeof(value.function_group), "%s", "ucm_group");
        std::snprintf(value.update_id, sizeof(value.update_id), "%s", id);
        std::snprintf(value.target_state, sizeof(value.target_state), "%s", target);
        value.action = action;
        return value;
    };

    const std::size_t ucm_index = manager.find_group_index("ucm_group");
    const std::size_t adas_index = manager.find_group_index("adas");
    NX_CHECK(manager.request_transition(ucm_index, "Run", "sm") == sm::TransitionResult::Success);
    NX_CHECK(coordinator.handle(request(sm::UpdateAction::Start, "u1", "")).result ==
             sm::UpdateResult::Accepted);
    NX_CHECK(manager.state(ucm_index) == "Update");
    NX_CHECK(coordinator.locked(ucm_index));
    NX_CHECK(sm::request_with_update_lock(manager, &coordinator, ucm_index, "Run", "sm") ==
             sm::TransitionResult::RejectedByUpdate);
    NX_CHECK(sm::request_with_update_lock(manager, &coordinator, adas_index, "Active", "app") ==
             sm::TransitionResult::Success);
    NX_CHECK(coordinator.handle(request(sm::UpdateAction::Start, "u2", "")).result ==
             sm::UpdateResult::Busy);
    NX_CHECK(coordinator.handle(request(sm::UpdateAction::Complete, "wrong", "Run")).result ==
             sm::UpdateResult::Rejected);
    NX_CHECK(coordinator.handle(request(sm::UpdateAction::Complete, "u1", "Run")).result ==
             sm::UpdateResult::Accepted);
    NX_CHECK(manager.state(ucm_index) == "Run");
    NX_CHECK(!coordinator.locked(ucm_index));

    NX_CHECK(coordinator.handle(request(sm::UpdateAction::Start, "u3", "")).result ==
             sm::UpdateResult::Accepted);
    NX_CHECK(coordinator.handle(request(sm::UpdateAction::Cancel, "u3", "")).result ==
             sm::UpdateResult::Accepted);
    NX_CHECK(manager.state(ucm_index) == "Run");

    sm::UpdateRequest machine_request{};
    std::snprintf(machine_request.function_group, sizeof(machine_request.function_group), "%s",
                  "RootFG");
    std::snprintf(machine_request.update_id, sizeof(machine_request.update_id), "%s", "u4");
    machine_request.action = sm::UpdateAction::Start;
    NX_CHECK(coordinator.handle(machine_request).result == sm::UpdateResult::Rejected);

    mode.store(1, std::memory_order_relaxed);
    NX_CHECK(coordinator.handle(request(sm::UpdateAction::Start, "u5", "")).result ==
             sm::UpdateResult::EmFailure);
    NX_CHECK(!coordinator.locked(ucm_index));

    mode.store(0, std::memory_order_relaxed);
    const auto ucm_name = com::ServiceName::create("Nexigen/Test/Ucm");
    NX_CHECK(ucm_name.has_value());
    auto ucm_server = com::Server<sm::UpdateRequest, sm::UpdateResponse>::create(*node, *ucm_name);
    auto ucm_client = com::Client<sm::UpdateRequest, sm::UpdateResponse>::create(*node, *ucm_name);
    NX_CHECK(ucm_server.has_value() && ucm_client.has_value());
    std::thread ucm_loop([&] {
        for (int i = 0; i < 200; ++i) {
            (void)coordinator.serve_once(*ucm_server);
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
    });
    const auto start_response = ucm_client->call(request(sm::UpdateAction::Start, "u6", ""),
                                                 std::chrono::milliseconds(500));
    NX_CHECK(start_response.has_value() && start_response->result == sm::UpdateResult::Accepted);
    const auto cancel_response = ucm_client->call(request(sm::UpdateAction::Cancel, "u6", ""),
                                                  std::chrono::milliseconds(500));
    NX_CHECK(cancel_response.has_value() && cancel_response->result == sm::UpdateResult::Accepted);
    ucm_loop.join();

    stop.store(true, std::memory_order_release);
    serve.join();
}

struct RecordingLsmSink final : sm::LsmActionSink {
    std::vector<std::string> calls{};
    bool fail_requests{false};

    nexigen::core::Status request_state(std::string_view group, std::string_view state,
                                        std::string_view) override {
        calls.push_back("request:" + std::string(group) + ":" + std::string(state));
        return fail_requests ? std::unexpected(nexigen::core::ErrorCode::SystemError)
                             : nexigen::core::Status{};
    }

    nexigen::core::Status publish_state(std::string_view group, std::string_view state) override {
        calls.push_back("publish:" + std::string(group) + ":" + std::string(state));
        return {};
    }
};

sm::LightweightStateMachineConfig make_lsm_config() {
    sm::LightweightStateMachineConfig config;
    config.name = "lsm";
    config.states = {"Off", "Active", "Fallback"};
    config.initial_state = "Off";
    config.actions = {
        {sm::LsmActionType::PublishState, "adas", "Off"},
        {sm::LsmActionType::RequestFunctionGroupState, "adas", "Active"},
        {sm::LsmActionType::PublishState, "adas", "Fallback"},
    };
    config.transitions = {
        {"Off", "Active", {0, 1}},
    };
    config.error_recovery = {
        {"Active", "Fallback", {2}},
    };
    return config;
}

void test_lightweight_state_machine() {
    RecordingLsmSink sink;
    auto machine = sm::LightweightStateMachine::create(make_lsm_config(), sink);
    NX_CHECK(machine.has_value());
    if (!machine) {
        return;
    }
    NX_CHECK(machine->request("Active", "app").has_value());
    NX_CHECK(machine->current_state() == "Active");
    NX_CHECK(sink.calls.size() == 2U);
    NX_CHECK(sink.calls[0] == "publish:adas:Off");
    NX_CHECK(sink.calls[1] == "request:adas:Active");

    sink.calls.clear();
    const auto missing = machine->request("Missing", "app");
    NX_CHECK(missing.has_value() && *missing == sm::TransitionResult::InvalidGraph);
    NX_CHECK(machine->current_state() == "Active");

    nlohmann::json json;
    sm::to_json(json, make_lsm_config());
    sm::LightweightStateMachineConfig parsed;
    sm::from_json(json, parsed);
    NX_CHECK(parsed.name == "lsm" && parsed.transitions.size() == 1U);
    NX_CHECK(std::format("{}", make_lsm_config()) == make_lsm_config().to_string());

    RecordingLsmSink failing;
    auto failing_machine = sm::LightweightStateMachine::create(make_lsm_config(), failing);
    NX_CHECK(failing_machine.has_value());
    if (failing_machine) {
        failing.fail_requests = true;
        const auto result = failing_machine->request("Active", "app");
        NX_CHECK(!result.has_value());
        NX_CHECK(failing_machine->current_state() == "Fallback");
        NX_CHECK(failing.calls.back() == "publish:adas:Fallback");
    }

    sm::LightweightStateMachineConfig no_recovery = make_lsm_config();
    no_recovery.error_recovery.clear();
    RecordingLsmSink no_rule;
    auto no_rule_machine = sm::LightweightStateMachine::create(no_recovery, no_rule);
    NX_CHECK(no_rule_machine.has_value());
    if (no_rule_machine) {
        no_rule.fail_requests = true;
        NX_CHECK(!no_rule_machine->request("Active", "app").has_value());
        NX_CHECK(no_rule_machine->current_state() == "Off");
    }

    sm::LightweightStateMachineConfig invalid = make_lsm_config();
    invalid.transitions.front().actions.push_back(99);
    NX_CHECK(!invalid.validate().has_value());

    sm::SmManifest manifest = make_manifest();
    manifest.state_machines.push_back(make_lsm_config());
    NX_CHECK(sm::validate(manifest).has_value());
    nlohmann::json manifest_json;
    sm::to_json(manifest_json, manifest);
    sm::SmManifest manifest_parsed;
    sm::from_json(manifest_json, manifest_parsed);
    NX_CHECK(manifest_parsed.state_machines.size() == 1U);
    NX_CHECK(manifest_parsed.state_machines.front().name == "lsm");
}

} // namespace

int main() {
    run_suite("vocabulary", test_vocabulary);
    run_suite("state_machine", test_state_machine);
    run_suite("ipc", test_ipc);
    run_suite("recovery_handler", test_recovery_handler);
    run_suite("telemetry", test_telemetry);
    run_suite("manifest", test_manifest);
    run_suite("thread_spec", test_thread_spec);
    run_suite("root_lifecycle", test_root_lifecycle_client);
    run_suite("ucm_coordination", test_ucm_coordination);
    run_suite("lightweight_state_machine", test_lightweight_state_machine);
    std::printf("TOTAL %d checks, %d failed\n", g_checks, g_failures);
    return g_failures == 0 ? 0 : 1;
}
