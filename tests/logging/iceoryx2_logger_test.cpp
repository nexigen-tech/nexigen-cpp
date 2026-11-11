// Тесты nexigen::logging::Iceoryx2Logger (change add-iceoryx2-logger).
// Сборка только при NEXIGEN_ENABLE_COM=ON (зависимость от nexigen::com).
//
// Самодостаточный харнес (NX_CHECK + run_suite), как tests/com/com_test.cpp:
// без override operator new — iceoryx2 (Rust через FFI) аллоцирует через
// системный malloc, а не C++ operator new, так что arena-счётчик логирования
// здесь не применим.
#include <chrono>
#include <cstdio>
#include <cstring>
#include <optional>
#include <string>
#include <thread>
#include <vector>

#include <nexigen/com/node.hpp>
#include <nexigen/com/sample.hpp>
#include <nexigen/com/service_name.hpp>
#include <nexigen/com/subscriber.hpp>
#include <nexigen/core/log_level.hpp>
#include <nexigen/logging/detail/iceoryx2_sink.hpp>
#include <nexigen/logging/detail/producer.hpp>
#include <nexigen/logging/iceoryx2_logger.hpp>
#include <nexigen/logging/log_frame.hpp>

namespace logging = nexigen::logging;
namespace com = nexigen::com;
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
    const int checks_before = g_checks;
    const int failures_before = g_failures;
    body();
    std::printf("[%-22s] %d checks, %d failed\n", name, g_checks - checks_before,
                g_failures - failures_before);
}

template <class Logger> void wait_writer_started(Logger& logger) {
    for (int i = 0; i < 5000 && !logger.writer_started(); ++i) {
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
}

void fill_service_name(char (&dst)[512], std::size_t& len, std::string_view name) {
    std::memcpy(dst, name.data(), name.size());
    len = name.size();
}

// --- LogFrame layout (task 1.1/1.2, spec "Frame layout is fixed") ---
void test_log_frame() {
    static_assert(sizeof(logging::LogFrame) == 256);
    static_assert(alignof(logging::LogFrame) == 64);
    static_assert(std::is_trivially_copyable_v<logging::LogFrame>);
    NX_CHECK(true);

    logging::LogFrame frame;
    frame.timestamp_ns = 12345;
    frame.app_id[0] = 'L';
    frame.app_id[1] = 'O';
    frame.app_id[2] = 'G';
    frame.app_id[3] = 'D';
    frame.thread_id = 7;
    frame.dropped_counter = 3;
    frame.payload_size = 5;
    frame.log_level = core::LogLevel::Error;
    std::memcpy(frame.payload, "hello", 5);

    NX_CHECK(frame.app_id_text() == "LOGD");
    NX_CHECK(frame.payload_text() == "hello");

    nlohmann::json j;
    logging::to_json(j, frame);
    logging::LogFrame back;
    logging::from_json(j, back);
    NX_CHECK(back.timestamp_ns == 12345);
    NX_CHECK(back.thread_id == 7);
    NX_CHECK(back.dropped_counter == 3);
    NX_CHECK(back.log_level == core::LogLevel::Error);
    NX_CHECK(back.app_id_text() == "LOGD");
    NX_CHECK(back.payload_text() == "hello");

    // hostile JSON leaves the target unchanged
    logging::from_json(nlohmann::json::array(), back);
    NX_CHECK(back.timestamp_ns == 12345);
    NX_CHECK(back.thread_id == 7);

    const std::string text = frame.to_string();
    NX_CHECK(text.find("LogFrame") != std::string::npos);
    NX_CHECK(std::format("{}", frame) == text);
}

// --- Round-trip over iceoryx2 (task 4.1) ---
void test_roundtrip() {
    auto node = com::Node::create();
    NX_CHECK(node.has_value());
    const auto name = com::ServiceName::create("Nexigen/Test/LogRoundTrip");
    NX_CHECK(name.has_value());
    auto subscriber = com::Subscriber<logging::LogFrame>::create(*node, *name);
    NX_CHECK(subscriber.has_value());

    logging::Iceoryx2Logger<> logger;
    logging::Iceoryx2LoggerConfig cfg;
    cfg.service_name = "Nexigen/Test/LogRoundTrip";
    cfg.app_id = "TEST";
    NX_CHECK(logger.initialize(cfg).has_value());
    wait_writer_started(logger);
    NX_CHECK(logger.writer_started());
    NX_CHECK(logger.writer_start_code() == core::ErrorCode::Ok);

    logger.info("roundtrip {}", 42);
    NX_CHECK(logger.flush().has_value());

    std::optional<com::Sample<logging::LogFrame>> got;
    for (int i = 0; i < 1000 && !got.has_value(); ++i) {
        auto received = subscriber->receive();
        NX_CHECK(received.has_value());
        if (received->has_value()) {
            got = std::move(*received);
        } else {
            std::this_thread::sleep_for(std::chrono::milliseconds(5));
        }
    }
    NX_CHECK(got.has_value());
    NX_CHECK(got->payload().payload_text() == "roundtrip 42");
    NX_CHECK(got->payload().app_id_text() == "TEST");
    NX_CHECK(got->payload().log_level == core::LogLevel::Info);
}

// --- Drop semantics (task 4.2): ring overflow -> next frame carries the delta
void test_drop_semantics() {
    auto node = com::Node::create();
    NX_CHECK(node.has_value());
    const auto name = com::ServiceName::create("Nexigen/Test/DropSvc");
    NX_CHECK(name.has_value());
    using Producer = logging::detail::Producer<8, 128, 1>;
    Producer producer;
    NX_CHECK(producer.initialize().has_value());
    producer.set_level(core::LogLevel::Info);

    logging::detail::Iceoryx2Sink<Producer>::Settings settings{};
    fill_service_name(settings.service_name, settings.service_name_len, "Nexigen/Test/DropSvc");
    settings.app_id[0] = 'D';
    settings.app_id[1] = 'R';
    settings.app_id[2] = 'P';
    logging::detail::Iceoryx2Sink<Producer> sink(settings, &producer, &*node);
    NX_CHECK(sink.start().has_value());

    // Subscriber AFTER sink start so the service is already opened by the
    // publisher. Default buffer (~2): a concurrent drain thread reads frames
    // as they arrive, preventing the small buffer from overflowing.
    auto subscriber = com::Subscriber<logging::LogFrame>::create(*node, *name);
    NX_CHECK(subscriber.has_value());
    if (!subscriber.has_value()) return;

    std::vector<logging::LogFrame> frames;
    std::atomic<bool> drain_stop{false};
    std::jthread drain_thread([&] {
        while (!drain_stop.load(std::memory_order_relaxed)) {
            auto received = subscriber->receive();
            if (received && received->has_value()) {
                frames.push_back((*received)->payload());
            } else if (!received) {
                break;
            } else {
                std::this_thread::sleep_for(std::chrono::microseconds(100));
            }
        }
        // final drain
        for (;;) {
            auto received = subscriber->receive();
            if (received && received->has_value()) {
                frames.push_back((*received)->payload());
            } else {
                break;
            }
        }
    });

    // Capacity 8: 8 accepted, 2 overflow -> buffer_full == 2.
    for (int i = 0; i < 10; ++i) {
        producer.emit(core::LogLevel::Info, "drop {}", i);
    }
    NX_CHECK(producer.dropped().buffer_full == 2);

    // Drain the 8 queued records through the sink.
    Producer::record_type rec{};
    int handled = 0;
    while (producer.pop(rec)) {
        sink.handle(rec);
        ++handled;
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }
    NX_CHECK(handled == 8);

    drain_stop.store(true, std::memory_order_relaxed);
    drain_thread.join();

    NX_CHECK(frames.size() == 8);
    NX_CHECK(frames[0].dropped_counter == 2);
    NX_CHECK(frames[1].dropped_counter == 0);
}

// --- Zero-subscriber publication (task 4.3) ---
void test_zero_subscriber() {
    logging::Iceoryx2Logger<> logger;
    logging::Iceoryx2LoggerConfig cfg;
    cfg.service_name = "Nexigen/Test/LogNoSub";
    cfg.app_id = "NONE";
    NX_CHECK(logger.initialize(cfg).has_value());
    wait_writer_started(logger);
    NX_CHECK(logger.writer_started());

    for (int i = 0; i < 100; ++i) {
        logger.info("no subscriber {}", i);
    }
    NX_CHECK(logger.flush().has_value());
    const std::string text = logger.to_string();
    NX_CHECK(text.find("Iceoryx2Logger") != std::string::npos);
    NX_CHECK(std::format("{}", logger) == text);
}

} // namespace

int main() {
    run_suite("log_frame", test_log_frame);
    run_suite("roundtrip", test_roundtrip);
    run_suite("drop_semantics", test_drop_semantics);
    run_suite("zero_subscriber", test_zero_subscriber);
    std::printf("TOTAL %d checks, %d failed\n", g_checks, g_failures);
    return g_failures == 0 ? 0 : 1;
}
