// nexigen::runner logging tests — facade, settings, fallback, and affinity.
#include <chrono>
#include <array>
#include <cstdint>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>
#include <string_view>
#include <thread>

#include <sched.h>
#include <unistd.h>

#include <nlohmann/json.hpp>

#include "../support/test_temp.hpp"

#include <nexigen/core/result.hpp>
#include <nexigen/logging/logging_settings.hpp>
#include <nexigen/runner/instance_lifecycle.hpp>
#include <nexigen/runner/runner_logging.hpp>

namespace core = nexigen::core;
namespace logging = nexigen::logging;
namespace runner = nexigen::runner;

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
    const int before_checks = g_checks;
    const int before_failures = g_failures;
    body();
    std::printf("[%-24s] %d checks, %d failed\n", name, g_checks - before_checks,
                g_failures - before_failures);
}

std::string unique_dir(std::string_view tag) {
    static int counter = 0;
    ++counter;
    return "/tmp/opencode/nexigen_log_" + std::string(tag) + "_" +
           std::to_string(static_cast<long long>(::getpid())) + "_" + std::to_string(counter);
}

void remove_tree(const std::string& path) {
    std::error_code error;
    static_cast<void>(std::filesystem::remove_all(path, error));
}

std::string read_text(const std::string& path) {
    std::ifstream input(path, std::ios::binary);
    if (!input.is_open()) {
        return {};
    }
    return std::string(std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>());
}

bool contains(std::string_view text, std::string_view needle) {
    return text.find(needle) != std::string_view::npos;
}

bool cpu_is_set(int cpu, const cpu_set_t& mask) {
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wsign-conversion"
    const bool set = CPU_ISSET(cpu, &mask) != 0;
#pragma GCC diagnostic pop
    return set;
}

int first_allowed_cpu() {
    cpu_set_t mask;
    CPU_ZERO(&mask);
    if (::sched_getaffinity(0, sizeof(mask), &mask) != 0) {
        return -1;
    }
    for (int cpu = 0; cpu < CPU_SETSIZE; ++cpu) {
        if (cpu_is_set(cpu, mask)) {
            return cpu;
        }
    }
    return -1;
}

void test_init_level_and_file() {
    runner::shutdown_runner_logging();
    const std::string directory = unique_dir("basic");
    const logging::Settings settings{};
    NX_CHECK(runner::init_runner_logging(directory, settings, logging::LogLevel::Info).has_value());
    NX_CHECK(runner::runner_logging_active());
    NX_CHECK(runner::runner_logging_path() == directory + "/runner.jsonl");
    runner::log_runner(logging::LogLevel::Info, "info-event");
    runner::log_runner(logging::LogLevel::Debug, "debug-event");
    NX_CHECK(runner::flush_runner_logging().has_value());
    const std::string text = read_text(directory + "/runner.jsonl");
    NX_CHECK(contains(text, "\"app\":\"nexigen-run\""));
    NX_CHECK(contains(text, "info-event"));
    NX_CHECK(!contains(text, "debug-event"));
    runner::shutdown_runner_logging();
    NX_CHECK(!runner::runner_logging_active());
    NX_CHECK(!read_text(directory + "/runner.jsonl").empty());
    remove_tree(directory);
}

void test_reinit() {
    runner::shutdown_runner_logging();
    const std::string first = unique_dir("reinit1");
    const std::string second = unique_dir("reinit2");
    const logging::Settings settings{};
    NX_CHECK(runner::init_runner_logging(first, settings, logging::LogLevel::Info).has_value());
    runner::log_runner(logging::LogLevel::Info, "first-event");
    NX_CHECK(runner::flush_runner_logging().has_value());
    runner::shutdown_runner_logging();
    NX_CHECK(runner::init_runner_logging(second, settings, logging::LogLevel::Info).has_value());
    runner::log_runner(logging::LogLevel::Info, "second-event");
    NX_CHECK(runner::flush_runner_logging().has_value());
    NX_CHECK(contains(read_text(second + "/runner.jsonl"), "second-event"));
    runner::shutdown_runner_logging();
    remove_tree(first);
    remove_tree(second);
}

void test_global_settings_loader() {
    const std::string directory = unique_dir("settings");
    std::error_code error;
    std::filesystem::create_directories(directory, error);
    {
        std::ofstream file(directory + "/logging.json", std::ios::binary | std::ios::trunc);
        file
            << R"({"initial_log_level":"debug","flush_cpu_affinity":[0],"max_file_size_bytes":4096})";
    }
    logging::Settings settings{};
    NX_CHECK(runner::load_runner_logging_settings(directory, settings).has_value());
    NX_CHECK(settings.global_loaded);
    NX_CHECK(settings.options.initial_log_level == logging::LogLevel::Debug);
    NX_CHECK(settings.options.worker_thread.cpus().count() == 1U);
    NX_CHECK(settings.options.max_file_size_bytes == 4096ULL);

    {
        std::ofstream file(directory + "/logging.json", std::ios::binary | std::ios::trunc);
        file << "{bad json";
    }
    logging::Settings invalid{};
    NX_CHECK(!runner::load_runner_logging_settings(directory, invalid).has_value());

    logging::Settings absent{};
    NX_CHECK(runner::load_runner_logging_settings("", absent).has_value());
    NX_CHECK(!absent.global_loaded);
    remove_tree(directory);
}

void test_fallback_directory() {
    runner::shutdown_runner_logging();
    const std::string directory = unique_dir("fallback");
    std::error_code error;
    std::filesystem::create_directories(directory, error);
    char previous[4096]{};
    NX_CHECK(::getcwd(previous, sizeof(previous)) != nullptr);
    NX_CHECK(::chdir(directory.c_str()) == 0);
    const logging::Settings settings{};
    NX_CHECK(runner::init_runner_logging("/proc/nexigen_runner_log_denied", settings,
                                         logging::LogLevel::Info)
                 .has_value());
    NX_CHECK(runner::runner_logging_path() == directory + "/.nexigen-run/runner.jsonl");
    runner::log_runner(logging::LogLevel::Info, "fallback-event");
    NX_CHECK(runner::flush_runner_logging().has_value());
    NX_CHECK(contains(read_text(directory + "/.nexigen-run/runner.jsonl"), "fallback-event"));
    runner::shutdown_runner_logging();
    NX_CHECK(::chdir(previous) == 0);
    remove_tree(directory);
}

void test_affinity_and_logger_mask() {
    const int cpu = first_allowed_cpu();
    NX_CHECK(cpu >= 0);
    if (cpu < 0 || cpu >= 64) {
        return;
    }
    const std::uint32_t core = static_cast<std::uint32_t>(cpu);
    const std::array<std::uint32_t, 1> requested{core};
    NX_CHECK(runner::apply_runner_affinity(requested).has_value());
    cpu_set_t mask;
    CPU_ZERO(&mask);
    NX_CHECK(::sched_getaffinity(0, sizeof(mask), &mask) == 0);
    NX_CHECK(cpu_is_set(cpu, mask));
    NX_CHECK(CPU_COUNT(&mask) == 1);
    const std::array<std::uint32_t, 1> invalid{99999};
    NX_CHECK(!runner::apply_runner_affinity(invalid).has_value());
    NX_CHECK(runner::reset_runner_affinity().has_value());

    const std::string directory = unique_dir("mask");
    const logging::Settings settings{};
    core::ThreadSpec logger_thread{};
    logger_thread.set_cpus(core::CpuSet::from_core_mask(1ULL << core));
    NX_CHECK(
        runner::init_runner_logging(directory, settings, logging::LogLevel::Info, logger_thread)
            .has_value());
    NX_CHECK(runner::runner_logging_writer_mask() == (1ULL << core));
    runner::shutdown_runner_logging();
    remove_tree(directory);
}

void test_state_write_warning() {
    runner::shutdown_runner_logging();
    const std::string directory = unique_dir("writefail");
    std::error_code error;
    std::filesystem::create_directories(directory + "/state.json", error);
    const logging::Settings settings{};
    NX_CHECK(runner::init_runner_logging(directory, settings, logging::LogLevel::Info).has_value());
    nlohmann::json document;
    document["state"] = "running";
    const core::Status written = runner::write_json_file(directory + "/state.json", document);
    NX_CHECK(!written.has_value());
    NX_CHECK(runner::flush_runner_logging().has_value());
    const std::string text = read_text(directory + "/runner.jsonl");
    NX_CHECK(contains(text, "json_write_failed"));
    NX_CHECK(contains(text, "state.json"));
    runner::shutdown_runner_logging();
    remove_tree(directory);
}

void test_inactive_and_invalid_config() {
    runner::shutdown_runner_logging();
    NX_CHECK(runner::flush_runner_logging().has_value());
    runner::log_runner(logging::LogLevel::Info, "inactive");
    runner::warn_runner("inactive-warning");
    NX_CHECK(runner::runner_logging_path().empty());
    NX_CHECK(runner::runner_logging_writer_mask() == 0);
    NX_CHECK(!runner::resume_runner_logging().has_value());

    const std::array<std::uint32_t, 1> invalid{99999};
    NX_CHECK(!runner::apply_runner_affinity(invalid).has_value());
    const int cpu = first_allowed_cpu();
    if (cpu >= 0) {
        const std::array<std::uint32_t, 2> duplicate{static_cast<std::uint32_t>(cpu),
                                                     static_cast<std::uint32_t>(cpu)};
        NX_CHECK(!runner::apply_runner_affinity(duplicate).has_value());
    }

    const std::string directory = unique_dir("invalid_config");
    logging::Settings settings{};
    settings.global_loaded = true;
    settings.options.worker_thread = core::ThreadSpec{};
    NX_CHECK(runner::init_runner_logging(directory, settings, logging::LogLevel::Info).has_value());
    runner::shutdown_runner_logging();

    settings.options.worker_thread = core::ThreadSpec{};
    NX_CHECK(runner::init_runner_logging(directory, settings, logging::LogLevel::Info).has_value());
    runner::shutdown_runner_logging();

    if (cpu >= 0) {
        settings.options.worker_thread = core::ThreadSpec{};
        NX_CHECK(
            runner::init_runner_logging(directory, settings, logging::LogLevel::Info).has_value());
        runner::shutdown_runner_logging();
        settings.options.worker_thread.set_cpus(core::CpuSet::from_core_mask(1ULL << cpu));
        NX_CHECK(
            runner::init_runner_logging(directory, settings, logging::LogLevel::Info).has_value());
        NX_CHECK(runner::runner_logging_writer_mask() == (1ULL << cpu));
        runner::shutdown_runner_logging();
    }
    remove_tree(directory);
}

void test_thread_spec_report_diagnostics() {
    // describe_thread_apply_report renders masks to attribute labels, no
    // syscalls required: build a report from JSON and check the labels.
    {
        nlohmann::json j = nlohmann::json{
            {"requested", 7U},
            {"applied", 3U},
            {"failed", 4U},
            {"rollback_failed", 0U},
            {"first_error", "SystemError"},
        };
        core::ThreadApplyReport report{};
        core::from_json(j, report);
        const std::string text = runner::describe_thread_apply_report(report);
        NX_CHECK(contains(text, "requested=name,affinity,scheduler"));
        NX_CHECK(contains(text, "applied=name,affinity"));
        NX_CHECK(contains(text, "failed=scheduler"));
        NX_CHECK(contains(text, "first_error=SystemError"));
    }

#if defined(NEXIGEN_ENABLE_FAULT_INJECTION)
    // BestEffort: name + affinity apply, scheduler fails; the report reflects the
    // partial application and the diagnostic names the scheduler attribute.
    {
        using nexigen::core::fault_injection::FaultPoint;
        const int cpu = first_allowed_cpu();
        if (cpu >= 0 && cpu < 64) {
            core::ThreadSpec spec{};
            spec.set_cpus(core::CpuSet::from_core_mask(1ULL << cpu));
            NX_CHECK(spec.set_name("rt-diag").has_value());
            NX_CHECK(spec.set_scheduler(core::SchedulingPolicy::FIFO, 1).has_value());
            const auto hook = [](FaultPoint point) noexcept {
                return point == FaultPoint::Thread_set_scheduler;
            };
            nexigen::core::fault_injection::set_fault_hook(hook);
            core::ThreadApplyReport report{};
            core::Status status{};
            std::jthread worker(
                [&] { status = runner::apply_runner_thread_spec(spec, report, "test"); });
            worker.join();
            nexigen::core::fault_injection::set_fault_hook(nullptr);
            NX_CHECK(!status.has_value());
            NX_CHECK(report.applied() ==
                     (core::ThreadApplyReport::name_mask | core::ThreadApplyReport::affinity_mask));
            NX_CHECK(report.failed() == core::ThreadApplyReport::scheduler_mask);
            NX_CHECK(report.first_error() == core::ErrorCode::SystemError);
            const std::string text = runner::describe_thread_apply_report(report);
            NX_CHECK(contains(text, "failed=scheduler"));
            NX_CHECK(!contains(text, "failed=name,affinity"));
        }
    }
#endif
}

} // namespace

int main() {
    run_suite("init_level_file", test_init_level_and_file);
    run_suite("reinit", test_reinit);
    run_suite("global_settings", test_global_settings_loader);
    run_suite("fallback_directory", test_fallback_directory);
    run_suite("affinity_logger_mask", test_affinity_and_logger_mask);
    run_suite("state_write_warning", test_state_write_warning);
    run_suite("inactive_invalid_config", test_inactive_and_invalid_config);
    run_suite("thread_spec_report", test_thread_spec_report_diagnostics);

    std::printf("total: %d checks, %d failed\n", g_checks, g_failures);
    return g_failures == 0 ? 0 : 1;
}
