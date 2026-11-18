// Generator lifecycle, publication, fault, and telemetry tests
// (change add-synchro-event-sync; spec synchro/generator; tasks 4.1-4.8).
#include "synchro_test_support.hpp"

#include <cerrno>
#include <chrono>
#include <csignal>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <string>
#include <string_view>
#include <thread>
#include <vector>

#include <dirent.h>
#include <sys/syscall.h>
#include <unistd.h>

#include <nexigen/chrono/timestamp.hpp>
#include <nexigen/com/channel_config.hpp>
#include <nexigen/com/error.hpp>
#include <nexigen/com/event_consumer_set.hpp>
#include <nexigen/com/node.hpp>
#include <nexigen/com/service_name.hpp>
#include <nexigen/metrics/concurrency_policy.hpp>
#include <nexigen/synchro/detail/sync_faults.hpp>
#include <nexigen/synchro/sync_error_kind.hpp>
#include <nexigen/synchro/sync_generator.hpp>
#include <nexigen/synchro/sync_schedule.hpp>
#include <nexigen/synchro/sync_state.hpp>

namespace {

using namespace nexigen;
using synchro::SyncGenerator;

constexpr auto kTestTimeout = std::chrono::milliseconds{2'000};

/// @brief No-op handler used to interrupt the generator's `ppoll` wait.
extern "C" void eintr_noop_handler(int) noexcept {}

/// @brief Read the kernel `comm` name of one thread.
/// @param tid Thread id.
/// @return Trimmed thread name, or an empty string when the task is gone.
std::string read_thread_name(pid_t tid) {
    const std::string path = std::format("/proc/self/task/{}/comm", tid);
    std::FILE* file = std::fopen(path.c_str(), "r");
    if (file == nullptr) {
        return {};
    }
    char buffer[32]{};
    const char* read = std::fgets(buffer, sizeof(buffer), file);
    std::fclose(file);
    if (read == nullptr) {
        return {};
    }
    std::string_view text{buffer};
    while (!text.empty() && (text.back() == '\n' || text.back() == '\0')) {
        text.remove_suffix(1);
    }
    return std::string{text};
}

/// @brief Find every live thread with the given `comm` name.
/// @param name Thread name to match.
/// @return Matching thread ids; empty when none exist.
std::vector<pid_t> find_threads_named(std::string_view name) {
    std::vector<pid_t> matches;
    DIR* tasks = ::opendir("/proc/self/task");
    if (tasks == nullptr) {
        return matches;
    }
    while (const dirent* entry = ::readdir(tasks)) {
        if (entry->d_name[0] < '0' || entry->d_name[0] > '9') {
            continue;
        }
        const auto tid = std::atoi(entry->d_name);
        if (read_thread_name(tid) == name) {
            matches.push_back(tid);
        }
    }
    ::closedir(tasks);
    return matches;
}

/// @brief Parsed overrun warnings of one run.
struct OverrunWarnings {
    std::uint64_t episodes{0}; ///< Warning records.
    std::uint64_t slots{0};    ///< Sum of the skipped-slot counts.
};

/// @brief Parse every captured "skipped N phase slots" warning.
/// @return Episode and skipped-slot totals.
OverrunWarnings parse_overrun_warnings() {
    constexpr std::string_view prefix = "sync generator overrun: skipped ";
    OverrunWarnings parsed{};
    std::lock_guard<std::mutex> guard{captured_mutex()};
    for (const CapturedRecord& record : captured_records()) {
        if (record.text.rfind(prefix, 0) != 0U) {
            continue;
        }
        const char* digits = record.text.c_str() + prefix.size();
        char* end = nullptr;
        const unsigned long long count = std::strtoull(digits, &end, 10);
        if (end == digits) {
            continue;
        }
        ++parsed.episodes;
        parsed.slots += static_cast<std::uint64_t>(count);
    }
    return parsed;
}

void test_create_rejects_invalid_config() {
    auto node = com::Node::create();
    NX_CHECK(node.has_value());
    if (!node) {
        return;
    }
    CaptureLogger logger{};
    MeterFixture meter{};
    NX_CHECK(meter.meter != nullptr);

    auto config = make_generator_config(unique_service("gen-invalid"));
    config.channel.cycle_period_ns = 0U;

    auto created = SyncGenerator<CaptureLogger>::create(*node, config, logger, *meter.meter);
    NX_CHECK(!created.has_value());
    if (!created.has_value()) {
        NX_CHECK(created.error().kind == synchro::SyncErrorKind::InvalidConfig);
        NX_CHECK(created.error().core == core::ErrorCode::InvalidArgument);
    }

    auto fast = make_generator_config(unique_service("gen-fast"));
    fast.channel.cycle_period_ns = 9'999'999U; // below the 1 ms phase floor
    fast.channel.phases_per_cycle = 10U;
    auto rejected = SyncGenerator<CaptureLogger>::create(*node, fast, logger, *meter.meter);
    NX_CHECK(!rejected.has_value());

    auto spin = make_generator_config(unique_service("gen-spin"));
    spin.spin_threshold_ns = spin.channel.phase_period_ns();
    auto spin_rejected = SyncGenerator<CaptureLogger>::create(*node, spin, logger, *meter.meter);
    NX_CHECK(!spin_rejected.has_value());
}

void test_create_rejects_duplicate_metrics() {
    auto node = com::Node::create();
    NX_CHECK(node.has_value());
    if (!node) {
        return;
    }
    CaptureLogger logger{};

    MeterFixture meter{};
    NX_CHECK(meter.meter != nullptr);
    const auto existing =
        meter.meter->create_counter<metrics::ConcurrencyPolicy::MultiThreaded>("sync_cycles_total");
    NX_CHECK(existing.has_value());

    const auto service = unique_service("gen-metrics");
    auto config = make_generator_config(service);
    auto created = SyncGenerator<CaptureLogger>::create(*node, config, logger, *meter.meter);
    NX_CHECK(!created.has_value());
    if (!created.has_value()) {
        NX_CHECK(created.error().kind == synchro::SyncErrorKind::Metrics);
        NX_CHECK(created.error().core == core::ErrorCode::InvalidArgument);
    }

    // No transport endpoint was opened by the failed attempt: the same service
    // can still be used with a fresh dedicated meter.
    MeterFixture fresh{};
    auto retried = SyncGenerator<CaptureLogger>::create(*node, config, logger, *fresh.meter);
    NX_CHECK(retried.has_value());
    if (retried.has_value()) {
        NX_CHECK((*retried)->state() == synchro::SyncState::Stopped);
    }
}

void test_create_reports_eventfd_fault() {
    auto node = com::Node::create();
    NX_CHECK(node.has_value());
    if (!node) {
        return;
    }
    CaptureLogger logger{};
    MeterFixture meter{};
    const auto config = make_generator_config(unique_service("gen-eventfd"));

    synchro::detail::SyncFaults::fail_eventfd = true;
    auto created = SyncGenerator<CaptureLogger>::create(*node, config, logger, *meter.meter);
    synchro::detail::SyncFaults::fail_eventfd = false;

    NX_CHECK(!created.has_value());
    if (!created.has_value()) {
        NX_CHECK(created.error().kind == synchro::SyncErrorKind::EventFd);
        NX_CHECK_EQ(created.error().os_errno, EMFILE);
        NX_CHECK(created.error().core == core::ErrorCode::SystemError);
    }
}

void test_start_reports_clock_fault() {
    auto node = com::Node::create();
    NX_CHECK(node.has_value());
    if (!node) {
        return;
    }
    CaptureLogger logger{};
    MeterFixture meter{};
    const auto config = make_generator_config(unique_service("gen-clock"));

    auto created = SyncGenerator<CaptureLogger>::create(*node, config, logger, *meter.meter);
    NX_CHECK(created.has_value());
    if (!created.has_value()) {
        return;
    }

    synchro::detail::SyncFaults::fail_clock = true;
    auto started = (*created)->start();
    synchro::detail::SyncFaults::fail_clock = false;

    NX_CHECK(!started.has_value());
    if (!started.has_value()) {
        NX_CHECK(started.error().kind == synchro::SyncErrorKind::Clock);
        NX_CHECK(started.error().core == core::ErrorCode::SystemError);
    }
    NX_CHECK((*created)->state() == synchro::SyncState::Stopped);
}

void test_lifecycle_and_publication() {
    auto node = com::Node::create();
    NX_CHECK(node.has_value());
    if (!node) {
        return;
    }
    CaptureLogger logger{};
    clear_captured_records();

    const auto service = unique_service("gen-run");
    auto config = make_generator_config(service, 20'000'000U, 4U);
    config.start.mode = synchro::StartMode::NextCycle;

    CollectedEvents collected{};
    auto channel = com::EventChannel<synchro::SyncPayload>::create(
        *node, *com::ServiceName::create(service.c_str()), config.channel.channel);
    NX_CHECK(channel.has_value());
    if (!channel.has_value()) {
        return;
    }
    auto endpoint =
        channel->consumer(CollectingEventCallback{&collected}, CollectingLossCallback{&collected});
    NX_CHECK(endpoint.has_value());
    if (!endpoint.has_value()) {
        return;
    }
    com::EventConsumerSet<1> set{*node};
    NX_CHECK(set.status().has_value());
    NX_CHECK(set.attach(*endpoint).has_value());

    MeterFixture meter{};
    auto created = SyncGenerator<CaptureLogger>::create(*node, config, logger, *meter.meter);
    NX_CHECK(created.has_value());
    if (!created.has_value()) {
        return;
    }
    auto& generator = **created;

    NX_CHECK(generator.state() == synchro::SyncState::Stopped);
    NX_CHECK(generator.to_string().find(service) != std::string::npos);
    NX_CHECK(std::format("{}", generator) == generator.to_string());

    NX_CHECK(generator.start().has_value());
    const auto state_after_start = generator.state();
    NX_CHECK(state_after_start == synchro::SyncState::WaitingForStart ||
             state_after_start == synchro::SyncState::Running);

    // A second start while the lifecycle is active is rejected.
    const auto second = generator.start();
    NX_CHECK(!second.has_value());
    if (!second.has_value()) {
        NX_CHECK(second.error().kind == synchro::SyncErrorKind::Lifecycle);
        NX_CHECK(second.error().core == core::ErrorCode::Conflict);
    }

    NX_CHECK(wait_for([&generator] { return generator.state() == synchro::SyncState::Running; },
                      kTestTimeout));
    NX_CHECK(pump_until(set, [&collected] { return collected.events >= 6U; }, kTestTimeout));
    NX_CHECK(collected.events >= 6U);
    NX_CHECK_EQ(collected.losses, 0U);
    NX_CHECK_EQ(collected.first_sequence, 0U);
    NX_CHECK(collected.monotonic);
    NX_CHECK_EQ(collected.last_payload.phases_total, 4U);

    const auto stop_begin = std::chrono::steady_clock::now();
    generator.stop();
    const auto stop_elapsed = std::chrono::steady_clock::now() - stop_begin;
    NX_CHECK(stop_elapsed < std::chrono::milliseconds{1'000});
    NX_CHECK(generator.state() == synchro::SyncState::Stopped);

    // Stopping an already-stopped generator is a no-op.
    generator.stop();
    NX_CHECK(generator.state() == synchro::SyncState::Stopped);

    const auto stats = generator.stats();
    NX_CHECK(stats.phases_published >= 6U);
    NX_CHECK(stats.cycles_completed >= 1U);
    NX_CHECK_EQ(stats.send_failures, 0U);
    NX_CHECK_EQ(stats.notify_failures, 0U);
    NX_CHECK(stats.start_time_ns != 0U);
    NX_CHECK(stats.last_phase_time_ns >= stats.start_time_ns);
    NX_CHECK(stats.max_jitter_ns < 20'000'000U);

    // The payload scheduled time is the absolute grid deadline, not the
    // publication instant.
    const synchro::SyncSchedule schedule{stats.start_time_ns, config.channel.cycle_period_ns,
                                         config.channel.phases_per_cycle};
    const auto& payload = collected.last_payload;
    const std::uint64_t absolute =
        payload.cycle_number * payload.phases_total + payload.phase_index;
    NX_CHECK_EQ(payload.scheduled_time_ns, schedule.deadline(absolute));
    NX_CHECK_EQ(payload.cycle_period_ns, config.channel.cycle_period_ns);
    NX_CHECK_EQ(payload.phase_index, static_cast<std::uint32_t>(absolute % 4U));

    NX_CHECK(captured_contains("sync generator starting"));
    NX_CHECK(captured_contains("sync generator running"));
    NX_CHECK(captured_contains("sync generator stopped"));
    NX_CHECK(!captured_contains("sync generator overrun"));
    NX_CHECK(!captured_contains("sync generator faulted"));
}

void test_start_at_exact_target() {
    auto node = com::Node::create();
    NX_CHECK(node.has_value());
    if (!node) {
        return;
    }
    CaptureLogger logger{};
    const auto service = unique_service("gen-target");
    auto config = make_generator_config(service, 20'000'000U, 4U);
    config.start.mode = synchro::StartMode::AtTarget;
    config.start.align_target_to_cycle = false;

    CollectedEvents collected{};
    auto channel = com::EventChannel<synchro::SyncPayload>::create(
        *node, *com::ServiceName::create(service.c_str()), config.channel.channel);
    NX_CHECK(channel.has_value());
    if (!channel.has_value()) {
        return;
    }
    auto endpoint =
        channel->consumer(CollectingEventCallback{&collected}, CollectingLossCallback{&collected});
    NX_CHECK(endpoint.has_value());
    if (!endpoint.has_value()) {
        return;
    }
    com::EventConsumerSet<1> set{*node};
    NX_CHECK(set.attach(*endpoint).has_value());

    MeterFixture meter{};
    auto created = SyncGenerator<CaptureLogger>::create(*node, config, logger, *meter.meter);
    NX_CHECK(created.has_value());
    if (!created.has_value()) {
        return;
    }
    auto& generator = **created;

    const auto now = chrono::Timestamp::now();
    NX_CHECK(now.has_value());
    if (!now.has_value()) {
        return;
    }
    const std::uint64_t target = now->nanoseconds() + 20'000'000U;
    NX_CHECK(generator.start_at(target).has_value());
    NX_CHECK(pump_until(set, [&collected] { return collected.events >= 2U; }, kTestTimeout));
    generator.stop();

    const auto stats = generator.stats();
    NX_CHECK_EQ(stats.start_time_ns, target);
    NX_CHECK(collected.events >= 2U);
    NX_CHECK(collected.last_payload.scheduled_time_ns >= target);
}

void test_start_at_past_target_realigns() {
    auto node = com::Node::create();
    NX_CHECK(node.has_value());
    if (!node) {
        return;
    }
    CaptureLogger logger{};
    clear_captured_records();
    const auto service = unique_service("gen-realign");
    auto config = make_generator_config(service, 20'000'000U, 4U);
    config.start.mode = synchro::StartMode::AtTarget;
    config.start.past_target = synchro::PastTargetPolicy::RealignToNextCycle;

    MeterFixture meter{};
    auto created = SyncGenerator<CaptureLogger>::create(*node, config, logger, *meter.meter);
    NX_CHECK(created.has_value());
    if (!created.has_value()) {
        return;
    }
    auto& generator = **created;

    auto now = chrono::Timestamp::now();
    NX_CHECK(now.has_value());
    if (!now.has_value()) {
        return;
    }
    NX_CHECK(generator.start_at(now->nanoseconds() - 5'000'000U).has_value());
    NX_CHECK(generator.stats().start_time_ns > now->nanoseconds());
    generator.stop();
    NX_CHECK(captured_contains("realigned"));

    // A rejected past target never starts the worker.
    config.start.past_target = synchro::PastTargetPolicy::Reject;
    config.channel.service_name = unique_service("gen-reject");
    MeterFixture rejecting{};
    auto second = SyncGenerator<CaptureLogger>::create(*node, config, logger, *rejecting.meter);
    NX_CHECK(second.has_value());
    if (!second.has_value()) {
        return;
    }
    auto rejected = (*second)->start_at(now->nanoseconds() - 5'000'000U);
    NX_CHECK(!rejected.has_value());
    if (!rejected.has_value()) {
        NX_CHECK(rejected.error().kind == synchro::SyncErrorKind::InvalidConfig);
    }
}

void test_restart_resets_epoch_and_sequence() {
    auto node = com::Node::create();
    NX_CHECK(node.has_value());
    if (!node) {
        return;
    }
    CaptureLogger logger{};
    const auto service = unique_service("gen-restart");
    auto config = make_generator_config(service, 20'000'000U, 4U);

    CollectedEvents collected{};
    auto channel = com::EventChannel<synchro::SyncPayload>::create(
        *node, *com::ServiceName::create(service.c_str()), config.channel.channel);
    NX_CHECK(channel.has_value());
    if (!channel.has_value()) {
        return;
    }
    auto endpoint =
        channel->consumer(CollectingEventCallback{&collected}, CollectingLossCallback{&collected});
    NX_CHECK(endpoint.has_value());
    if (!endpoint.has_value()) {
        return;
    }
    com::EventConsumerSet<1> set{*node};
    NX_CHECK(set.attach(*endpoint).has_value());

    MeterFixture meter{};
    auto created = SyncGenerator<CaptureLogger>::create(*node, config, logger, *meter.meter);
    NX_CHECK(created.has_value());
    if (!created.has_value()) {
        return;
    }
    auto& generator = **created;

    NX_CHECK(generator.start().has_value());
    NX_CHECK(pump_until(set, [&collected] { return collected.events >= 3U; }, kTestTimeout));
    const std::uint64_t first_epoch = collected.last_epoch;
    generator.stop();

    NX_CHECK(generator.start().has_value());
    NX_CHECK(pump_until(set, [&collected] { return collected.epoch_changes >= 1U; }, kTestTimeout));
    NX_CHECK(pump_until(set, [&collected] { return collected.events >= 6U; }, kTestTimeout));
    generator.stop();

    NX_CHECK_EQ(collected.epoch_changes, 1U);
    NX_CHECK_EQ(collected.first_sequence_of_new_epoch, 0U);
    NX_CHECK(collected.last_epoch != first_epoch);
    NX_CHECK(collected.monotonic);
}

void test_faulted_state_and_restart() {
    auto node = com::Node::create();
    NX_CHECK(node.has_value());
    if (!node) {
        return;
    }
    CaptureLogger logger{};
    clear_captured_records();
    synchro::detail::SyncFaults::reset();

    const auto service = unique_service("gen-faulted");
    auto config = make_generator_config(service, 20'000'000U, 4U);
    CollectedEvents collected{};
    auto channel = com::EventChannel<synchro::SyncPayload>::create(
        *node, *com::ServiceName::create(service.c_str()), config.channel.channel);
    NX_CHECK(channel.has_value());
    if (!channel.has_value()) {
        return;
    }
    auto endpoint =
        channel->consumer(CollectingEventCallback{&collected}, CollectingLossCallback{&collected});
    NX_CHECK(endpoint.has_value());
    if (!endpoint.has_value()) {
        return;
    }
    com::EventConsumerSet<1> set{*node};
    NX_CHECK(set.status().has_value());
    NX_CHECK(set.attach(*endpoint).has_value());

    MeterFixture meter{};
    auto created = SyncGenerator<CaptureLogger>::create(*node, config, logger, *meter.meter);
    NX_CHECK(created.has_value());
    if (!created.has_value()) {
        return;
    }
    auto& generator = **created;

    NX_CHECK(generator.start().has_value());
    NX_CHECK(wait_for([&generator] { return generator.state() == synchro::SyncState::Running; },
                      kTestTimeout));

    // A clock fault inside the worker wait must publish `Faulted` and keep it.
    synchro::detail::SyncFaults::fail_clock = true;
    const bool faulted = wait_for(
        [&generator] { return generator.state() == synchro::SyncState::Faulted; }, kTestTimeout);
    synchro::detail::SyncFaults::fail_clock = false;
    NX_CHECK(faulted);
    NX_CHECK(generator.state() == synchro::SyncState::Faulted);
    // `fault()` publishes the state before logging, so wait for the record to
    // prove the worker finished the transition.
    NX_CHECK(
        wait_for([&] { return captured_count("sync generator faulted") >= 1U; }, kTestTimeout));
    NX_CHECK_EQ(captured_count("sync generator faulted"), 1U);

    // Restarting a faulted generator begins a new epoch at cycle zero.
    NX_CHECK(generator.start().has_value());
    NX_CHECK(pump_until(set, [&collected] { return collected.epoch_changes >= 1U; }, kTestTimeout));
    generator.stop();
    synchro::detail::SyncFaults::reset();

    NX_CHECK_EQ(collected.epoch_changes, 1U);
    NX_CHECK_EQ(collected.first_cycle_of_new_epoch, 0U);
    NX_CHECK_EQ(collected.first_sequence_of_new_epoch, 0U);
    NX_CHECK(generator.state() == synchro::SyncState::Stopped);
}

void test_eintr_preserves_absolute_grid() {
    auto node = com::Node::create();
    NX_CHECK(node.has_value());
    if (!node) {
        return;
    }
    CaptureLogger logger{};
    clear_captured_records();
    synchro::detail::SyncFaults::reset();

    struct sigaction action{};
    action.sa_handler = &eintr_noop_handler;
    ::sigemptyset(&action.sa_mask);
    action.sa_flags = 0; // no SA_RESTART: ppoll must return EINTR
    struct sigaction previous{};
    const int installed = ::sigaction(SIGUSR1, &action, &previous);
    NX_CHECK_EQ(installed, 0);
    if (installed != 0) {
        return;
    }

    constexpr std::uint64_t cycle_period_ns = 50'000'000U;
    constexpr std::uint32_t phases_per_cycle = 5U;
    const auto service = unique_service("gen-eintr");
    auto config = make_generator_config(service, cycle_period_ns, phases_per_cycle);
    config.start.mode = synchro::StartMode::AtTarget;
    config.start.align_target_to_cycle = false;
    NX_CHECK(config.thread.set_name("sync-eintr").has_value());

    CollectedEvents collected{};
    auto channel = com::EventChannel<synchro::SyncPayload>::create(
        *node, *com::ServiceName::create(service.c_str()), config.channel.channel);
    NX_CHECK(channel.has_value());
    if (!channel.has_value()) {
        static_cast<void>(::sigaction(SIGUSR1, &previous, nullptr));
        return;
    }
    auto endpoint =
        channel->consumer(CollectingEventCallback{&collected}, CollectingLossCallback{&collected});
    NX_CHECK(endpoint.has_value());
    if (!endpoint.has_value()) {
        static_cast<void>(::sigaction(SIGUSR1, &previous, nullptr));
        return;
    }
    com::EventConsumerSet<1> set{*node};
    NX_CHECK(set.status().has_value());
    NX_CHECK(set.attach(*endpoint).has_value());

    MeterFixture meter{};
    auto created = SyncGenerator<CaptureLogger>::create(*node, config, logger, *meter.meter);
    NX_CHECK(created.has_value());
    if (!created.has_value()) {
        static_cast<void>(::sigaction(SIGUSR1, &previous, nullptr));
        return;
    }
    auto& generator = **created;

    const auto now = chrono::Timestamp::now();
    NX_CHECK(now.has_value());
    if (!now.has_value()) {
        static_cast<void>(::sigaction(SIGUSR1, &previous, nullptr));
        return;
    }
    const std::uint64_t target = now->nanoseconds() + 100'000'000U;
    NX_CHECK(generator.start_at(target).has_value());
    NX_CHECK(
        wait_for([&generator] { return generator.state() == synchro::SyncState::WaitingForStart; },
                 kTestTimeout));

    pid_t worker_tid = -1;
    const bool found = wait_for(
        [&worker_tid] {
            const auto ids = find_threads_named("sync-eintr");
            if (ids.empty()) {
                return false;
            }
            worker_tid = ids.front();
            return true;
        },
        kTestTimeout);
    NX_CHECK(found);

    for (int attempt = 0; attempt < 200 && worker_tid > 0 && synchro::detail::eintr_count() == 0U &&
                          generator.state() == synchro::SyncState::WaitingForStart;
         ++attempt) {
        static_cast<void>(::syscall(SYS_tgkill, ::getpid(), worker_tid, SIGUSR1));
        std::this_thread::sleep_for(std::chrono::milliseconds{1});
    }
    NX_CHECK(synchro::detail::eintr_count() >= 1U);

    NX_CHECK(wait_for([&generator] { return generator.state() == synchro::SyncState::Running; },
                      kTestTimeout));
    NX_CHECK(pump_until(set, [&collected] { return collected.events >= 2U; }, kTestTimeout));
    generator.stop();
    synchro::detail::SyncFaults::reset();
    static_cast<void>(::sigaction(SIGUSR1, &previous, nullptr));

    const auto stats = generator.stats();
    NX_CHECK_EQ(stats.start_time_ns, target);
    NX_CHECK(collected.events >= 2U);
    NX_CHECK_EQ(collected.epoch_changes, 0U);
    NX_CHECK_EQ(collected.losses, 0U);

    // Every publication stays on the original absolute grid; the interrupt only
    // triggers a recomputation from the same deadline.
    const synchro::SyncSchedule schedule{target, cycle_period_ns, phases_per_cycle};
    const auto& payload = collected.last_payload;
    const std::uint64_t absolute =
        payload.cycle_number * payload.phases_total + payload.phase_index;
    NX_CHECK_EQ(payload.scheduled_time_ns, schedule.deadline(absolute));
    NX_CHECK_EQ(payload.scheduled_time_ns,
                target + absolute * (cycle_period_ns / phases_per_cycle));
}

void test_hybrid_wait_sleeps_then_spins() {
    auto node = com::Node::create();
    NX_CHECK(node.has_value());
    if (!node) {
        return;
    }
    CaptureLogger logger{};
    clear_captured_records();
    synchro::detail::SyncFaults::reset();

    const auto service = unique_service("gen-hybrid");
    auto config = make_generator_config(service, 20'000'000U, 2U); // 10 ms phases
    config.spin_threshold_ns = 2'000'000U;                         // 2 ms final spin
    config.start.mode = synchro::StartMode::AtTarget;
    config.start.align_target_to_cycle = false;

    CollectedEvents collected{};
    auto channel = com::EventChannel<synchro::SyncPayload>::create(
        *node, *com::ServiceName::create(service.c_str()), config.channel.channel);
    NX_CHECK(channel.has_value());
    if (!channel.has_value()) {
        return;
    }
    auto endpoint =
        channel->consumer(CollectingEventCallback{&collected}, CollectingLossCallback{&collected});
    NX_CHECK(endpoint.has_value());
    if (!endpoint.has_value()) {
        return;
    }
    com::EventConsumerSet<1> set{*node};
    NX_CHECK(set.status().has_value());
    NX_CHECK(set.attach(*endpoint).has_value());

    MeterFixture meter{};
    auto created = SyncGenerator<CaptureLogger>::create(*node, config, logger, *meter.meter);
    NX_CHECK(created.has_value());
    if (!created.has_value()) {
        return;
    }
    auto& generator = **created;

    const auto now = chrono::Timestamp::now();
    NX_CHECK(now.has_value());
    if (!now.has_value()) {
        return;
    }
    NX_CHECK(generator.start_at(now->nanoseconds() + 100'000'000U).has_value());
    NX_CHECK(
        wait_for([&generator] { return generator.state() == synchro::SyncState::WaitingForStart; },
                 kTestTimeout));

    // Far from the deadline the worker blocks in ppoll, not in the spin loop.
    NX_CHECK(wait_for([] { return synchro::detail::coarse_wait_count() >= 1U; }, kTestTimeout));
    NX_CHECK_EQ(synchro::detail::spin_count(), 0U);

    NX_CHECK(wait_for([&generator] { return generator.state() == synchro::SyncState::Running; },
                      kTestTimeout));
    NX_CHECK(pump_until(set, [&collected] { return collected.events >= 2U; }, kTestTimeout));
    generator.stop();

    NX_CHECK(collected.events >= 2U);
    NX_CHECK(synchro::detail::coarse_wait_count() >= 1U);
    NX_CHECK(synchro::detail::spin_count() > 0U);
    NX_CHECK_EQ(collected.losses, 0U);
    synchro::detail::SyncFaults::reset();
}

void test_publication_uses_one_worker_thread() {
    auto node = com::Node::create();
    NX_CHECK(node.has_value());
    if (!node) {
        return;
    }
    CaptureLogger logger{};
    clear_captured_records();
    synchro::detail::SyncFaults::reset();

    const auto service = unique_service("gen-thread");
    auto config = make_generator_config(service, 20'000'000U, 4U);
    NX_CHECK(config.thread.set_name("sync-pub").has_value());

    CollectedEvents collected{};
    auto channel = com::EventChannel<synchro::SyncPayload>::create(
        *node, *com::ServiceName::create(service.c_str()), config.channel.channel);
    NX_CHECK(channel.has_value());
    if (!channel.has_value()) {
        return;
    }
    auto endpoint =
        channel->consumer(CollectingEventCallback{&collected}, CollectingLossCallback{&collected});
    NX_CHECK(endpoint.has_value());
    if (!endpoint.has_value()) {
        return;
    }
    com::EventConsumerSet<1> set{*node};
    NX_CHECK(set.status().has_value());
    NX_CHECK(set.attach(*endpoint).has_value());

    MeterFixture meter{};
    auto created = SyncGenerator<CaptureLogger>::create(*node, config, logger, *meter.meter);
    NX_CHECK(created.has_value());
    if (!created.has_value()) {
        return;
    }
    auto& generator = **created;

    NX_CHECK(generator.start().has_value());
    NX_CHECK(wait_for([] { return synchro::detail::published_thread_id() != 0; }, kTestTimeout));

    pid_t worker_tid = -1;
    const bool found = wait_for(
        [&worker_tid] {
            const auto ids = find_threads_named("sync-pub");
            if (ids.empty()) {
                return false;
            }
            worker_tid = ids.front();
            return true;
        },
        kTestTimeout);
    NX_CHECK(found);
    NX_CHECK(pump_until(set, [&collected] { return collected.events >= 3U; }, kTestTimeout));
    const long published_tid = synchro::detail::published_thread_id();
    generator.stop();

    NX_CHECK(collected.events >= 3U);
    NX_CHECK(published_tid != 0);
    if (found) {
        NX_CHECK_EQ(published_tid, static_cast<long>(worker_tid));
    }
    NX_CHECK(!synchro::detail::publish_thread_changed());
    synchro::detail::SyncFaults::reset();
}

void test_create_maps_service_name_failure() {
    auto node = com::Node::create();
    NX_CHECK(node.has_value());
    if (!node) {
        return;
    }
    CaptureLogger logger{};
    MeterFixture meter{};
    NX_CHECK(meter.meter != nullptr);
    if (meter.meter == nullptr) {
        return;
    }

    // Non-empty, so config validation passes, but far beyond the transport
    // service-name limit, so `ServiceName::create` rejects it.
    const std::string oversized(4'096U, 'a');
    auto config = make_generator_config(oversized);
    NX_CHECK(config.channel.validate().has_value());

    auto created = SyncGenerator<CaptureLogger>::create(*node, config, logger, *meter.meter);
    NX_CHECK(!created.has_value());
    if (!created.has_value()) {
        NX_CHECK(created.error().kind == synchro::SyncErrorKind::Transport);
        NX_CHECK(created.error().transport == com::Error::InvalidArgument);
    }
}

void test_notify_failure_is_distinguished() {
    auto node = com::Node::create();
    NX_CHECK(node.has_value());
    if (!node) {
        return;
    }
    CaptureLogger logger{};
    clear_captured_records();
    synchro::detail::SyncFaults::reset();

    const auto service = unique_service("gen-notify");
    auto config = make_generator_config(service, 20'000'000U, 4U);
    MeterFixture meter{};
    auto created = SyncGenerator<CaptureLogger>::create(*node, config, logger, *meter.meter);
    NX_CHECK(created.has_value());
    if (!created.has_value()) {
        return;
    }
    auto& generator = **created;

    synchro::detail::SyncFaults::fail_notify = true;
    NX_CHECK(generator.start().has_value());
    const bool published =
        wait_for([&generator] { return generator.stats().phases_published >= 4U; }, kTestTimeout);
    generator.stop();
    synchro::detail::SyncFaults::fail_notify = false;
    synchro::detail::SyncFaults::reset();

    NX_CHECK(published);
    const auto stats = generator.stats();
    NX_CHECK(stats.phases_published >= 4U);
    NX_CHECK_EQ(stats.notify_failures, stats.phases_published);
    NX_CHECK_EQ(stats.send_failures, 0U);
    NX_CHECK_EQ(captured_count("wake notification failed"), stats.notify_failures);
}

void test_send_failure_does_not_count_phase() {
    auto node = com::Node::create();
    NX_CHECK(node.has_value());
    if (!node) {
        return;
    }
    CaptureLogger logger{};
    clear_captured_records();
    synchro::detail::SyncFaults::reset();

    const auto service = unique_service("gen-send");
    auto config = make_generator_config(service, 20'000'000U, 4U);
    MeterFixture meter{};
    auto created = SyncGenerator<CaptureLogger>::create(*node, config, logger, *meter.meter);
    NX_CHECK(created.has_value());
    if (!created.has_value()) {
        return;
    }
    auto& generator = **created;

    synchro::detail::SyncFaults::fail_send = true;
    NX_CHECK(generator.start().has_value());
    const bool rejected =
        wait_for([&generator] { return generator.stats().send_failures >= 3U; }, kTestTimeout);
    generator.stop();
    synchro::detail::SyncFaults::fail_send = false;
    synchro::detail::SyncFaults::reset();

    NX_CHECK(rejected);
    const auto stats = generator.stats();
    NX_CHECK(stats.send_failures >= 3U);
    NX_CHECK_EQ(stats.phases_published, 0U);
    NX_CHECK_EQ(stats.cycles_completed, 0U);
    NX_CHECK_EQ(stats.notify_failures, 0U);
    NX_CHECK_EQ(captured_count("sync generator publish failed"), stats.send_failures);
}

void test_state_gauge_follows_lifecycle() {
    auto node = com::Node::create();
    NX_CHECK(node.has_value());
    if (!node) {
        return;
    }
    CaptureLogger logger{};
    clear_captured_records();
    synchro::detail::SyncFaults::reset();

    const auto service = unique_service("gen-gauge");
    auto config = make_generator_config(service, 20'000'000U, 4U);
    config.start.mode = synchro::StartMode::AtTarget;
    config.start.align_target_to_cycle = false;

    RecordingMeterFixture registry{};
    NX_CHECK(registry.meter != nullptr);
    if (registry.meter == nullptr) {
        return;
    }
    auto created = SyncGenerator<CaptureLogger>::create(*node, config, logger, *registry.meter);
    NX_CHECK(created.has_value());
    if (!created.has_value()) {
        return;
    }
    auto& generator = **created;

    std::int64_t value = -1;
    NX_CHECK(registry.gauge_value("sync_state", value));
    NX_CHECK_EQ(value, static_cast<std::int64_t>(synchro::SyncState::Stopped));

    auto now = chrono::Timestamp::now();
    NX_CHECK(now.has_value());
    if (!now.has_value()) {
        return;
    }
    NX_CHECK(generator.start_at(now->nanoseconds() + 100'000'000U).has_value());
    NX_CHECK(
        wait_for([&generator] { return generator.state() == synchro::SyncState::WaitingForStart; },
                 kTestTimeout));
    NX_CHECK(registry.gauge_value("sync_state", value));
    NX_CHECK_EQ(value, static_cast<std::int64_t>(synchro::SyncState::WaitingForStart));

    NX_CHECK(wait_for([&generator] { return generator.state() == synchro::SyncState::Running; },
                      kTestTimeout));
    NX_CHECK(registry.gauge_value("sync_state", value));
    NX_CHECK_EQ(value, static_cast<std::int64_t>(synchro::SyncState::Running));

    generator.stop();
    NX_CHECK(registry.gauge_value("sync_state", value));
    NX_CHECK_EQ(value, static_cast<std::int64_t>(synchro::SyncState::Stopped));

    now = chrono::Timestamp::now();
    NX_CHECK(now.has_value());
    if (!now.has_value()) {
        return;
    }
    NX_CHECK(generator.start_at(now->nanoseconds() + 100'000'000U).has_value());
    synchro::detail::SyncFaults::fail_clock = true;
    const bool faulted = wait_for(
        [&generator] { return generator.state() == synchro::SyncState::Faulted; }, kTestTimeout);
    synchro::detail::SyncFaults::fail_clock = false;
    synchro::detail::SyncFaults::reset();
    NX_CHECK(faulted);
    NX_CHECK(registry.gauge_value("sync_state", value));
    NX_CHECK_EQ(value, static_cast<std::int64_t>(synchro::SyncState::Faulted));

    generator.stop();
    NX_CHECK(registry.gauge_value("sync_state", value));
    NX_CHECK_EQ(value, static_cast<std::int64_t>(synchro::SyncState::Stopped));
}

void test_overrun_counter_and_single_warning() {
    auto node = com::Node::create();
    NX_CHECK(node.has_value());
    if (!node) {
        return;
    }
    CaptureLogger logger{};
    clear_captured_records();
    synchro::detail::SyncFaults::reset();

    const auto service = unique_service("gen-overrun");
    auto config = make_generator_config(service, 10'000'000U, 10U); // 1 ms phases
    RecordingMeterFixture registry{};
    NX_CHECK(registry.meter != nullptr);
    if (registry.meter == nullptr) {
        return;
    }
    auto created = SyncGenerator<CaptureLogger>::create(*node, config, logger, *registry.meter);
    NX_CHECK(created.has_value());
    if (!created.has_value()) {
        return;
    }
    auto& generator = **created;

    // Delay the first publication by 3.3 phase periods after the wake-up.
    synchro::detail::SyncFaults::stall_once_ns.store(3'300'000U, std::memory_order_relaxed);
    NX_CHECK(generator.start().has_value());
    const bool overran = wait_for(
        [&generator] {
            const auto snapshot = generator.stats();
            return snapshot.overrun_episodes >= 1U && snapshot.phases_published >= 1U;
        },
        kTestTimeout);
    generator.stop();
    synchro::detail::SyncFaults::reset();

    NX_CHECK(overran);
    const auto stats = generator.stats();
    NX_CHECK(stats.overrun_episodes >= 1U);
    NX_CHECK(stats.overrun_slots >= 3U);

    const OverrunWarnings warnings = parse_overrun_warnings();
    NX_CHECK_EQ(warnings.episodes, stats.overrun_episodes);
    NX_CHECK_EQ(warnings.slots, stats.overrun_slots);

    std::uint64_t metric_overruns = 0U;
    NX_CHECK(registry.counter_value("sync_overruns_total", metric_overruns));
    NX_CHECK_EQ(metric_overruns, stats.overrun_slots);
    NX_CHECK(stats.phases_published >= 1U);
}

void test_generator_instruments_are_registered() {
    auto node = com::Node::create();
    NX_CHECK(node.has_value());
    if (!node) {
        return;
    }
    CaptureLogger logger{};
    MeterFixture meter{};
    const auto service = unique_service("gen-metrics-all");
    auto created = SyncGenerator<CaptureLogger>::create(*node, make_generator_config(service),
                                                        logger, *meter.meter);
    NX_CHECK(created.has_value());
    if (!created.has_value()) {
        return;
    }

    using metrics::ConcurrencyPolicy;
    constexpr auto mt = ConcurrencyPolicy::MultiThreaded;
    NX_CHECK(!meter.meter->create_counter<mt>("sync_cycles_total").has_value());
    NX_CHECK(!meter.meter->create_counter<mt>("sync_phases_total").has_value());
    NX_CHECK(!meter.meter->create_counter<mt>("sync_overruns_total").has_value());
    static constexpr double bounds[] = {-1'000.0, 0.0, 1'000.0};
    NX_CHECK(!meter.meter->create_explicit_histogram<mt>("sync_jitter_ns", bounds).has_value());
    NX_CHECK((!meter.meter->create_gauge<std::int64_t, mt>("sync_state").has_value()));

    // The generator's instruments are all MultiThreaded: validate via the
    // duplicate rejection path (the provider names are the proof of identity).
    auto& generator = **created;
    NX_CHECK(generator.start().has_value());
    generator.stop();
}

} // namespace

void run_generator_suite() {
    run_suite("generator config", test_create_rejects_invalid_config);
    run_suite("generator metrics failure", test_create_rejects_duplicate_metrics);
    run_suite("generator eventfd fault", test_create_reports_eventfd_fault);
    run_suite("generator clock fault", test_start_reports_clock_fault);
    run_suite("generator lifecycle", test_lifecycle_and_publication);
    run_suite("generator exact target", test_start_at_exact_target);
    run_suite("generator realign", test_start_at_past_target_realigns);
    run_suite("generator restart", test_restart_resets_epoch_and_sequence);
    run_suite("generator faulted restart", test_faulted_state_and_restart);
    run_suite("generator EINTR", test_eintr_preserves_absolute_grid);
    run_suite("generator hybrid wait", test_hybrid_wait_sleeps_then_spins);
    run_suite("generator publish thread", test_publication_uses_one_worker_thread);
    run_suite("generator transport mapping", test_create_maps_service_name_failure);
    run_suite("generator notify failure", test_notify_failure_is_distinguished);
    run_suite("generator send failure", test_send_failure_does_not_count_phase);
    run_suite("generator state gauge", test_state_gauge_follows_lifecycle);
    run_suite("generator overrun episode", test_overrun_counter_and_single_warning);
    run_suite("generator instruments", test_generator_instruments_are_registered);
}
