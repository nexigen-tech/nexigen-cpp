// Тесты nexigen::logging::Iceoryx2Logger (change add-iceoryx2-logger).
// Сборка только при NEXIGEN_ENABLE_COM=ON (зависимость от nexigen::com).
//
// Самодостаточный харнес (NX_CHECK + run_suite), как tests/com/com_test.cpp:
// без override operator new — iceoryx2 (Rust через FFI) аллоцирует через
// системный malloc, а не C++ operator new, так что arena-счётчик логирования
// здесь не применим.
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <optional>
#include <string>
#include <thread>
#include <vector>

#include <unistd.h>

#include <nexigen/com/node.hpp>
#include <nexigen/com/publisher.hpp>
#include <nexigen/com/sample.hpp>
#include <nexigen/com/service_name.hpp>
#include <nexigen/com/subscriber.hpp>
#include <nexigen/com/transport_header.hpp>
#include <nexigen/core/log_level.hpp>
#include <nexigen/logging/concepts/quiescable_logger_concept.hpp>
#include <nexigen/logging/detail/iceoryx2_sink.hpp>
#include <nexigen/logging/detail/producer.hpp>
#include <nexigen/logging/iceoryx2_logger.hpp>
#include <nexigen/logging/log_frame.hpp>
#include <nexigen/logging/log_service_options.hpp>

#include "log_shape_test_support.hpp"

namespace logging = nexigen::logging;
namespace com = nexigen::com;
namespace core = nexigen::core;

namespace {

static_assert(logging::concepts::QuiescableLogger<logging::Iceoryx2Logger<>>,
              "Iceoryx2Logger must satisfy the quiescence contract");

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

std::string unique_service(const char* prefix) {
    return std::string(prefix) + std::to_string(static_cast<long long>(::getpid()));
}

bool init_logger(logging::Iceoryx2Logger<>& logger, const std::string& service, const char* app) {
    logging::Iceoryx2LoggerConfig cfg;
    cfg.service_name = service;
    auto id = nexigen::core::AppId::create(app);
    if (!id.has_value()) {
        return false;
    }
    cfg.app_id = *id;
    if (!logger.initialize(cfg).has_value()) {
        return false;
    }
    wait_writer_started(logger);
    return logger.writer_started();
}

logging::LogFrame make_frame(const char* app, std::uint64_t stamp, std::string_view message) {
    logging::LogFrame frame{};
    frame.timestamp_ns = stamp;
    const std::size_t app_len =
        std::strlen(app) < sizeof(frame.app_id) ? std::strlen(app) : sizeof(frame.app_id);
    std::memcpy(frame.app_id, app, app_len);
    frame.log_level = core::LogLevel::Info;
    const std::size_t msg_len =
        message.size() < sizeof(frame.payload) ? message.size() : sizeof(frame.payload);
    std::memcpy(frame.payload, message.data(), msg_len);
    frame.payload_size = static_cast<std::uint16_t>(msg_len);
    frame.pid = static_cast<std::uint32_t>(::getpid());
    frame.version = logging::LogFrame::format_version;
    return frame;
}

bool send_frame(com::Publisher<logging::LogFrame, com::TransportHeader>& publisher,
                const logging::LogFrame& frame, std::uint64_t sequence) {
    com::TransportHeader header{};
    std::memcpy(header.app_id, frame.app_id, sizeof(header.app_id));
    header.pid = frame.pid;
    header.publisher_id = 1U;
    header.sequence = sequence;
    return publisher.send_copy(frame, header).has_value();
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
    NX_CHECK(frame.version == logging::LogFrame::format_version);
    static_assert(sizeof(frame.payload) == 211U);

    logging::LogFrame full{};
    std::memcpy(full.app_id, "ABCDEFGHIJKLMNOP", 16U);
    NX_CHECK(full.app_id_text() == "ABCDEFGHIJKLMNOP");
    nlohmann::json full_json;
    logging::to_json(full_json, full);
    logging::LogFrame full_back;
    logging::from_json(full_json, full_back);
    NX_CHECK(full_back.app_id_text() == "ABCDEFGHIJKLMNOP");

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

// --- Shared log-service options (change fix-log-subscriber-capacity) ---
void test_log_service_options() {
    static_assert(logging::log_service_options().max_subscribers == 4U);
    static_assert(logging::log_service_options().max_publishers == 64U);
    static_assert(logging::log_service_options().max_nodes == 64U);
    static_assert(logging::log_service_options().subscriber_max_buffer_size == 16384U);
    static_assert(logging::log_service_options().history_size == 4U);
    static_assert(logging::log_service_options().enable_safe_overflow);

    const com::PublishSubscribeOptions options = logging::log_service_options();
    NX_CHECK(options.max_subscribers == logging::kLogServiceMaxSubscribers);
    NX_CHECK(options.max_publishers == logging::kLogServiceMaxPublishers);
    NX_CHECK(options.max_nodes == logging::kLogServiceMaxNodes);
    NX_CHECK(options.subscriber_max_buffer_size == logging::kLogServiceBufferDepth);
    NX_CHECK(options.history_size == logging::kLogServiceHistoryDepth);
    NX_CHECK(options.enable_safe_overflow == logging::kLogServiceSafeOverflow);
    NX_CHECK(options.subscriber_buffer_size == 0U);
    NX_CHECK(options.subscriber_history_request == 0U);
}

// --- The created service carries the shared capacity shape ---
void test_service_shape() {
    const std::string service = unique_service("Nexigen/Test/LogShape");
    logging::Iceoryx2Logger<> logger;
    logging::Iceoryx2LoggerConfig cfg;
    cfg.service_name = service;
    cfg.app_id = nexigen::core::AppId::from_literal("SHP");
    NX_CHECK(logger.initialize(cfg).has_value());
    wait_writer_started(logger);
    NX_CHECK(logger.writer_started());

    auto node = com::Node::create();
    NX_CHECK(node.has_value());
    if (!node.has_value()) {
        return;
    }
    std::optional<logging::test_support::ServiceShape> shape;
    for (int i = 0; i < 100 && !shape.has_value(); ++i) {
        shape = logging::test_support::query_service_shape(service, *node);
        if (!shape.has_value()) {
            std::this_thread::sleep_for(std::chrono::milliseconds(5));
        }
    }
    NX_CHECK(shape.has_value());
    if (shape.has_value()) {
        NX_CHECK(shape->max_subscribers == logging::kLogServiceMaxSubscribers);
        NX_CHECK(shape->max_publishers == logging::kLogServiceMaxPublishers);
        NX_CHECK(shape->max_nodes == logging::kLogServiceMaxNodes);
        NX_CHECK(shape->buffer_depth == logging::kLogServiceBufferDepth);
        NX_CHECK(shape->history == logging::kLogServiceHistoryDepth);
        NX_CHECK(shape->safe_overflow == logging::kLogServiceSafeOverflow);
    }
    NX_CHECK(logger.quiesce().has_value());
}

// --- Config capacity is pinned to the shared contract ---
void test_config_capacity() {
    logging::Iceoryx2LoggerConfig cfg;
    NX_CHECK(cfg.max_subscribers == 4U);
    NX_CHECK(cfg.history_size == 4U);

    const nlohmann::json json = cfg;
    logging::Iceoryx2LoggerConfig back{};
    logging::from_json(json, back);
    NX_CHECK(back.max_subscribers == 4U);
    NX_CHECK(back.history_size == 4U);
    NX_CHECK(cfg.to_string().find("subscribers=4") != std::string::npos);
    NX_CHECK(std::format("{}", cfg) == cfg.to_string());

    logging::Iceoryx2Logger<> logger;
    cfg.service_name = unique_service("Nexigen/Test/LogCfg");
    cfg.max_subscribers = 16U;
    const core::Status status = logger.initialize(cfg);
    NX_CHECK(!status.has_value());
    if (!status.has_value()) {
        NX_CHECK(status.error() == core::ErrorCode::InvalidArgument);
    }
    NX_CHECK(!logger.writer_started());

    logging::Iceoryx2Logger<> history_logger;
    logging::Iceoryx2LoggerConfig history_cfg;
    history_cfg.service_name = unique_service("Nexigen/Test/LogHist");
    history_cfg.history_size = 8U;
    const core::Status history_status = history_logger.initialize(history_cfg);
    NX_CHECK(!history_status.has_value());
    if (!history_status.has_value()) {
        NX_CHECK(history_status.error() == core::ErrorCode::InvalidArgument);
    }
    NX_CHECK(!history_logger.writer_started());

    // The public ThreadSpec setters reject an incoherent policy/priority pair,
    // so the empty and oversized service-name cases cover the reachable
    // precondition violations; worker_spec.validate() stays as defence in depth.
    logging::Iceoryx2Logger<> empty_logger;
    logging::Iceoryx2LoggerConfig empty_cfg;
    empty_cfg.service_name.clear();
    const core::Status empty_status = empty_logger.initialize(empty_cfg);
    NX_CHECK(!empty_status.has_value());
    if (!empty_status.has_value()) {
        NX_CHECK(empty_status.error() == core::ErrorCode::InvalidArgument);
    }
    NX_CHECK(!empty_logger.writer_started());

    logging::Iceoryx2Logger<> oversized_logger;
    logging::Iceoryx2LoggerConfig oversized_cfg;
    oversized_cfg.service_name = std::string(512U, 'x');
    const core::Status oversized_status = oversized_logger.initialize(oversized_cfg);
    NX_CHECK(!oversized_status.has_value());
    if (!oversized_status.has_value()) {
        NX_CHECK(oversized_status.error() == core::ErrorCode::InvalidArgument);
    }
    NX_CHECK(!oversized_logger.writer_started());
}

// --- An existing service with an old shape is rejected with both shapes named ---
void test_incompatible_service() {
    const std::string service = unique_service("Nexigen/Test/LogShapeOld");
    auto node = com::Node::create();
    NX_CHECK(node.has_value());
    if (!node.has_value()) {
        return;
    }
    const auto name = com::ServiceName::create(service.c_str());
    NX_CHECK(name.has_value());
    if (!name.has_value()) {
        return;
    }

    com::PublishSubscribeOptions old{};
    old.max_subscribers = 8U;
    old.max_publishers = 2U;
    old.max_nodes = 20U;
    old.subscriber_max_buffer_size = 2U;
    old.history_size = 0U;
    old.enable_safe_overflow = false;
    auto publisher =
        com::Publisher<logging::LogFrame, com::TransportHeader>::create(*node, *name, old);
    NX_CHECK(publisher.has_value());
    if (!publisher.has_value()) {
        return;
    }

    logging::Iceoryx2Logger<> logger;
    logging::Iceoryx2LoggerConfig cfg;
    cfg.service_name = service;
    cfg.app_id = nexigen::core::AppId::from_literal("OLD");
    const core::Status status = logger.initialize(cfg);
    NX_CHECK(!status.has_value());
    if (!status.has_value()) {
        NX_CHECK(status.error() == core::ErrorCode::SystemError);
    }
    NX_CHECK(!logger.writer_started());
    NX_CHECK(logger.startup_diagnostic().find("buffer_depth=2") != std::string_view::npos);
    NX_CHECK(logger.startup_diagnostic().find("buffer_depth=16384") != std::string_view::npos);
    NX_CHECK(logger.startup_diagnostic().find("max_publishers=2") != std::string_view::npos);
    NX_CHECK(logger.to_string().find("startup=log service") != std::string::npos);
}

/// Drain three frames from one subscriber and check that all three
/// applications are represented.
bool receives_three_logs(com::Subscriber<logging::LogFrame, com::TransportHeader>& sub) {
    bool saw_one = false;
    bool saw_two = false;
    bool saw_three = false;
    int received = 0;
    for (int attempt = 0; attempt < 2000 && received < 3; ++attempt) {
        auto got = sub.receive();
        if (!got.has_value()) {
            return false;
        }
        if (!got->has_value()) {
            std::this_thread::sleep_for(std::chrono::milliseconds(2));
            continue;
        }
        const std::string_view app = (*got)->payload().app_id_text();
        saw_one = saw_one || app == "A1";
        saw_two = saw_two || app == "A2";
        saw_three = saw_three || app == "A3";
        ++received;
    }
    return received == 3 && saw_one && saw_two && saw_three;
}

// --- Three logging applications and three subscribers share one service ---
void test_three_publishers() {
    const std::string service = unique_service("Nexigen/Test/LogThree");
    auto node = com::Node::create();
    NX_CHECK(node.has_value());
    if (!node.has_value()) {
        return;
    }
    const auto name = com::ServiceName::create(service.c_str());
    NX_CHECK(name.has_value());
    if (!name.has_value()) {
        return;
    }
    auto sub1 = com::Subscriber<logging::LogFrame, com::TransportHeader>::create(
        *node, *name, logging::log_service_options());
    NX_CHECK(sub1.has_value());
    if (!sub1.has_value()) {
        return;
    }
    auto sub2 = com::Subscriber<logging::LogFrame, com::TransportHeader>::create(
        *node, *name, logging::log_service_options());
    NX_CHECK(sub2.has_value());
    if (!sub2.has_value()) {
        return;
    }
    auto sub3 = com::Subscriber<logging::LogFrame, com::TransportHeader>::create(
        *node, *name, logging::log_service_options());
    NX_CHECK(sub3.has_value());
    if (!sub3.has_value()) {
        return;
    }

    logging::Iceoryx2Logger<> first;
    logging::Iceoryx2Logger<> second;
    logging::Iceoryx2Logger<> third;
    NX_CHECK(init_logger(first, service, "A1"));
    NX_CHECK(init_logger(second, service, "A2"));
    NX_CHECK(init_logger(third, service, "A3"));

    first.info("from one");
    second.info("from two");
    third.info("from three");
    NX_CHECK(first.flush().has_value());
    NX_CHECK(second.flush().has_value());
    NX_CHECK(third.flush().has_value());

    NX_CHECK(receives_three_logs(*sub1));
    NX_CHECK(receives_three_logs(*sub2));
    NX_CHECK(receives_three_logs(*sub3));
}

// --- A full 16 384-frame burst fits the shared subscriber depth ---
void test_burst_no_loss() {
    constexpr std::uint64_t kDepth = logging::kLogServiceBufferDepth;
    const std::string service = unique_service("Nexigen/Test/LogBurst");
    auto node = com::Node::create();
    NX_CHECK(node.has_value());
    if (!node.has_value()) {
        return;
    }
    const auto name = com::ServiceName::create(service.c_str());
    NX_CHECK(name.has_value());
    if (!name.has_value()) {
        return;
    }
    auto publisher = com::Publisher<logging::LogFrame, com::TransportHeader>::create(
        *node, *name, logging::log_service_options());
    NX_CHECK(publisher.has_value());
    if (!publisher.has_value()) {
        return;
    }
    auto sub = com::Subscriber<logging::LogFrame, com::TransportHeader>::create(
        *node, *name, logging::log_service_options());
    NX_CHECK(sub.has_value());
    if (!sub.has_value()) {
        return;
    }

    bool all_sent = true;
    for (std::uint64_t sequence = 1; sequence <= kDepth; ++sequence) {
        if (!send_frame(*publisher, make_frame("BRST", sequence, "burst"), sequence)) {
            all_sent = false;
        }
    }
    NX_CHECK(all_sent);

    std::uint64_t received = 0;
    bool no_gap = true;
    for (int attempt = 0; attempt < 100000 && received < kDepth; ++attempt) {
        auto got = sub->receive();
        if (!got.has_value()) {
            NX_CHECK(false);
            break;
        }
        if (!got->has_value()) {
            break;
        }
        no_gap = no_gap && ((*got)->lost_before() == 0U);
        ++received;
    }
    NX_CHECK(received == kDepth);
    NX_CHECK(no_gap);
}

// --- Overflow evicts the oldest frames and reports the sequence gap ---
void test_overflow_gap() {
    constexpr std::uint64_t kDepth = logging::kLogServiceBufferDepth;
    constexpr std::uint64_t kAfterBaseline = 20000U;
    const std::string service = unique_service("Nexigen/Test/LogOverflow");
    auto node = com::Node::create();
    NX_CHECK(node.has_value());
    if (!node.has_value()) {
        return;
    }
    const auto name = com::ServiceName::create(service.c_str());
    NX_CHECK(name.has_value());
    if (!name.has_value()) {
        return;
    }
    auto publisher = com::Publisher<logging::LogFrame, com::TransportHeader>::create(
        *node, *name, logging::log_service_options());
    NX_CHECK(publisher.has_value());
    if (!publisher.has_value()) {
        return;
    }
    auto sub = com::Subscriber<logging::LogFrame, com::TransportHeader>::create(
        *node, *name, logging::log_service_options());
    NX_CHECK(sub.has_value());
    if (!sub.has_value()) {
        return;
    }

    // Establish the per-publisher sequence baseline. The sample is released
    // before the burst: the subscriber may hold at most two samples at once.
    {
        NX_CHECK(send_frame(*publisher, make_frame("OVFL", 0U, "baseline"), 1U));
        auto baseline = sub->receive();
        NX_CHECK(baseline.has_value());
        NX_CHECK(baseline.has_value() && baseline->has_value());
    }

    bool all_sent = true;
    for (std::uint64_t i = 0; i < kAfterBaseline; ++i) {
        if (!send_frame(*publisher, make_frame("OVFL", i + 2U, "overflow"), i + 2U)) {
            all_sent = false;
        }
    }
    NX_CHECK(all_sent);

    // The first retained sample reports the evicted prefix as a sequence gap.
    {
        auto got = sub->receive();
        NX_CHECK(got.has_value());
        NX_CHECK(got.has_value() && got->has_value());
        if (got.has_value() && got->has_value()) {
            NX_CHECK((*got)->lost_before() == kAfterBaseline - kDepth);
        }
    }

    // The retained tail is exactly the shared depth.
    std::uint64_t retained = 1U;
    for (;;) {
        auto next = sub->receive();
        if (!next.has_value() || !next->has_value()) {
            break;
        }
        ++retained;
    }
    NX_CHECK(retained == kDepth);
}

// --- Round-trip over iceoryx2 (task 4.1) ---
void test_roundtrip() {
    auto node = com::Node::create();
    NX_CHECK(node.has_value());
    const auto name = com::ServiceName::create("Nexigen/Test/LogRoundTrip");
    NX_CHECK(name.has_value());
    auto subscriber = com::Subscriber<logging::LogFrame, com::TransportHeader>::create(
        *node, *name, logging::log_service_options());
    NX_CHECK(subscriber.has_value());

    logging::Iceoryx2Logger<> logger;
    logging::Iceoryx2LoggerConfig cfg;
    cfg.service_name = "Nexigen/Test/LogRoundTrip";
    cfg.app_id = nexigen::core::AppId::from_literal("TEST");
    NX_CHECK(logger.initialize(cfg).has_value());
    wait_writer_started(logger);
    NX_CHECK(logger.writer_started());
    NX_CHECK(logger.writer_start_code() == core::ErrorCode::Ok);

    logger.info("roundtrip {}", 42);
    NX_CHECK(logger.flush().has_value());

    std::optional<com::Sample<logging::LogFrame, com::TransportHeader>> got;
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
    auto subscriber = com::Subscriber<logging::LogFrame, com::TransportHeader>::create(
        *node, *name, logging::log_service_options());
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
        sink.on_event(rec);
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
    cfg.app_id = nexigen::core::AppId::from_literal("NONE");
    NX_CHECK(logger.initialize(cfg).has_value());
    wait_writer_started(logger);
    NX_CHECK(logger.writer_started());

    for (int i = 0; i < 100; ++i) {
        logger.info("no subscriber {}", i);
    }
    NX_CHECK(logger.flush().has_value());
    NX_CHECK(logger.quiesce().has_value());
    NX_CHECK(logger.quiesce().has_value()); // idempotent
    NX_CHECK(!logger.flush().has_value());  // barrier gone after quiescence
    const std::string text = logger.to_string();
    NX_CHECK(text.find("Iceoryx2Logger") != std::string::npos);
    NX_CHECK(std::format("{}", logger) == text);
}

} // namespace

int main() {
    run_suite("log_frame", test_log_frame);
    run_suite("log_service_opts", test_log_service_options);
    run_suite("service_shape", test_service_shape);
    run_suite("config_capacity", test_config_capacity);
    run_suite("incompatible_svc", test_incompatible_service);
    run_suite("three_publishers", test_three_publishers);
    run_suite("burst_no_loss", test_burst_no_loss);
    run_suite("overflow_gap", test_overflow_gap);
    run_suite("roundtrip", test_roundtrip);
    run_suite("drop_semantics", test_drop_semantics);
    run_suite("zero_subscriber", test_zero_subscriber);
    std::printf("TOTAL %d checks, %d failed\n", g_checks, g_failures);
    return g_failures == 0 ? 0 : 1;
}
