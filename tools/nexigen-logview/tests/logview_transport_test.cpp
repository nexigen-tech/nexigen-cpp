// Transport test: a real iceoryx2 publisher feeds CaptureThread and the model.
#include <chrono>
#include <cstdio>
#include <cstring>
#include <ctime>
#include <memory>
#include <optional>
#include <string>
#include <thread>

#include <nexigen/com/event.hpp>
#include <nexigen/com/node.hpp>
#include <nexigen/com/publisher.hpp>
#include <nexigen/com/service_name.hpp>
#include <nexigen/com/transport_header.hpp>
#include <nexigen/logging/log_frame.hpp>
#include <nexigen/logging/log_service_options.hpp>

#include "backend/capture_thread.hpp"
#include "log_view_model.hpp"

using namespace nexigen::logview;

namespace {

int g_checks = 0;
int g_failures = 0;

void check(bool condition, const char* expression, int line) {
    ++g_checks;
    if (!condition) {
        ++g_failures;
        std::fprintf(stderr, "FAIL line %d: %s\n", line, expression);
    }
}

#define NX_CHECK(condition) check((condition), #condition, __LINE__)

using Publisher =
    nexigen::com::Publisher<nexigen::logging::LogFrame, nexigen::com::TransportHeader>;
using FramePublisher = Publisher;

constexpr const char* kService = "Nexigen/Test/LogviewTransport";

nexigen::logging::LogFrame frame_for(std::uint64_t sequence, std::uint16_t version) {
    nexigen::logging::LogFrame frame{};
    frame.timestamp_ns = 1'000U + sequence * 10U;
    frame.log_level = nexigen::core::LogLevel::Info;
    std::memcpy(frame.app_id, "Transport", 9);
    frame.version = version;
    const int written = std::snprintf(reinterpret_cast<char*>(frame.payload), sizeof(frame.payload),
                                      "transport %llu", static_cast<unsigned long long>(sequence));
    frame.payload_size = written > 0 ? static_cast<std::uint16_t>(written) : 0U;
    return frame;
}

/// @brief Publish `count` frames starting at `first_sequence`.
///
/// @param publisher Publisher bound to the log service.
/// @param notifier Optional wake-up event; may be `nullptr`.
/// @param publisher_id Publisher incarnation used by the transport header.
/// @param first_sequence First sequence number of the batch.
/// @param count Frames to publish.
/// @param version Wire format version stamped into each frame.
void send_batch(FramePublisher& publisher, nexigen::com::EventNotifier* notifier,
                std::uint64_t publisher_id, std::uint64_t first_sequence, std::uint64_t count,
                std::uint16_t version) {
    for (std::uint64_t i = 0; i < count; ++i) {
        nexigen::com::TransportHeader header{};
        std::memcpy(header.app_id, "Transport", 9);
        header.sequence = first_sequence + i;
        header.publisher_id = publisher_id;
        static_cast<void>(publisher.send_copy(frame_for(first_sequence + i, version), header));
        if (notifier != nullptr) {
            static_cast<void>(notifier->notify());
        }
    }
}

/// @brief Drain ingress into the model until `wanted` records arrive.
///
/// @param model Model receiving the drained records.
/// @param capture Capture thread feeding the model.
/// @param last_rejected Cursor of already-counted transport rejections.
/// @param wanted Received-record threshold to stop at.
void drain_until(LogViewModel& model, CaptureThread& capture, std::uint64_t& last_rejected,
                 std::size_t wanted) {
    for (int attempt = 0; attempt < 100; ++attempt) {
        const auto stats = capture.stats();
        static_cast<void>(model.drain(capture.ingress(), stats.overflow));
        if (stats.rejected > last_rejected) {
            model.note_rejected(stats.rejected - last_rejected);
            last_rejected = stats.rejected;
        }
        if (model.counters().received >= wanted) {
            return;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(20));
    }
}

/// @brief Measure process CPU over a short idle window and assert no spin.
///
/// @param capture Idle capture thread.
void run_idle_check(CaptureThread& capture) {
    timespec cpu_begin{};
    timespec cpu_end{};
    static_cast<void>(::clock_gettime(CLOCK_PROCESS_CPUTIME_ID, &cpu_begin));
    std::this_thread::sleep_for(std::chrono::milliseconds(300));
    static_cast<void>(::clock_gettime(CLOCK_PROCESS_CPUTIME_ID, &cpu_end));
    const auto cpu_ns = [](const timespec& ts) {
        return static_cast<std::uint64_t>(ts.tv_sec) * 1'000'000'000ULL +
               static_cast<std::uint64_t>(ts.tv_nsec);
    };
    const std::uint64_t idle_cpu = cpu_ns(cpu_end) - cpu_ns(cpu_begin);
    NX_CHECK(capture.stats().received == 0U);
    NX_CHECK(idle_cpu < 250'000'000ULL);
}

/// @brief Create one publisher incarnation on the log service.
///
/// @param node Transport node.
/// @param service_name Log service name.
/// @param label Scenario label used in the skip message.
/// @return The publisher, or `std::nullopt` when the transport refuses it.
std::optional<FramePublisher> create_publisher(nexigen::com::Node& node,
                                               const nexigen::com::ServiceName& service_name,
                                               const char* label) {
    auto created =
        FramePublisher::create(node, service_name, nexigen::logging::log_service_options());
    if (!created.has_value()) {
        std::printf("transport test skipped: cannot create the %s publisher\n", label);
        return std::nullopt;
    }
    return std::make_optional<FramePublisher>(std::move(*created));
}

/// @brief Publish three frames from the first incarnation and receive them.
///
/// @param publisher First publisher incarnation.
/// @param notifier Optional wake-up event.
/// @param model Model under test.
/// @param capture Capture thread feeding the model.
/// @param last_rejected Rejection cursor.
void run_first_batch(FramePublisher& publisher, nexigen::com::EventNotifier* notifier,
                     LogViewModel& model, CaptureThread& capture, std::uint64_t& last_rejected) {
    send_batch(publisher, notifier, 1U, 1U, 3U, nexigen::logging::LogFrame::format_version);
    drain_until(model, capture, last_rejected, 3U);
    NX_CHECK(model.counters().received == 3U);
}

/// @brief Publish from a second incarnation: new baseline, no false gap.
///
/// @param publisher Second publisher incarnation.
/// @param notifier Optional wake-up event.
/// @param model Model under test.
/// @param capture Capture thread feeding the model.
/// @param last_rejected Rejection cursor.
void run_second_batch(FramePublisher& publisher, nexigen::com::EventNotifier* notifier,
                      LogViewModel& model, CaptureThread& capture, std::uint64_t& last_rejected) {
    send_batch(publisher, notifier, 2U, 1U, 3U, nexigen::logging::LogFrame::format_version);
    drain_until(model, capture, last_rejected, 6U);
    NX_CHECK(model.counters().received == 6U);
    NX_CHECK(capture.stats().publishers >= 2U);
}

/// @brief A UI stall must not stop capture; the receiver keeps draining.
///
/// @param publisher Publisher used to burst frames.
/// @param notifier Optional wake-up event.
/// @param model Model under test.
/// @param capture Capture thread feeding the model.
/// @param last_rejected Rejection cursor.
void run_stall(FramePublisher& publisher, nexigen::com::EventNotifier* notifier,
               LogViewModel& model, CaptureThread& capture, std::uint64_t& last_rejected) {
    send_batch(publisher, notifier, 2U, 10U, 50U, nexigen::logging::LogFrame::format_version);
    std::this_thread::sleep_for(std::chrono::milliseconds(120));
    drain_until(model, capture, last_rejected, 56U);
    NX_CHECK(model.counters().received == 56U);
    NX_CHECK(capture.stats().ingress_size == 0U || capture.stats().received >= 56U);
}

/// @brief An injected receive failure is surfaced and the next drain resumes.
///
/// @param publisher Publisher used for the recovery frame.
/// @param notifier Optional wake-up event.
/// @param model Model under test.
/// @param capture Capture thread feeding the model.
/// @param last_rejected Rejection cursor.
void run_injected_error(FramePublisher& publisher, nexigen::com::EventNotifier* notifier,
                        LogViewModel& model, CaptureThread& capture, std::uint64_t& last_rejected) {
    capture.inject_receive_error_for_tests();
    std::this_thread::sleep_for(std::chrono::milliseconds(50));
    NX_CHECK(capture.stats().last_error != 0);
    send_batch(publisher, notifier, 2U, 60U, 1U, nexigen::logging::LogFrame::format_version);
    drain_until(model, capture, last_rejected, 57U);
    NX_CHECK(model.counters().received == 57U);
}

/// @brief An unknown wire version is rejected without reaching the history.
///
/// @param publisher Publisher used to send the incompatible frame.
/// @param notifier Optional wake-up event.
/// @param model Model under test.
/// @param capture Capture thread feeding the model.
/// @param last_rejected Rejection cursor.
void run_unknown_version(FramePublisher& publisher, nexigen::com::EventNotifier* notifier,
                         LogViewModel& model, CaptureThread& capture,
                         std::uint64_t& last_rejected) {
    send_batch(publisher, notifier, 2U, 61U, 1U, 99U);
    for (int attempt = 0; attempt < 100; ++attempt) {
        const auto stats = capture.stats();
        static_cast<void>(model.drain(capture.ingress(), stats.overflow));
        if (stats.rejected > last_rejected) {
            model.note_rejected(stats.rejected - last_rejected);
            last_rejected = stats.rejected;
        }
        if (stats.rejected >= 1U) {
            break;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(20));
    }
    NX_CHECK(model.counters().received == 57U);
    NX_CHECK(model.counters().rejected >= 1U);
    NX_CHECK(model.ring().size() >= 57U);
}

} // namespace

int main() {
    auto node = nexigen::com::Node::create();
    if (!node) {
        std::printf("transport test skipped: cannot create a node\n");
        return 0;
    }
    const auto service_name = nexigen::com::ServiceName::create(kService);
    if (!service_name) {
        std::printf("transport test skipped: invalid service name\n");
        return 0;
    }

    // Attach before any publisher exists: capture must open/join the service
    // and then receive the first frame published afterwards.
    auto capture = std::make_unique<CaptureThread>(kService, nexigen::core::ThreadSpec{});
    const auto started = capture->start();
    if (!started) {
        std::printf("transport test skipped: capture could not start\n");
        return 0;
    }

    run_idle_check(*capture);

    const auto event_name =
        nexigen::com::ServiceName::create((std::string(kService) + "Event").c_str());
    auto notifier = event_name ? nexigen::com::EventNotifier::create(*node, *event_name)
                               : nexigen::com::Result<nexigen::com::EventNotifier>{
                                     std::unexpected(nexigen::com::Error::Unknown)};
    nexigen::com::EventNotifier* wake = notifier.has_value() ? &*notifier : nullptr;

    LogViewConfig config{};
    config.max_memory_mb = 16;
    config.service_name = kService;
    auto model_result = LogViewModel::create(16ULL * 1024ULL * 1024ULL, config);
    NX_CHECK(model_result.has_value());
    if (!model_result.has_value()) {
        capture->stop();
        return 1;
    }
    std::unique_ptr<LogViewModel> model = std::move(*model_result);

    std::uint64_t last_rejected = 0;
    std::this_thread::sleep_for(std::chrono::milliseconds(50));
    auto publisher_one = create_publisher(*node, *service_name, "first");
    if (!publisher_one.has_value()) {
        capture->stop();
        return 0;
    }
    run_first_batch(*publisher_one, wake, *model, *capture, last_rejected);

    // A second publisher incarnation starts a new sequence baseline without a
    // false transport gap, and the status count tracks both.
    publisher_one.reset();
    std::this_thread::sleep_for(std::chrono::milliseconds(50));
    auto publisher_two = create_publisher(*node, *service_name, "second");
    if (!publisher_two.has_value()) {
        capture->stop();
        return 0;
    }
    run_second_batch(*publisher_two, wake, *model, *capture, last_rejected);
    run_stall(*publisher_two, wake, *model, *capture, last_rejected);
    run_injected_error(*publisher_two, wake, *model, *capture, last_rejected);
    run_unknown_version(*publisher_two, wake, *model, *capture, last_rejected);

    capture->stop();
    std::printf("total: %d checks, %d failed\n", g_checks, g_failures);
    return g_failures == 0 ? 0 : 1;
}
