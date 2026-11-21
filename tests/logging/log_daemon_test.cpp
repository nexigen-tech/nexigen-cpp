// Тесты nexigen-logd (change add-log-daemon). Сборка только при NEXIGEN_ENABLE_COM=ON.
#include <atomic>
#include <chrono>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>
#include <thread>
#include <vector>

#include <csignal>
#include <cstdlib>
#include <fcntl.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>

#include <nlohmann/json.hpp>

#include <nexigen/com/event.hpp>
#include <nexigen/com/node.hpp>
#include <nexigen/com/subscriber.hpp>
#include <nexigen/com/transport_header.hpp>
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
const char* g_daemon_bin = nullptr;
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
    auto sub = com::Subscriber<logging::LogFrame, com::TransportHeader>::create(*node, *svc);
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

// Helpers shared by the real-daemon e2e tests: a per-PID unique service, the
// daemon config, a forked daemon child, a log-directory poll, and cleanup.

std::string make_unique_service(const char* prefix) {
    return std::string(prefix) + std::to_string(::getpid());
}

void write_logd_config(const std::string& cfg_path, const std::string& service,
                       const std::string& log_dir) {
    std::ofstream cfg_file(cfg_path, std::ios::out | std::ios::trunc);
    cfg_file << "{\"service_name\":\"" << service << "\",\"log_directory\":\"" << log_dir
             << "\",\"min_log_level_storage\":\"verbose\",\"batch_flush_interval_ms\":50}";
}

void prepare_root(const std::string& root, const std::string& log_dir) {
    std::error_code ec;
    std::filesystem::remove_all(root, ec);
    std::filesystem::create_directories(log_dir, ec);
}

pid_t spawn_logd(const char* bin, const std::string& cfg_path, const std::string& out_path) {
    const pid_t pid = ::fork();
    if (pid != 0) {
        return pid;
    }
    const int fd = ::open(out_path.c_str(), O_WRONLY | O_CREAT | O_TRUNC, 0644);
    if (fd >= 0) {
        ::dup2(fd, 1);
        ::dup2(fd, 2);
        ::close(fd);
    }
    ::execl(bin, bin, "--config", cfg_path.c_str(), static_cast<char*>(nullptr));
    ::_exit(127);
}

bool logdir_contains(const std::string& log_dir, std::string_view needle, int attempts = 100) {
    for (int attempt = 0; attempt < attempts; ++attempt) {
        for (const auto& entry : std::filesystem::recursive_directory_iterator(log_dir)) {
            if (!entry.is_regular_file() || entry.file_size() == 0U) {
                continue;
            }
            std::ifstream in(entry.path());
            const std::string content((std::istreambuf_iterator<char>(in)),
                                      std::istreambuf_iterator<char>());
            if (content.find(needle) != std::string::npos) {
                return true;
            }
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }
    return false;
}

void stop_logd(pid_t pid) {
    ::kill(pid, SIGTERM);
    int status = 0;
    for (int i = 0; i < 50; ++i) {
        const pid_t reaped = ::waitpid(pid, &status, WNOHANG);
        if (reaped == pid || reaped < 0) {
            break;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }
    ::kill(pid, SIGKILL);
    ::waitpid(pid, &status, 0);
}

void cleanup_root(const std::string& root) {
    std::error_code ec;
    std::filesystem::remove_all(root, ec);
}

// Publish one LogFrame from a main-thread publisher, mirroring how the metrics
// sink constructs its transport (node + publisher + notifier on the caller's
// thread), then notify the companion event service.
bool publish_main_thread(const std::string& service, std::string_view msg) {
    auto node = com::Node::create();
    if (!node) {
        return false;
    }
    const auto name = com::ServiceName::create(service.c_str());
    if (!name) {
        return false;
    }
    auto publisher = com::Publisher<logging::LogFrame, com::TransportHeader>::create(*node, *name);
    if (!publisher) {
        return false;
    }
    const auto ev_name = com::ServiceName::create((service + "Event").c_str());
    auto notifier = ev_name ? com::EventNotifier::create(*node, *ev_name)
                            : com::Result<com::EventNotifier>{std::unexpected(com::Error::Unknown)};

    logging::LogFrame frame{};
    frame.timestamp_ns = 123;
    std::memcpy(frame.app_id, "TEST", 4);
    frame.pid = static_cast<std::uint32_t>(::getpid());
    frame.log_level = core::LogLevel::Info;
    const std::size_t n = msg.size();
    std::memcpy(frame.payload, msg.data(), n);
    frame.payload_size = static_cast<std::uint16_t>(n);

    std::this_thread::sleep_for(std::chrono::milliseconds(50));
    com::TransportHeader header{};
    std::memcpy(header.app_id, "TEST", 4);
    header.pid = static_cast<std::uint32_t>(::getpid());
    header.sequence = 1U;
    const auto sent = publisher->send_copy(frame, header);
    if (sent.has_value() && notifier) {
        static_cast<void>(notifier->notify());
    }
    return sent.has_value();
}

// Real-daemon end-to-end: spawn `nexigen-logd` and assert it writes a record
// published by an Iceoryx2Logger. `g_daemon_bin` is the executable path supplied
// by CTest; when empty the suite is skipped.
void test_real_daemon_e2e() {
    if (g_daemon_bin == nullptr || g_daemon_bin[0] == '\0') {
        std::printf("  (skipped: no daemon binary path)\n");
        return;
    }
    const std::string service = make_unique_service("Nexigen/Test/LogdE2E");
    const std::string root = "/tmp/opencode/logd_e2e_" + std::to_string(::getpid());
    const std::string log_dir = root + "/logs";
    prepare_root(root, log_dir);
    write_logd_config(root + "/config.json", service, log_dir);
    const pid_t pid = spawn_logd(g_daemon_bin, root + "/config.json", root + "/out");
    NX_CHECK(pid >= 0);
    if (pid < 0) {
        return;
    }
    std::this_thread::sleep_for(std::chrono::milliseconds(500));

    bool published = false;
    bool observed = false;
    {
        logging::Iceoryx2Logger<> logger;
        logging::Iceoryx2LoggerConfig cfg;
        cfg.service_name = service;
        cfg.app_id = "TEST";
        if (logger.initialize(cfg).has_value()) {
            wait_writer(logger);
            for (int attempt = 0; attempt < 50 && !observed; ++attempt) {
                logger.info("hello e2e daemon");
                static_cast<void>(logger.flush());
                published = true;
                observed = logdir_contains(log_dir, "hello e2e daemon", 2);
            }
        }
    }

    NX_CHECK(published);
    NX_CHECK(observed);

    stop_logd(pid);
    cleanup_root(root);
}

// Main-thread publisher variant, used to bisect the worker-thread hypothesis.
void test_real_daemon_main_thread_publisher() {
    if (g_daemon_bin == nullptr || g_daemon_bin[0] == '\0') {
        std::printf("  (skipped: no daemon binary path)\n");
        return;
    }
    const std::string service = make_unique_service("Nexigen/Test/LogdMainThread");
    const std::string root = "/tmp/opencode/logd_mt_" + std::to_string(::getpid());
    const std::string log_dir = root + "/logs";
    prepare_root(root, log_dir);
    write_logd_config(root + "/config.json", service, log_dir);
    const pid_t pid = spawn_logd(g_daemon_bin, root + "/config.json", root + "/out");
    NX_CHECK(pid >= 0);
    if (pid < 0) {
        return;
    }
    std::this_thread::sleep_for(std::chrono::milliseconds(500));

    const bool published = publish_main_thread(service, "hello main thread");
    NX_CHECK(published);
    bool observed = false;
    for (int attempt = 0; attempt < 50 && !observed; ++attempt) {
        observed = logdir_contains(log_dir, "hello main thread", 2);
        if (!observed) {
            static_cast<void>(publish_main_thread(service, "hello main thread"));
        }
    }
    NX_CHECK(observed);

    stop_logd(pid);
    cleanup_root(root);
}

} // namespace

int main(int argc, char* argv[]) {
    run_suite("config_compat", test_config_compatibility);
    run_suite("config", test_config_thread_fields);
    run_suite("e2e", test_e2e);
    g_daemon_bin = argc > 1 ? argv[1] : nullptr;
    run_suite("real_daemon_e2e", test_real_daemon_e2e);
    run_suite("real_daemon_main_thread", test_real_daemon_main_thread_publisher);
    std::printf("TOTAL %d checks, %d failed\n", g_checks, g_failures);
    return g_failures == 0 ? 0 : 1;
}