// nexigen-phmd safety-loop e2e helper (change complete-phm-safety-loop).
//
// A small, test-only process that plays every counterpart the daemon talks to
// during a recovery, so `safety_loop_test.sh` can drive the whole ladder
// without a real Execution Manager, State Manager, or supervised application:
//
//   * the supervised application — emits checkpoints, then stops;
//   * the Execution Manager    — serves `Nexigen/PHM/Recovery`, admits the
//     restart, and publishes the lifecycle ladder up to `Failed`;
//   * the State Manager        — receives `Nexigen/SM/Recovery`, publishes the
//     committed group state, and answers `Nexigen/SM/RecoveryAck`.
//
// Modes:
//   acked    — the SM acknowledges with `Accepted`;
//   rejected — the SM acknowledges with `Rejected`/`NoSafeState`;
//   noack    — the SM stays silent, so the daemon's budget expires.
//
// It writes a one-line summary so the shell driver can assert what happened.
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <optional>
#include <string>
#include <string_view>
#include <thread>

#include <nexigen/com/event.hpp>
#include <nexigen/com/field.hpp>
#include <nexigen/com/node.hpp>
#include <nexigen/com/publisher.hpp>
#include <nexigen/com/server.hpp>
#include <nexigen/com/service_name.hpp>
#include <nexigen/com/subscriber.hpp>
#include <nexigen/exec/process_lifecycle.hpp>
#include <nexigen/phm/checkpoint.hpp>
#include <nexigen/phm/recovery_ack.hpp>
#include <nexigen/phm/recovery_request.hpp>
#include <nexigen/phm/recovery_response.hpp>
#include <nexigen/sm/group_state_payload.hpp>
#include <nexigen/sm/recovery_notification.hpp>

namespace {

using namespace std::chrono_literals;

/// @brief Status returned when a channel cannot be opened.
constexpr int kChannelFailure = 3;

/// @brief How long the harness lets the transport discover its endpoints.
constexpr auto k_warmup = 1200ms;

/// @brief How long the harness waits for one daemon reaction.
constexpr auto k_reaction_timeout = 3000ms;

/// @brief How long the harness keeps answering after the last verdict.
constexpr auto k_drain = 900ms;

[[nodiscard]] std::optional<nexigen::com::ServiceName> make_name(std::string_view text) {
    const std::string owned(text);
    auto name = nexigen::com::ServiceName::create(owned.c_str());
    if (!name) {
        return std::nullopt;
    }
    return std::move(*name);
}

[[nodiscard]] auto now() {
    return std::chrono::steady_clock::now();
}

/// @brief The lifecycle channel the daemon subscribes to for one process.
struct LifecycleChannel {
    nexigen::com::Publisher<nexigen::exec::ProcessLifecycleEvent> publisher;
    nexigen::com::EventNotifier event;
};

/// @brief Counters written to the summary file.
struct Counters {
    std::uint32_t em_requests{0};
    std::uint32_t sm_notifications{0};
    bool ack_sent{false};
};

/// @brief Everything the helper opened.
struct Harness {
    std::optional<LifecycleChannel> lifecycle{};
    std::optional<nexigen::phm::CheckpointEmitter> checkpoints{};
    std::optional<
        nexigen::com::Server<nexigen::phm::RecoveryRequest, nexigen::phm::RecoveryResponse>>
        em_server{};
    std::optional<nexigen::com::Subscriber<nexigen::sm::RecoveryNotification>> sm_subscriber{};
    std::optional<nexigen::com::Publisher<nexigen::phm::RecoveryAck>> ack_publisher{};
    std::optional<nexigen::com::EventNotifier> ack_event{};
    std::optional<nexigen::com::Field<nexigen::sm::GroupStatePayload>> group_state{};
};

/// @brief Publish one lifecycle sample carrying the process's Function Group.
[[nodiscard]] bool publish_lifecycle(LifecycleChannel& channel, std::string_view app,
                                     std::string_view group,
                                     nexigen::exec::ProcessLifecycleState state) {
    nexigen::exec::ProcessLifecycleEvent sample{};
    std::snprintf(sample.app, sizeof(sample.app), "%.*s", static_cast<int>(app.size()), app.data());
    std::snprintf(sample.function_group, sizeof(sample.function_group), "%.*s",
                  static_cast<int>(group.size()), group.data());
    sample.state = state;
    if (!channel.publisher.send_copy(sample)) {
        return false;
    }
    return channel.event.notify().has_value();
}

/// @brief Open the EM lifecycle channel the daemon subscribes to.
[[nodiscard]] bool open_lifecycle(Harness& harness, nexigen::com::Node& node) {
    const auto sample_name = make_name("Nexigen/PHM/ExecutionState");
    const auto event_name = make_name("Nexigen/PHM/ExecutionStateEvent");
    if (!sample_name || !event_name) {
        return false;
    }
    auto publisher = nexigen::com::Publisher<nexigen::exec::ProcessLifecycleEvent>::create(
        node, *sample_name, 64);
    auto event = nexigen::com::EventNotifier::create(node, *event_name);
    if (!publisher || !event) {
        return false;
    }
    harness.lifecycle.emplace(LifecycleChannel{std::move(*publisher), std::move(*event)});
    return true;
}

/// @brief Open the application-side checkpoint emitter.
[[nodiscard]] bool open_checkpoints(Harness& harness, nexigen::com::Node& node,
                                    std::string_view checkpoint_service) {
    const auto name = make_name(checkpoint_service);
    if (!name) {
        return false;
    }
    auto emitter = nexigen::phm::CheckpointEmitter::create(node, *name);
    if (!emitter) {
        return false;
    }
    harness.checkpoints.emplace(std::move(*emitter));
    return true;
}

/// @brief Open the Execution Manager request server.
[[nodiscard]] bool open_em(Harness& harness, nexigen::com::Node& node) {
    const auto name = make_name("Nexigen/PHM/Recovery");
    if (!name) {
        return false;
    }
    auto server =
        nexigen::com::Server<nexigen::phm::RecoveryRequest, nexigen::phm::RecoveryResponse>::create(
            node, *name);
    if (!server) {
        return false;
    }
    harness.em_server.emplace(std::move(*server));
    return true;
}

/// @brief Open the State Manager notification, acknowledgement, and state.
[[nodiscard]] bool open_sm(Harness& harness, nexigen::com::Node& node, std::string_view group) {
    const auto notification_name = make_name("Nexigen/SM/Recovery");
    const auto ack_name = make_name("Nexigen/SM/RecoveryAck");
    const auto ack_event_name = make_name("Nexigen/SM/RecoveryAckEvent");
    std::string state_service = "Nexigen/SM/State/";
    state_service.append(group);
    const auto state_name = make_name(state_service);
    if (!notification_name || !ack_name || !ack_event_name || !state_name) {
        return false;
    }
    // PHM publishes the notification with transport defaults, so the subscriber
    // must not request a different depth.
    auto subscriber = nexigen::com::Subscriber<nexigen::sm::RecoveryNotification>::create(
        node, *notification_name);
    // PHM subscribes to the acknowledgement with a service maximum of 64.
    auto publisher =
        nexigen::com::Publisher<nexigen::phm::RecoveryAck>::create(node, *ack_name, 64);
    auto event = nexigen::com::EventNotifier::create(node, *ack_event_name);
    if (!subscriber || !publisher || !event) {
        return false;
    }
    nexigen::sm::GroupStatePayload initial{};
    std::snprintf(initial.state, sizeof(initial.state), "%s", "Active");
    auto field =
        nexigen::com::Field<nexigen::sm::GroupStatePayload>::create(node, *state_name, initial);
    if (!field) {
        return false;
    }
    harness.sm_subscriber.emplace(std::move(*subscriber));
    harness.ack_publisher.emplace(std::move(*publisher));
    harness.ack_event.emplace(std::move(*event));
    harness.group_state.emplace(std::move(*field));
    return true;
}

/// @brief Open every channel the scenario needs.
[[nodiscard]] bool open_harness(Harness& harness, nexigen::com::Node& node,
                                std::string_view checkpoint_service, std::string_view group) {
    return open_lifecycle(harness, node) && open_checkpoints(harness, node, checkpoint_service) &&
           open_em(harness, node) && open_sm(harness, node, group);
}

/// @brief Answer one pending EM request, if any.
void serve_em(Harness& harness, Counters& counters) noexcept {
    if (!harness.em_server) {
        return;
    }
    auto received = harness.em_server->receive();
    if (!received || !received->has_value()) {
        return;
    }
    ++counters.em_requests;
    const nexigen::phm::RecoveryResponse response{(*received)->payload().request_id,
                                                  nexigen::phm::RecoveryOutcome::Accepted,
                                                  nexigen::phm::RecoveryReason::None};
    static_cast<void>((*received)->send_copy(response));
}

/// @brief Drain one pending SM notification, if any.
[[nodiscard]] std::optional<std::uint64_t> drain_sm(Harness& harness, Counters& counters) noexcept {
    if (!harness.sm_subscriber) {
        return std::nullopt;
    }
    auto received = harness.sm_subscriber->receive();
    if (!received || !received->has_value()) {
        return std::nullopt;
    }
    ++counters.sm_notifications;
    return (*received)->payload().request_id;
}

/// @brief Publish one acknowledgement and wake the daemon.
void send_ack(Harness& harness, Counters& counters, std::uint64_t request_id,
              nexigen::phm::RecoveryOutcome outcome, nexigen::phm::RecoveryReason reason) noexcept {
    if (!harness.ack_publisher || !harness.ack_event) {
        return;
    }
    if (harness.ack_publisher->send_copy(nexigen::phm::RecoveryAck{request_id, outcome, reason})) {
        counters.ack_sent = harness.ack_event->notify().has_value();
    }
}

/// @brief Emit checkpoints and repeat `Running` until the transport settles.
///
/// Checkpoints flow from the first moment, so the daemon never sees a dead
/// process while the harness is still connecting. `Running` is repeated because
/// the daemon must know the Function Group before the alive window may fail.
[[nodiscard]] bool warmup(Harness& harness, std::string_view app, std::string_view group) {
    const auto deadline = now() + k_warmup;
    auto next_running = now();
    while (now() < deadline) {
        static_cast<void>(harness.checkpoints->emit(1U));
        if (now() >= next_running) {
            if (!publish_lifecycle(*harness.lifecycle, app, group,
                                   nexigen::exec::ProcessLifecycleState::Running)) {
                return false;
            }
            next_running = now() + 100ms;
        }
        std::this_thread::sleep_for(20ms);
    }
    return true;
}

/// @brief Report a few last checkpoints, then go silent.
void emit_then_stop(Harness& harness) {
    const auto deadline = now() + 200ms;
    while (now() < deadline) {
        static_cast<void>(harness.checkpoints->emit(1U));
        std::this_thread::sleep_for(20ms);
    }
}

/// @brief Wait until the daemon asks for a restart.
[[nodiscard]] bool wait_for_em(Harness& harness, Counters& counters) {
    const auto deadline = now() + k_reaction_timeout;
    while (now() < deadline && counters.em_requests == 0U) {
        serve_em(harness, counters);
        std::this_thread::sleep_for(5ms);
    }
    return counters.em_requests > 0U;
}

/// @brief Publish the lifecycle ladder that ends in a spent restart budget.
void publish_failure(Harness& harness, std::string_view app, std::string_view group) {
    static_cast<void>(publish_lifecycle(*harness.lifecycle, app, group,
                                        nexigen::exec::ProcessLifecycleState::Terminating));
    std::this_thread::sleep_for(20ms);
    static_cast<void>(publish_lifecycle(*harness.lifecycle, app, group,
                                        nexigen::exec::ProcessLifecycleState::Terminated));
    std::this_thread::sleep_for(20ms);
    static_cast<void>(publish_lifecycle(*harness.lifecycle, app, group,
                                        nexigen::exec::ProcessLifecycleState::Failed));
}

/// @brief Wait until the daemon escalates the exhausted process.
[[nodiscard]] std::optional<std::uint64_t> wait_for_sm(Harness& harness, Counters& counters) {
    const auto deadline = now() + k_reaction_timeout;
    while (now() < deadline) {
        serve_em(harness, counters);
        if (const auto id = drain_sm(harness, counters); id.has_value()) {
            return id;
        }
        std::this_thread::sleep_for(5ms);
    }
    return std::nullopt;
}

/// @brief Commit SafeState, then answer according to the mode.
void answer(Harness& harness, Counters& counters, std::string_view mode, std::uint64_t request_id) {
    if (mode == "noack") {
        return;
    }
    if (harness.group_state) {
        nexigen::sm::GroupStatePayload safe{};
        std::snprintf(safe.state, sizeof(safe.state), "%s", "SafeState");
        static_cast<void>(harness.group_state->set(std::move(safe)));
    }
    if (mode == "acked") {
        send_ack(harness, counters, request_id, nexigen::phm::RecoveryOutcome::Accepted,
                 nexigen::phm::RecoveryReason::None);
        return;
    }
    send_ack(harness, counters, request_id, nexigen::phm::RecoveryOutcome::Rejected,
             nexigen::phm::RecoveryReason::NoSafeState);
}

/// @brief Keep serving until the daemon's verdict must have been logged.
void drain(Harness& harness, Counters& counters) {
    const auto deadline = now() + k_drain;
    while (now() < deadline) {
        serve_em(harness, counters);
        static_cast<void>(drain_sm(harness, counters));
        std::this_thread::sleep_for(5ms);
    }
}

/// @brief Write the one-line summary the shell driver asserts on.
void write_summary(const Counters& counters, const std::string& outfile) {
    std::ofstream out(outfile, std::ios::trunc);
    out << "em_requests=" << counters.em_requests
        << " sm_notifications=" << counters.sm_notifications
        << " ack_sent=" << (counters.ack_sent ? 1 : 0) << '\n';
}

/// @brief Drive the whole ladder for one mode.
[[nodiscard]] int run(nexigen::com::Node& node, std::string_view checkpoint_service,
                      std::string_view app, std::string_view group, std::string_view mode,
                      const std::string& outfile) {
    Harness harness{};
    if (!open_harness(harness, node, checkpoint_service, group)) {
        std::fprintf(stderr, "safety-loop-helper: cannot open the harness channels\n");
        return kChannelFailure;
    }
    Counters counters{};

    if (!warmup(harness, app, group)) {
        std::fprintf(stderr, "safety-loop-helper: cannot publish Running\n");
        return kChannelFailure;
    }
    emit_then_stop(harness);
    if (!wait_for_em(harness, counters)) {
        std::fprintf(stderr, "safety-loop-helper: no recovery request within the timeout\n");
        return 1;
    }
    publish_failure(harness, app, group);
    const auto request_id = wait_for_sm(harness, counters);
    if (!request_id.has_value()) {
        std::fprintf(stderr, "safety-loop-helper: no SM notification within the timeout\n");
        return 1;
    }
    answer(harness, counters, mode, *request_id);
    drain(harness, counters);
    write_summary(counters, outfile);
    return 0;
}

} // namespace

int main(int argc, char* argv[]) {
    if (argc != 7) {
        std::fprintf(stderr,
                     "usage: %s drive <checkpoint-service> <app> <group> "
                     "<acked|rejected|noack> <outfile>\n",
                     argv[0]);
        return 2;
    }
    if (std::string_view(argv[1]) != "drive") {
        std::fprintf(stderr, "safety-loop-helper: unknown mode '%s'\n", argv[1]);
        return 2;
    }
    auto node = nexigen::com::Node::create();
    if (!node) {
        std::fprintf(stderr, "safety-loop-helper: node creation failed\n");
        return kChannelFailure;
    }
    return run(*node, argv[2], argv[3], argv[4], argv[5], argv[6]);
}
