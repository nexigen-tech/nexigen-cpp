// Тесты nexigen::sm — харнес как в остальных модулях (NX_CHECK + run_suite).
// Сборка только при NEXIGEN_ENABLE_COM=ON.
//
// Наборы: vocabulary (enum'ы), state_machine (граф/арбитраж/sequencing),
// ipc (Field-публикация + prepare), recovery_handler (PHM fault → SafeState),
// telemetry (audit + метрики), manifest (round-trip).
#include <chrono>
#include <cstdio>
#include <format>
#include <string>
#include <string_view>
#include <thread>
#include <vector>

#include <nlohmann/json.hpp>

#include <nexigen/com/event.hpp>
#include <nexigen/com/field.hpp>
#include <nexigen/com/node.hpp>
#include <nexigen/com/publisher.hpp>
#include <nexigen/com/service_name.hpp>
#include <nexigen/com/subscriber.hpp>
#include <nexigen/core/serialization_mandate.hpp>
#include <nexigen/sm/machine_state.hpp>
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
    manifest.function_groups.push_back(motion);
    manifest.requester_priorities = {{"phm", 90}, {"diagnostics", 50}, {"app", 10}};
    manifest.metrics_flush_interval_ms = 100;
    manifest.startup_timeout_ms = 5000;
    return manifest;
}

void test_vocabulary() {
    for (const sm::TransitionResult value :
         {sm::TransitionResult::Success, sm::TransitionResult::RejectedByPriority,
          sm::TransitionResult::InvalidGraph, sm::TransitionResult::Timeout}) {
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
    for (const sm::MachineState value :
         {sm::MachineState::Startup, sm::MachineState::Verify, sm::MachineState::NormalRunning,
          sm::MachineState::Shutdown}) {
        const std::string_view name = to_string(value);
        sm::MachineState back{};
        NX_CHECK(from_string(name, back));
        NX_CHECK(back == value);
        NX_CHECK(std::format("{}", value) == std::string(name));
    }
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
        sm::StateManager manager(make_manifest());
        const sm::TransitionResult result = manager.request_transition(0, "Off", "app");
        NX_CHECK(result == sm::TransitionResult::InvalidGraph);
        NX_CHECK(manager.state(0) == "Active");
    }

    // Valid transition is applied.
    {
        sm::StateManager manager(make_manifest());
        const sm::TransitionResult result = manager.request_transition(0, "Parking", "app");
        NX_CHECK(result == sm::TransitionResult::Success);
        NX_CHECK(manager.state(0) == "Parking");
    }

    // Arbitration: a higher-priority requester wins over the lower holder.
    {
        sm::StateManager manager(make_manifest());
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

    // Startup/shutdown sequencing.
    {
        sm::StateManager manager(make_manifest());
        NX_CHECK(manager.machine_state() == sm::MachineState::Startup);
        NX_CHECK(manager.advance_startup());
        NX_CHECK(manager.machine_state() == sm::MachineState::Verify);
        NX_CHECK(manager.confirm_startup_step());
        NX_CHECK(manager.machine_state() == sm::MachineState::NormalRunning);
        manager.begin_shutdown();
        NX_CHECK(manager.machine_state() == sm::MachineState::Shutdown);
        NX_CHECK(manager.state(0) == "Off");
    }

    // Diagnostics render machine + group states.
    {
        sm::StateManager manager(make_manifest());
        const std::string text = manager.to_string();
        NX_CHECK(text.find("machine=Startup") != std::string::npos);
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
        sm::StateManager manager(make_manifest());
        auto publication = sm::StatePublication::create(*node, make_manifest());
        NX_CHECK(publication.has_value());
        NX_CHECK(publication->publish(0, "Parking").has_value());

        const auto field_name = com::ServiceName::create("Nexigen/SM/State/motion_control");
        NX_CHECK(field_name.has_value());
        auto listener = com::FieldListener<sm::GroupStatePayload>::open(*node, *field_name);
        NX_CHECK(listener.has_value());
        NX_CHECK(std::string(listener->get().state) == "Parking");
        NX_CHECK(publication->to_string().find("groups=1") != std::string::npos);
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

    sm::StateManager manager(make_manifest());
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
    nlohmann::json j;
    to_json(j, manifest);
    sm::SmManifest back;
    from_json(j, back);
    NX_CHECK(back.function_groups.size() == 1U);
    NX_CHECK(back.function_groups[0].name == "motion_control");
    NX_CHECK(back.function_groups[0].states.size() == 4U);
    NX_CHECK(back.function_groups[0].allowed_transitions.size() == 6U);
    NX_CHECK(back.function_groups[0].transition_max_time_ms == 500U);
    NX_CHECK(back.function_groups[0].preparation_timeout_ms == 100U);
    NX_CHECK(back.requester_priorities.size() == 3U);
    NX_CHECK(back.requester_priorities[0].requester_id == "phm");
    NX_CHECK(back.requester_priorities[0].priority == 90);
    NX_CHECK(back.priority_of("phm") == 90);
    NX_CHECK(back.priority_of("unknown") == 0);

    const std::string text = manifest.to_string();
    NX_CHECK(text.find("motion_control") != std::string::npos);
    NX_CHECK(to_string(manifest) == text);
    NX_CHECK(std::format("{}", manifest) == text);

    // Hostile input leaves the target unchanged.
    from_json(nlohmann::json::array(), manifest);
    NX_CHECK(manifest.function_groups.size() == 1U);
    from_json(nlohmann::json{{"function_groups", nlohmann::json::array({42})}}, manifest);
    NX_CHECK(manifest.function_groups.size() == 1U);
}

} // namespace

int main() {
    run_suite("vocabulary", test_vocabulary);
    run_suite("state_machine", test_state_machine);
    run_suite("ipc", test_ipc);
    run_suite("recovery_handler", test_recovery_handler);
    run_suite("telemetry", test_telemetry);
    run_suite("manifest", test_manifest);
    std::printf("TOTAL %d checks, %d failed\n", g_checks, g_failures);
    return g_failures == 0 ? 0 : 1;
}
