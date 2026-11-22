// Tests for app::App<Policy> (change add-app-runtime-facade; spec app/runtime).
//
// COM-gated: the runtime owns one event input and an optional event output, so
// these tests build only with NEXIGEN_ENABLE_COM=ON.
#include "app_test_support.hpp"

#include <atomic>
#include <cerrno>
#include <chrono>
#include <csignal>
#include <cstdint>
#include <filesystem>
#include <string>
#include <string_view>
#include <thread>
#include <vector>

#include <nexigen/app/app.hpp>
#include <nexigen/app/policies/console_policy.hpp>
#include <nexigen/com/event_channel.hpp>
#include <nexigen/com/node.hpp>
#include <nexigen/com/service_name.hpp>
#include <nexigen/com/subscriber.hpp>
#include <nexigen/com/transport_header.hpp>

namespace {

namespace com = nexigen::com;
namespace core = nexigen::core;

struct RuntimeSignal {
    std::uint64_t sequence{};
    double value{};
};

using TestPolicy = app::policies::ConsolePolicy;
using TestApp = app::App<TestPolicy>;

std::string unique_service(std::string_view suffix) {
    return "Nexigen/Test/AppRuntime/" + std::to_string(::getpid()) + "/" + std::string(suffix);
}

std::vector<const char*> args(std::initializer_list<const char*> values) {
    std::vector<const char*> out;
    out.reserve(values.size() + 1);
    out.push_back("app-runtime-test");
    for (const char* value : values) {
        out.push_back(value);
    }
    return out;
}

void write_settings_directory(const std::string& dir, std::string_view metrics) {
    std::error_code error;
    std::filesystem::create_directories(dir, error);
    write_text(dir + "/logging.json", R"({"initial_log_level":"debug"})");
    write_text(dir + "/metrics.json", metrics);
    write_text(dir + "/tracing.json", "{}");
    write_text(dir + "/chrono.json", "{}");
}

void test_time_base_mode_serialization() {
    NX_CHECK(app::to_string(app::TimeBaseMode::External) == "external");
    NX_CHECK(app::to_string(app::TimeBaseMode::Embedded) == "embedded");
    app::TimeBaseMode mode = app::TimeBaseMode::External;
    NX_CHECK(app::from_string("embedded", mode));
    NX_CHECK(mode == app::TimeBaseMode::Embedded);
    NX_CHECK(std::format("{}", mode) == "embedded");
    nlohmann::json json;
    app::to_json(json, app::TimeBaseMode::External);
    NX_CHECK(json == "external");
    app::from_json(nlohmann::json("embedded"), mode);
    NX_CHECK(mode == app::TimeBaseMode::Embedded);
}

void test_app_requires_identity() {
    auto argv = args({});
    auto created = TestApp::create(static_cast<int>(argv.size()), argv.data(), "runtime",
                                   app::TimeBaseMode::Embedded);
    NX_CHECK(!created.has_value());
    if (!created.has_value()) {
        NX_CHECK(created.error() == core::ErrorCode::InvalidArgument);
    }
}

void test_app_lifecycle_and_sigpipe() {
    struct sigaction before {};
    NX_CHECK(::sigaction(SIGPIPE, nullptr, &before) == 0);
    auto argv = args({"--app-id", "RUNTIME"});
    auto created = TestApp::create(static_cast<int>(argv.size()), argv.data(), "runtime",
                                   app::TimeBaseMode::Embedded);
    NX_CHECK(created.has_value());
    if (!created.has_value()) {
        return;
    }
    auto app = std::move(*created);
    NX_CHECK(app != nullptr);
    if (!app) {
        return;
    }

    struct sigaction during {};
    NX_CHECK(::sigaction(SIGPIPE, nullptr, &during) == 0);
    NX_CHECK(during.sa_handler == SIG_IGN);
    int pipe_fds[2]{-1, -1};
    NX_CHECK(::pipe(pipe_fds) == 0);
    if (pipe_fds[0] >= 0) {
        static_cast<void>(::close(pipe_fds[0]));
        const char byte = 'x';
        errno = 0;
        NX_CHECK(::write(pipe_fds[1], &byte, 1U) == -1);
        NX_CHECK(errno == EPIPE);
        static_cast<void>(::close(pipe_fds[1]));
    }
    NX_CHECK(!app->to_string().empty());
    NX_CHECK(std::format("{}", *app) == app->to_string());
    NX_CHECK(app->counter("signals_processed") != nullptr);
    NX_CHECK(app->gauge("last_value") != nullptr);
    const double bounds[] = {1.0, 2.0, 3.0};
    NX_CHECK(app->histogram("latency", std::span<const double>{bounds}) != nullptr);

    std::jthread stopper{[] {
        std::this_thread::sleep_for(std::chrono::milliseconds{100});
        static_cast<void>(::kill(::getpid(), SIGTERM));
    }};
    const auto reason = app->run();
    NX_CHECK(reason.has_value());
    if (reason.has_value()) {
        NX_CHECK(*reason == app::InfrastructureStopReason::Sigterm);
    }
    NX_CHECK(app->shutdown().has_value());
    stopper.join();

    struct sigaction after {};
    NX_CHECK(::sigaction(SIGPIPE, nullptr, &after) == 0);
    NX_CHECK(after.sa_handler == before.sa_handler);
    NX_CHECK(app->shutdown().has_value());
}

void send_runtime_input_once(const std::string& in_service) {
    std::this_thread::sleep_for(std::chrono::milliseconds{100});
    auto node = com::Node::create();
    auto name = com::ServiceName::create(in_service.c_str());
    if (!node || !name) {
        return;
    }
    auto channel = com::EventChannel<RuntimeSignal>::create(*node, *name);
    if (!channel) {
        return;
    }
    auto producer = channel->producer("TEST");
    if (!producer) {
        return;
    }
    static_cast<void>(producer->publish_now(RuntimeSignal{42U, 3.5}, 0x1001U));
    std::this_thread::sleep_for(std::chrono::milliseconds{200});
    static_cast<void>(::kill(::getpid(), SIGTERM));
}

bool observe_runtime_output(
    com::Result<com::Subscriber<RuntimeSignal, com::EventHeader>>& subscriber) {
    if (!subscriber) {
        return false;
    }
    for (int attempt = 0; attempt < 20; ++attempt) {
        auto got = subscriber->receive();
        if (got && got->has_value()) {
            NX_CHECK((**got).payload().sequence == 42U);
            NX_CHECK((**got).payload().value == 7.0);
            NX_CHECK((**got).user_header().app_id_text() == "IO_TEST");
            NX_CHECK((**got).user_header().event_code == 0x2001U);
            return true;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds{10});
    }
    return false;
}

void test_app_input_and_output() {
    auto argv = args({"--app-id", "IO_TEST"});
    auto created = TestApp::create(static_cast<int>(argv.size()), argv.data(), "runtime-io",
                                   app::TimeBaseMode::Embedded);
    NX_CHECK(created.has_value());
    if (!created.has_value()) {
        return;
    }
    auto app = std::move(*created);
    if (!app) {
        return;
    }

    const std::string in_service = unique_service("in");
    const std::string out_service = unique_service("out");
    std::atomic<std::uint64_t> received{0};
    auto* output = app->publish<RuntimeSignal>(out_service);
    NX_CHECK(output != nullptr);
    auto subscriber_node = com::Node::create();
    auto output_name = com::ServiceName::create(out_service.c_str());
    NX_CHECK(subscriber_node.has_value());
    NX_CHECK(output_name.has_value());
    com::ChannelConfig subscribe_config{};
    auto output_subscriber =
        subscriber_node && output_name
            ? com::Subscriber<RuntimeSignal, com::EventHeader>::create(
                  *subscriber_node, *output_name, subscribe_config.publish_subscribe_options())
            : com::Result<com::Subscriber<RuntimeSignal, com::EventHeader>>{
                  std::unexpected(com::Error::Unknown)};
    NX_CHECK(output_subscriber.has_value());
    if (!output_subscriber.has_value()) {
        std::fprintf(stderr, "output subscriber: %s\n",
                     std::string(com::to_string(output_subscriber.error())).c_str());
    }
    std::atomic<std::uint32_t> input_code{0};
    const bool subscribed = app->subscribe<RuntimeSignal>(
        in_service, [&](const com::EventHeader& header, const RuntimeSignal& signal) {
            received.store(signal.sequence + 1U, std::memory_order_release);
            input_code.store(header.event_code, std::memory_order_release);
            if (output != nullptr) {
                static_cast<void>(
                    output->publish_now(RuntimeSignal{signal.sequence, signal.value * 2.0}, 0x2001U));
            }
        });
    NX_CHECK(subscribed);

    std::jthread sender{[&in_service] { send_runtime_input_once(in_service); }};

    const auto reason = app->run();
    NX_CHECK(reason.has_value());
    NX_CHECK(received.load(std::memory_order_acquire) == 43U);
    NX_CHECK(input_code.load(std::memory_order_acquire) == 0x1001U);
    NX_CHECK(observe_runtime_output(output_subscriber));
    NX_CHECK(app->shutdown().has_value());
    sender.join();
}

void test_app_help_and_short_identity() {
    auto help_argv = args({"--help"});
    auto help = TestApp::create(static_cast<int>(help_argv.size()), help_argv.data(), "runtime",
                                app::TimeBaseMode::Embedded);
    NX_CHECK(help.has_value());
    if (help.has_value()) {
        NX_CHECK(*help == nullptr);
    }

    auto id_argv = args({"-aiADJACENT"});
    auto created = TestApp::create(static_cast<int>(id_argv.size()), id_argv.data(), "runtime",
                                   app::TimeBaseMode::Embedded);
    NX_CHECK(created.has_value());
    if (!created.has_value()) {
        return;
    }
    NX_CHECK(*created != nullptr);
    if (*created) {
        NX_CHECK((*created)->shutdown().has_value());
    }
}

void test_app_second_instance_rejected() {
    auto first_argv = args({"--app-id", "FIRST"});
    auto second_argv = args({"--app-id", "SECOND"});
    auto first = TestApp::create(static_cast<int>(first_argv.size()), first_argv.data(), "first",
                                 app::TimeBaseMode::Embedded);
    NX_CHECK(first.has_value());
    if (!first.has_value() || !*first) {
        return;
    }
    auto second = TestApp::create(static_cast<int>(second_argv.size()), second_argv.data(),
                                  "second", app::TimeBaseMode::Embedded);
    NX_CHECK(!second.has_value());
    if (!second.has_value()) {
        NX_CHECK(second.error() == core::ErrorCode::Conflict);
    }
    NX_CHECK((*first)->shutdown().has_value());
}

void test_app_settings_directory_path() {
    const std::string dir =
        "/tmp/opencode/app-runtime-settings-" + std::to_string(::getpid());
    write_settings_directory(dir, "{}");
    auto argv = args({"--global-settings", dir.c_str(), "--app-id", "FILED"});
    auto created = TestApp::create(static_cast<int>(argv.size()), argv.data(), "runtime-file",
                                   app::TimeBaseMode::Embedded);
    NX_CHECK(created.has_value());
    if (created.has_value() && *created) {
        NX_CHECK((*created)->shutdown().has_value());
    }
    std::error_code error;
    std::filesystem::remove_all(dir, error);
}

void test_app_counter_capacity_gate() {
    const std::string dir =
        "/tmp/opencode/app-runtime-counter-" + std::to_string(::getpid());
    write_settings_directory(dir, R"({"max_counters":1})");
    auto argv = args({"--global-settings", dir.c_str(), "--app-id", "CGATE"});
    auto created = TestApp::create(static_cast<int>(argv.size()), argv.data(), "runtime-counter",
                                   app::TimeBaseMode::Embedded);
    NX_CHECK(created.has_value());
    if (!created.has_value() || !*created) {
        std::error_code cleanup_error;
        std::filesystem::remove_all(dir, cleanup_error);
        return;
    }
    auto app = std::move(*created);
    NX_CHECK(app->counter("filler") != nullptr);
    auto* signals = app->counter("signals_processed");
    NX_CHECK(signals == nullptr);
    if (signals == nullptr) {
        const auto reason = app->run();
        NX_CHECK(!reason.has_value());
        if (!reason.has_value()) {
            NX_CHECK(reason.error() == core::ErrorCode::InvalidArgument);
        }
        bool named = false;
        for (const auto& diagnostic : app->diagnostics()) {
            named = named || diagnostic.field == "signals_processed";
        }
        NX_CHECK(named);
    }
    NX_CHECK(app->shutdown().has_value());
    std::error_code error;
    std::filesystem::remove_all(dir, error);
}

void test_app_external_base_missing() {
    const std::string dir =
        "/tmp/opencode/app-runtime-external-" + std::to_string(::getpid());
    write_settings_directory(dir, "{}");
    const std::string segment =
        "/nexigen-app-runtime-missing-" + std::to_string(static_cast<long>(::getpid()));
    write_text(dir + "/chrono.json",
               std::string(R"({"segment_name":")") + segment + std::string(R"("})"));
    auto argv = args({"--global-settings", dir.c_str(), "--app-id", "EXT"});
    auto created = TestApp::create(static_cast<int>(argv.size()), argv.data(), "runtime-external",
                                   app::TimeBaseMode::External);
    NX_CHECK(!created.has_value());
    if (!created.has_value()) {
        NX_CHECK(created.error() == core::ErrorCode::NotFound);
    }
    std::error_code error;
    std::filesystem::remove_all(dir, error);
}

void test_app_sighup() {
    auto argv = args({"--app-id", "SIGHUP"});
    auto created = TestApp::create(static_cast<int>(argv.size()), argv.data(), "runtime-hup",
                                   app::TimeBaseMode::Embedded);
    NX_CHECK(created.has_value());
    if (!created.has_value()) {
        return;
    }
    auto app = std::move(*created);
    if (!app) {
        return;
    }
    std::jthread stopper{[] {
        std::this_thread::sleep_for(std::chrono::milliseconds{100});
        static_cast<void>(::kill(::getpid(), SIGHUP));
    }};
    const auto reason = app->run();
    NX_CHECK(reason.has_value());
    if (reason.has_value()) {
        NX_CHECK(*reason == app::InfrastructureStopReason::Sighup);
        NX_CHECK(app::exit_code(*reason) == 0);
    }
    NX_CHECK(app->shutdown().has_value());
    stopper.join();
}

void test_app_second_subscription_rejected() {
    auto argv = args({"--app-id", "SECOND"});
    auto created = TestApp::create(static_cast<int>(argv.size()), argv.data(), "runtime-second",
                                   app::TimeBaseMode::Embedded);
    NX_CHECK(created.has_value());
    if (!created.has_value()) {
        return;
    }
    auto app = std::move(*created);
    if (!app) {
        return;
    }
    NX_CHECK(app->subscribe<RuntimeSignal>(unique_service("first"), [](const RuntimeSignal&) {}));
    NX_CHECK(!app->subscribe<RuntimeSignal>(unique_service("second"), [](const RuntimeSignal&) {}));
    NX_CHECK(!app->run().has_value());
    NX_CHECK(app->shutdown().has_value());
}

void test_app_diagnostic_gate() {
    auto argv = args({"--app-id", "DIAG"});
    auto created = TestApp::create(static_cast<int>(argv.size()), argv.data(), "runtime-diag",
                                   app::TimeBaseMode::Embedded);
    NX_CHECK(created.has_value());
    if (!created.has_value()) {
        return;
    }
    auto app = std::move(*created);
    if (!app) {
        return;
    }
    NX_CHECK(!app->subscribe<RuntimeSignal>("", [](const RuntimeSignal&) {}));
    const auto reason = app->run();
    NX_CHECK(!reason.has_value());
    if (!reason.has_value()) {
        NX_CHECK(reason.error() == core::ErrorCode::InvalidArgument);
    }
    NX_CHECK(!app->diagnostics().empty());
    NX_CHECK(app->shutdown().has_value());
}

} // namespace

void run_app_runtime_suites() {
    run_suite("app_runtime_time_base", test_time_base_mode_serialization);
    run_suite("app_runtime_identity", test_app_requires_identity);
    run_suite("app_runtime_lifecycle", test_app_lifecycle_and_sigpipe);
    run_suite("app_runtime_help", test_app_help_and_short_identity);
    run_suite("app_runtime_sighup", test_app_sighup);
    run_suite("app_runtime_files", test_app_settings_directory_path);
    run_suite("app_runtime_counter_gate", test_app_counter_capacity_gate);
    run_suite("app_runtime_external", test_app_external_base_missing);
    run_suite("app_runtime_single", test_app_second_instance_rejected);
    run_suite("app_runtime_ports", test_app_input_and_output);
    run_suite("app_runtime_second", test_app_second_subscription_rejected);
    run_suite("app_runtime_diagnostics", test_app_diagnostic_gate);
}

int main() {
    run_app_runtime_suites();
    std::printf("app runtime: %d checks, %d failures\n", g_checks, g_failures);
    return g_failures == 0 ? 0 : 1;
}
