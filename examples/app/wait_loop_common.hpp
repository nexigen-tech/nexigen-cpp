#pragma once

// Shared body of the Infrastructure wait-loop examples. The policy chooses
// where telemetry goes; the wait loop stays the same.

#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <cstdio>
#include <deque>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <string_view>
#include <thread>
#include <utility>

#include <nexigen/app/infrastructure.hpp>
#include <nexigen/chrono/timestamp.hpp>
#include <nexigen/core/result.hpp>
#include <nexigen/metrics/policy_enums.hpp>
#include <nexigen/tracing/span_kind.hpp>

#include "time_base_fixture.hpp"

namespace nexigen::examples {

namespace app = nexigen::app;
namespace core = nexigen::core;
namespace chrono = nexigen::chrono;
namespace metrics = nexigen::metrics;
namespace tracing = nexigen::tracing;

namespace wait_loop_detail {

constexpr std::uint64_t kEventCount = 5;
constexpr auto kWaitTimeout = std::chrono::milliseconds(100);

struct Event {
    std::uint64_t sequence{0};
    chrono::Timestamp stamp{};
    double value{0.0};
};

inline int fail(std::string_view app_name, std::string_view message) {
    std::fprintf(stderr, "%.*s: %.*s\n", static_cast<int>(app_name.size()), app_name.data(),
                 static_cast<int>(message.size()), message.data());
    return 1;
}

} // namespace wait_loop_detail

/// @brief Run the wait-loop workload with a chosen Infrastructure policy.
///
/// @tparam Policy Stateless Infrastructure policy (console/file/iceoryx2/null).
/// @param app_name Application name used for settings and log prefix.
/// @return Process exit code.
template <class Policy> int run_wait_loop(std::string_view app_name) {
    using wait_loop_detail::Event;
    using wait_loop_detail::fail;
    using wait_loop_detail::kEventCount;
    using wait_loop_detail::kWaitTimeout;

    LocalTimeBase time_base{app_name};
    if (!time_base.is_open()) {
        return fail(app_name, "local time base could not be created");
    }

    auto created = app::make_infrastructure<Policy, 256, 8>();
    if (!created.has_value()) {
        return fail(app_name, "make_infrastructure failed");
    }
    auto infra = std::move(*created);

    app::Settings settings = app::Settings::create(std::string(app_name));
    settings.chrono.segment_name = time_base.name();
    if (!infra->initialize(settings).has_value()) {
        return fail(app_name, "Infrastructure::initialize failed");
    }

    const auto logger = infra->logger();
    if (!logger.has_value()) {
        return fail(app_name, "Infrastructure::logger failed");
    }

    const auto meter = infra->meter(app_name, "1.0");
    if (!meter.has_value()) {
        return fail(app_name, "Infrastructure::meter failed");
    }
    const auto events_counter =
        (*meter)
            ->template create_counter<metrics::ConcurrencyPolicy::SingleThreaded,
                                      metrics::AccessPolicy::Readable>("events");
    if (!events_counter.has_value()) {
        return fail(app_name, "create_counter failed");
    }

    const std::string tracer_scope = std::string(app_name) + ".events";
    const auto tracer = infra->tracer_for(tracer_scope);
    if (!tracer.has_value()) {
        return fail(app_name, "Infrastructure::tracer_for failed");
    }

    std::mutex mutex;
    std::condition_variable cv;
    std::deque<Event> queue;
    bool producer_done = false;

    std::jthread producer([&](std::stop_token stop) {
        for (std::uint64_t i = 0; i < kEventCount && !stop.stop_requested(); ++i) {
            // Sleep in small slices so shutdown stays responsive.
            for (int slice = 0; slice < 20 && !stop.stop_requested(); ++slice) {
                std::this_thread::sleep_for(std::chrono::milliseconds(10));
            }
            if (stop.stop_requested()) {
                break;
            }
            const auto now = chrono::Timestamp::now();
            if (!now.has_value()) {
                break;
            }
            {
                std::lock_guard lock(mutex);
                queue.push_back(Event{i, *now, 0.5 * static_cast<double>(i + 1U)});
            }
            cv.notify_one();
        }
        {
            std::lock_guard lock(mutex);
            producer_done = true;
        }
        cv.notify_one();
    });

    std::uint64_t processed = 0;
    const app::InfrastructureStopReason reason = infra->run([&] {
        Event event{};
        {
            std::unique_lock lock(mutex);
            cv.wait_for(lock, kWaitTimeout, [&] { return !queue.empty() || producer_done; });
            if (queue.empty()) {
                if (producer_done) {
                    infra->request_stop(app::InfrastructureStopReason::Requested);
                    return false;
                }
                return true; // timeout: let run() check signals/time base
            }
            event = std::move(queue.front());
            queue.pop_front();
        }

        auto span = (*tracer)->start_span("event", tracing::SpanKind::Internal);
        static_cast<void>(
            span.set_attribute("sequence", static_cast<std::int64_t>(event.sequence)));
        (*events_counter)->inc();
        (*logger)->info("event seq={} mono_ns={} value={:.2f}", event.sequence,
                        event.stamp.nanoseconds(), event.value);
        static_cast<void>(span.end());
        ++processed;
        return true;
    });

    producer.request_stop();
    producer.join();

    (*logger)->info("wait loop finished: processed={}", processed);
    const core::Status shutdown = infra->shutdown();
    if (!shutdown.has_value()) {
        return fail(app_name, "Infrastructure::shutdown failed");
    }
    if (reason != app::InfrastructureStopReason::Requested) {
        return fail(app_name, "run() did not return Requested");
    }

    std::printf("%.*s: OK (processed=%llu, reason=requested)\n", static_cast<int>(app_name.size()),
                app_name.data(), static_cast<unsigned long long>(processed));
    return processed == kEventCount ? 0 : 1;
}

} // namespace nexigen::examples
