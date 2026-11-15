// nexigen-phmd identity e2e helper (change checkpoint-identity).
//
// A small, test-only process that drives the daemon's identity channels:
//   * run            — claim and/or emit checkpoints for a fixed duration;
//   * watch-security — subscribe to the security-event channel and dump the
//                      first event;
//   * run --reset    — claim, then publish a Terminating/Terminated/Running
//                      lifecycle sequence and keep emitting without a new claim.
//
// It exists so `identity_test.sh` can prove the daemon-side gating, the
// security-event channel, and the per-incarnation reset without a full app.
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <optional>
#include <string>
#include <string_view>
#include <thread>

#include <unistd.h>

#include <nexigen/com/event.hpp>
#include <nexigen/com/node.hpp>
#include <nexigen/com/publisher.hpp>
#include <nexigen/com/service_name.hpp>
#include <nexigen/com/subscriber.hpp>
#include <nexigen/exec/process_lifecycle.hpp>
#include <nexigen/phm/checkpoint.hpp>
#include <nexigen/phm/checkpoint_claim.hpp>
#include <nexigen/phm/claim_publisher.hpp>
#include <nexigen/phm/security_event.hpp>

namespace {

using namespace std::chrono_literals;

constexpr auto k_checkpoint_period = 20ms;
constexpr auto k_claim_period = 100ms;
constexpr auto k_reset_at = 700ms;

/// @brief Status returned by the helper on a channel-creation failure.
constexpr int kChannelFailure = 3;

void sleep_for(std::chrono::milliseconds duration) {
    std::this_thread::sleep_for(duration);
}

[[nodiscard]] std::optional<nexigen::com::ServiceName> make_name(std::string_view text) {
    const std::string owned(text);
    auto name = nexigen::com::ServiceName::create(owned.c_str());
    if (!name) {
        return std::nullopt;
    }
    return std::move(*name);
}

[[nodiscard]] std::chrono::milliseconds parse_ms(const char* text) {
    return std::chrono::milliseconds(std::strtol(text, nullptr, 10));
}

/// @brief The optional lifecycle channel used by the reset scenario.
struct LifecycleChannel {
    nexigen::com::Publisher<nexigen::exec::ProcessLifecycleEvent> publisher;
    nexigen::com::EventNotifier event;
};

/// @brief Publish one lifecycle sample and wake the daemon.
[[nodiscard]] bool publish_lifecycle(LifecycleChannel& channel, std::string_view name,
                                     nexigen::exec::ProcessLifecycleState state) {
    nexigen::exec::ProcessLifecycleEvent sample{};
    std::snprintf(sample.app, sizeof(sample.app), "%.*s", static_cast<int>(name.size()),
                  name.data());
    sample.state = state;
    if (!channel.publisher.send_copy(sample)) {
        return false;
    }
    return channel.event.notify().has_value();
}

/// @brief Open the EM lifecycle channel the daemon subscribes to.
[[nodiscard]] std::optional<LifecycleChannel> open_lifecycle(nexigen::com::Node& node) {
    const auto sample_name = make_name("Nexigen/PHM/ExecutionState");
    const auto event_name = make_name("Nexigen/PHM/ExecutionStateEvent");
    if (!sample_name || !event_name) {
        return std::nullopt;
    }
    // The daemon subscribes with a service maximum of 64.
    auto publisher = nexigen::com::Publisher<nexigen::exec::ProcessLifecycleEvent>::create(
        node, *sample_name, 64);
    auto event = nexigen::com::EventNotifier::create(node, *event_name);
    if (!publisher || !event) {
        return std::nullopt;
    }
    return LifecycleChannel{std::move(*publisher), std::move(*event)};
}

/// @brief End the current incarnation and start a new one.
void publish_reset(LifecycleChannel& channel, std::string_view name) {
    static_cast<void>(
        publish_lifecycle(channel, name, nexigen::exec::ProcessLifecycleState::Terminating));
    sleep_for(50ms);
    static_cast<void>(
        publish_lifecycle(channel, name, nexigen::exec::ProcessLifecycleState::Terminated));
    sleep_for(50ms);
    static_cast<void>(
        publish_lifecycle(channel, name, nexigen::exec::ProcessLifecycleState::Running));
}

/// @brief Emit checkpoint id 1 and refresh the claim while it is active.
[[nodiscard]] bool drive(nexigen::com::Node& node, nexigen::phm::CheckpointEmitter& emitter,
                         std::optional<nexigen::phm::ClaimPublisher>& claimer,
                         std::string_view name, std::uint32_t claimed_uid, bool reset,
                         std::chrono::milliseconds duration) {
    std::optional<LifecycleChannel> lifecycle{};
    if (reset) {
        lifecycle = open_lifecycle(node);
        if (!lifecycle) {
            std::fprintf(stderr, "identity-helper: cannot open the lifecycle channel\n");
            return false;
        }
    }

    auto next_checkpoint = std::chrono::steady_clock::now();
    auto next_claim = next_checkpoint;
    const auto reset_at = next_checkpoint + k_reset_at;
    const auto deadline = next_checkpoint + duration;
    const bool claims = claimer.has_value();
    bool claim_active = claims;
    bool reset_done = false;

    while (std::chrono::steady_clock::now() < deadline) {
        const auto now = std::chrono::steady_clock::now();
        if (now >= next_checkpoint) {
            static_cast<void>(emitter.emit(1U));
            next_checkpoint += k_checkpoint_period;
        }
        if (claim_active && now >= next_claim) {
            static_cast<void>(
                claimer->claim(name, static_cast<std::uint32_t>(::getpid()), claimed_uid));
            next_claim += k_claim_period;
        }
        if (reset && !reset_done && now >= reset_at) {
            reset_done = true;
            // The new incarnation never claims, so only the daemon's registry
            // reset decides whether its checkpoints are trusted.
            claim_active = false;
            publish_reset(*lifecycle, name);
        }
        sleep_for(5ms);
    }
    return true;
}

struct ScenarioArgs {
    std::string checkpoint_service;
    std::string claim_sample;
    std::string claim_event;
    std::string name;
    std::string_view uid_mode;
    std::chrono::milliseconds duration{0};
    bool reset{false};
};

[[nodiscard]] std::optional<ScenarioArgs> parse_run_args(int argc, char* argv[]) {
    if (argc < 8 || argc > 9) {
        return std::nullopt;
    }
    if (argc == 9 && std::string_view(argv[8]) != "--reset") {
        return std::nullopt;
    }
    return ScenarioArgs{argv[2], argv[3], argv[4], argv[5], argv[6], parse_ms(argv[7]), argc == 9};
}

/// @brief Open the claim channel unless the scenario opts out.
[[nodiscard]] std::optional<nexigen::phm::ClaimPublisher>
open_claimer(nexigen::com::Node& node, const ScenarioArgs& args, std::uint32_t& claimed_uid) {
    if (args.uid_mode == "none" || args.claim_sample == "-" || args.claim_event == "-") {
        return std::nullopt;
    }
    const auto sample_name = make_name(args.claim_sample);
    const auto event_name = make_name(args.claim_event);
    if (!sample_name || !event_name) {
        std::fprintf(stderr, "identity-helper: invalid claim service name\n");
        std::exit(kChannelFailure);
    }
    auto publisher = nexigen::phm::ClaimPublisher::create(node, *sample_name, *event_name);
    if (!publisher) {
        std::fprintf(stderr, "identity-helper: cannot open the claim channel\n");
        std::exit(kChannelFailure);
    }
    if (args.uid_mode == "foreign") {
        claimed_uid += 1U;
    }
    return std::move(*publisher);
}

[[nodiscard]] int run_scenario(nexigen::com::Node& node, int argc, char* argv[]) {
    const std::optional<ScenarioArgs> args = parse_run_args(argc, argv);
    if (!args) {
        std::fprintf(stderr, "identity-helper: unknown mode or bad arguments\n");
        return 2;
    }
    const auto checkpoint_name = make_name(args->checkpoint_service);
    if (!checkpoint_name) {
        std::fprintf(stderr, "identity-helper: invalid checkpoint service name\n");
        return kChannelFailure;
    }
    auto emitter = nexigen::phm::CheckpointEmitter::create(node, *checkpoint_name);
    if (!emitter) {
        std::fprintf(stderr, "identity-helper: cannot open the checkpoint channel\n");
        return kChannelFailure;
    }

    std::uint32_t claimed_uid = ::getuid();
    std::optional<nexigen::phm::ClaimPublisher> claimer = open_claimer(node, *args, claimed_uid);
    if (!drive(node, *emitter, claimer, args->name, claimed_uid, args->reset, args->duration)) {
        return kChannelFailure;
    }
    return 0;
}

[[nodiscard]] int watch_security(nexigen::com::Node& node, int argc, char* argv[]) {
    if (argc != 6) {
        std::fprintf(stderr, "identity-helper: bad watch-security arguments\n");
        return 2;
    }
    const auto sample_name = make_name(argv[2]);
    const auto event_name = make_name(argv[3]);
    if (!sample_name || !event_name) {
        std::fprintf(stderr, "identity-helper: invalid security service name\n");
        return kChannelFailure;
    }
    // The daemon publishes with a service maximum of 64; matching it keeps the
    // transport settings compatible.
    auto subscriber =
        nexigen::com::Subscriber<nexigen::phm::SecurityEvent>::create(node, *sample_name, 64, 64);
    auto listener = nexigen::com::EventListener::create(node, *event_name);
    if (!subscriber || !listener) {
        std::fprintf(stderr, "identity-helper: cannot open the security channel\n");
        return kChannelFailure;
    }
    const auto deadline = std::chrono::steady_clock::now() + parse_ms(argv[4]);
    while (std::chrono::steady_clock::now() < deadline) {
        auto received = subscriber->receive();
        if (!received) {
            std::fprintf(stderr, "identity-helper: security receive failed\n");
            return kChannelFailure;
        }
        if (received->has_value()) {
            std::ofstream out(argv[5], std::ios::trunc);
            out << (*received)->payload().to_string() << '\n';
            return 0;
        }
        static_cast<void>(listener->try_wait([](nexigen::com::EventId) {}));
        sleep_for(5ms);
    }
    std::fprintf(stderr, "identity-helper: no security event within the timeout\n");
    return 1;
}

} // namespace

int main(int argc, char* argv[]) {
    if (argc < 2) {
        std::fprintf(stderr,
                     "usage: %s run <checkpoint-service> <claim-sample|-> <claim-event|-> "
                     "<name> <self|foreign|none> <duration-ms> [--reset]\n"
                     "       %s watch-security <sample> <event> <timeout-ms> <outfile>\n",
                     argv[0], argv[0]);
        return 2;
    }
    auto node = nexigen::com::Node::create();
    if (!node) {
        std::fprintf(stderr, "identity-helper: node creation failed\n");
        return kChannelFailure;
    }
    const std::string_view mode = argv[1];
    if (mode == "watch-security") {
        return watch_security(*node, argc, argv);
    }
    if (mode == "run") {
        return run_scenario(*node, argc, argv);
    }
    std::fprintf(stderr, "identity-helper: unknown mode or bad arguments\n");
    return 2;
}
