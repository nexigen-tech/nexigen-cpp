// Тесты nexigen-logd (change add-log-daemon). Сборка только при NEXIGEN_ENABLE_COM=ON.
#include <atomic>
#include <chrono>
#include <cstdio>
#include <cstring>
#include <string>
#include <thread>
#include <vector>

#include <nlohmann/json.hpp>

#include <nexigen/com/event.hpp>
#include <nexigen/com/node.hpp>
#include <nexigen/com/subscriber.hpp>
#include <nexigen/com/waitset.hpp>
#include <nexigen/core/log_level.hpp>
#include <nexigen/logging/iceoryx2_logger.hpp>
#include <nexigen/logging/log_daemon_config.hpp>

#include <type_traits>
#include <nexigen/logging/log_frame.hpp>

namespace com = nexigen::com;
namespace logging = nexigen::logging;
namespace core = nexigen::core;

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
    int cb = g_checks, fb = g_failures;
    body();
    std::printf("[%-22s] %d checks, %d failed\n", name, g_checks - cb, g_failures - fb);
}
template <class L> void wait_writer(L& l) {
    for (int i = 0; i < 5000 && !l.writer_started(); ++i)
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
}

void test_config_compatibility() {
    static_assert(std::is_aggregate_v<logging::LogDaemonConfig>);
    // Legacy positional initialization (pre-main_thread member order) still
    // compiles because main_thread is appended last.
    [[maybe_unused]] logging::LogDaemonConfig positional{
        std::string{"svc"},      std::string{"/tmp"}, std::uint64_t{20}, std::uint32_t{5},
        core::LogLevel::Warning, core::CpuSet{},      std::uint32_t{500}};
}

void test_config_thread_fields() {
    logging::LogDaemonConfig config{};
    NX_CHECK(config.main_thread.set_name("logd-main").has_value());
    config.main_thread.set_cpus(nexigen::core::CpuSet::from_core_mask(1ULL));
    config.bg_cpu_affinity = nexigen::core::CpuSet::from_core_mask(0b10ULL);

    const nlohmann::json json = config;
    logging::LogDaemonConfig parsed{};
    logging::from_json(json, parsed);
    NX_CHECK(parsed.main_thread == config.main_thread);
    NX_CHECK(parsed.bg_cpu_affinity == config.bg_cpu_affinity);
    NX_CHECK(parsed.to_string().find("main_thread") != std::string::npos);

    // Effective spec: explicit main wins; empty main inherits legacy bg.
    NX_CHECK(logging::effective_main_thread_spec(config) == config.main_thread);
    logging::LogDaemonConfig legacy_affinity{};
    legacy_affinity.bg_cpu_affinity = nexigen::core::CpuSet::from_core_mask(0b10ULL);
    const nexigen::core::ThreadSpec inherited =
        logging::effective_main_thread_spec(legacy_affinity);
    NX_CHECK(inherited.cpus() == legacy_affinity.bg_cpu_affinity);
    NX_CHECK(!inherited.has_requests() == false);

    logging::LogDaemonConfig legacy{};
    logging::from_json(nlohmann::json{{"service_name", "Nexigen/Test/Legacy"}}, legacy);
    NX_CHECK(!legacy.main_thread.has_requests());
}

void test_e2e() {
    logging::Iceoryx2Logger<> logger;
    logging::Iceoryx2LoggerConfig cfg;
    cfg.service_name = "Nexigen/Test/LogdE2E";
    cfg.app_id = "E2E";
    NX_CHECK(logger.initialize(cfg).has_value());
    wait_writer(logger);

    auto node = com::Node::create();
    NX_CHECK(node.has_value());
    const auto svc = com::ServiceName::create("Nexigen/Test/LogdE2E");
    NX_CHECK(svc.has_value());
    auto sub = com::Subscriber<logging::LogFrame>::create(*node, *svc);
    NX_CHECK(sub.has_value());
    if (!sub.has_value()) return;
    const auto evn = com::ServiceName::create("Nexigen/Test/LogdE2EEvent");
    NX_CHECK(evn.has_value());
    auto listener = com::EventListener::create(*node, *evn);
    NX_CHECK(listener.has_value());

    std::atomic<bool> stop{false};
    std::vector<logging::LogFrame> frames;
    std::jthread drain([&] {
        while (!stop.load(std::memory_order_relaxed)) {
            auto got = sub->receive();
            if (got && got->has_value())
                frames.push_back((*got)->payload());
            else
                std::this_thread::sleep_for(std::chrono::microseconds(100));
        }
        for (;;) {
            auto got = sub->receive();
            if (!got || !got->has_value()) break;
            frames.push_back((*got)->payload());
        }
    });

    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    logger.info("hello daemon");
    std::this_thread::sleep_for(std::chrono::milliseconds(50));
    logger.warning("calibration lost");
    std::this_thread::sleep_for(std::chrono::milliseconds(50));
    logger.error("fatal error {}", 42);
    NX_CHECK(logger.flush().has_value());
    std::this_thread::sleep_for(std::chrono::milliseconds(200));
    stop.store(true, std::memory_order_relaxed);
    drain.join();

    NX_CHECK(frames.size() == 3);
    NX_CHECK(frames[0].payload_text() == "hello daemon");
    NX_CHECK(frames[1].payload_text() == "calibration lost");
    NX_CHECK(frames[2].payload_text() == "fatal error 42");
    NX_CHECK(frames[0].pid != 0U);
}

} // namespace

int main() {
    run_suite("config_compat", test_config_compatibility);
    run_suite("config", test_config_thread_fields);
    run_suite("e2e", test_e2e);
    std::printf("TOTAL %d checks, %d failed\n", g_checks, g_failures);
    return g_failures == 0 ? 0 : 1;
}