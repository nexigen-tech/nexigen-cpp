// nexigen::integration simulation_suite — shared receiver body
// (change add-simulation-suite-example; spec examples/simulation-suite).
//
// Both receivers run exactly this loop, so the working receiver and the flaky
// one cannot drift apart: receive a vector, link the span to the incoming
// trace, print the index, the ten values and the measured latency, meter the
// result, publish a checkpoint, and honour the published Function Group state.
#pragma once

#include <chrono>
#include <cstdint>
#include <cstdio>
#include <format>
#include <optional>
#include <string>
#include <string_view>
#include <thread>

#include <nexigen/com/event.hpp>
#include <nexigen/com/waitset.hpp>
#include <nexigen/core/result.hpp>
#include <nexigen/tracing/scoped_span.hpp>
#include <nexigen/tracing/span_kind.hpp>
#include <nexigen/tracing/span_status.hpp>
#include <nexigen/tracing/trace_header.hpp>

#include "sim_common.hpp"
#include "vector_sample.hpp"

namespace simulation_suite {

/// @brief Fallback poll interval used only when the event channel is missing.
inline constexpr std::chrono::milliseconds k_receive_poll_period{50};

/// @brief WaitSet timeout, so the group state and the deadlines are re-checked.
///
/// The receiver wakes on the generator's event; this bound only guarantees the
/// loop notices a stop request or a Function Group change while idle.
inline constexpr std::chrono::milliseconds k_state_poll_period{200};

/// @brief How long one spin slice runs before the loop yields to the harness.
///
/// Bounding the slice keeps `Infrastructure::run` able to observe a stop
/// request even though the receiver never blocks in spin mode.
inline constexpr std::chrono::milliseconds k_spin_slice{20};

/// @brief One received vector, already rendered for the console.
///
/// The latency is passed in rather than re-read, so the console line and the
/// metric report the same measurement taken immediately after `receive()`.
///
/// @param payload Vector received from the transport.
/// @param latency_us Transport latency measured at the receive instant.
/// @param trace_id Trace id carried by the transport header.
/// @return Console line naming the index, the ten values, and the latency.
[[nodiscard]] inline std::string render_received(const VectorSample& payload,
                                                 std::uint64_t latency_us, std::uint64_t trace_id) {
    return std::format("{} latency_us={} trace_id={:016x}", payload.to_string(), latency_us,
                       trace_id);
}

/// @brief Drain one receive attempt and report every vector it yielded.
///
/// @tparam Subscriber Concrete subscriber type.
/// @tparam Counter Concrete counter pointer type.
/// @tparam Gauge Concrete gauge pointer type.
/// @param subscriber Open subscription on the vector service.
/// @param tracer Tracer used for the per-vector consumer span.
/// @param logger Logger used for the per-vector record.
/// @param counter `vectors_received` counter.
/// @param latency `last_latency_us` gauge.
/// @param link Platform link used for the checkpoint.
/// @param allow_print Whether the Function Group state permits console output.
/// @return The number of vectors drained, or `-1` when the transport failed.
template <class Subscriber, class Counter, class Gauge>
int drain_once(Subscriber& subscriber, auto* tracer, auto& logger, Counter* counter, Gauge* latency,
               PlatformLink& link, bool allow_print) {
    int drained = 0;
    for (;;) {
        auto result = subscriber.receive();
        if (!result.has_value()) {
            logger.warning("receive failed: {}", com::to_string(result.error()));
            return -1;
        }
        if (!result->has_value()) {
            return drained;
        }
        auto& message = **result;
        // The receive instant is captured before any receiver-side work, so the
        // reported latency measures transport and scheduling, not tracing.
        const std::uint64_t received_ns = now_ns();
        const VectorSample& payload = message.payload();
        const tracing::TraceHeader& header = message.user_header();
        const std::uint64_t elapsed =
            received_ns > payload.sent_ns ? received_ns - payload.sent_ns : 0U;
        const std::uint64_t latency_us = elapsed / 1000U;

        auto& context = tracing::current_trace_context();
        context.trace_id = header.trace_id;
        context.source_span_id = header.source_span_id;
        auto span = tracer->start_span("process-vector", tracing::SpanKind::Consumer);
        static_cast<void>(
            span.set_attribute("sequence", static_cast<std::int64_t>(payload.sequence)));

        counter->inc();
        latency->set(static_cast<std::int64_t>(latency_us));
        if (allow_print) {
            std::printf("%s\n",
                        render_received(payload, latency_us, header.trace_id.value()).c_str());
            std::fflush(stdout);
            static_cast<void>(link.checkpoint(1));
        }
        logger.debug("received vector #{} latency_us={} trace_id={:016x}", payload.sequence,
                     latency_us, header.trace_id.value());
        static_cast<void>(span.set_status(tracing::SpanStatus::Ok));
        ++drained;
    }
}

/// @brief Run the receiver loop until the deadline.
///
/// @tparam Infrastructure Concrete infrastructure type.
/// @tparam Subscriber Concrete subscriber type.
/// @tparam Counter Concrete counter pointer type.
/// @tparam Gauge Concrete gauge pointer type.
/// @param infra Initialized infrastructure.
/// @param subscriber Open subscription.
/// @param link Platform link.
/// @param tracer Tracer used for the per-vector span.
/// @param counter `vectors_received` counter.
/// @param latency `last_latency_us` gauge.
/// @param deadline Absolute stop time.
/// @param crash_at Instant the flaky receiver must terminate at, or
///        `time_point::max()` for the well-behaved receiver.
/// @param crash_due Receives `true` when the run stopped because of `crash_at`;
///        may be `nullptr`.
/// @param listener Wake event the generator rings after each publish; `nullptr`
///        falls back to a fixed poll period.
/// @param spin Busy-wait on the subscription instead of blocking on the event,
///        trading a full core for the shortest possible wake-to-read path.
/// @return The infrastructure stop reason.
template <class Infrastructure, class Subscriber, class Counter, class Gauge>
[[nodiscard]] app::InfrastructureStopReason run_receiver_loop(
    Infrastructure& infra, Subscriber& subscriber, PlatformLink& link, auto* tracer,
    Counter* counter, Gauge* latency, std::chrono::steady_clock::time_point deadline,
    std::chrono::steady_clock::time_point crash_at = std::chrono::steady_clock::time_point::max(),
    bool* crash_due = nullptr, com::EventListener* listener = nullptr, bool spin = false) {
    // The receiver runs inside Infrastructure::run(), whose signal watcher
    // already owns SIGINT/SIGTERM. Creating the WaitSet without its own signal
    // handling avoids two competing handlers for the same process.
    std::optional<com::WaitSet> waitset{};
    std::optional<com::WaitSetGuard> guard{};
    if (listener != nullptr) {
        auto created = com::WaitSet::create_without_signal_handling();
        if (created.has_value()) {
            waitset.emplace(std::move(*created));
            auto attached = waitset->attach_notification(*listener);
            if (attached.has_value()) {
                guard.emplace(std::move(*attached));
            }
        }
    }

    // Cache the last state so the loop logs transitions, not every poll. An
    // empty state is the standalone case and means "no State Manager yet".
    std::string last_state{};
    auto last_state_check = std::chrono::steady_clock::now();
    bool allowed = state_allows_run(link.group_state());
    return infra.run([&]() -> bool {
        const auto now = std::chrono::steady_clock::now();
        if (now >= deadline) {
            return false;
        }
        if (now >= crash_at) {
            if (crash_due != nullptr) {
                *crash_due = true;
            }
            return false;
        }

        // State is a control-plane input, not a hot-path signal: checking it at
        // the bounded poll period is enough and keeps the receive path clean.
        if (now - last_state_check >= k_state_poll_period) {
            last_state_check = now;
            const std::string_view state = link.group_state();
            if (state != last_state) {
                (*infra.logger())->info("group state '{}'", state_name(state));
                last_state = state;
            }
            allowed = state_allows_run(state);
            if (!allowed) {
                (*infra.logger())->debug("receiver paused while group is '{}'", state_name(last_state));
            }
        }

        if (spin) {
            // Busy-wait: no scheduler in the path, at the cost of a full core.
            // The slice bound is what still lets the stop request be observed.
            const auto slice_end = now + k_spin_slice;
            while (std::chrono::steady_clock::now() < slice_end) {
                if (std::chrono::steady_clock::now() >= crash_at) {
                    if (crash_due != nullptr) {
                        *crash_due = true;
                    }
                    return false;
                }
                if (drain_once(subscriber, tracer, **infra.logger(), counter, latency, link,
                               allowed) != 0) {
                    return true;
                }
            }
            return true;
        }

        if (waitset.has_value() && guard.has_value()) {
            // Block until the generator rings: the sample is already queued, so
            // the drain below sees it without a polling delay. The timeout is
            // only the idle-state re-check, not the data path.
            static_cast<void>(waitset->wait_and_process_once_with_timeout(
                [&](com::WaitSetAttachmentId attachment) -> com::CallbackProgression {
                    if (attachment.has_event_from(*guard)) {
                        static_cast<void>(listener->try_wait([](com::EventId) {}));
                        static_cast<void>(drain_once(subscriber, tracer, **infra.logger(), counter,
                                                     latency, link, allowed));
                    }
                    return com::CallbackProgression::Continue;
                },
                k_state_poll_period));
            return true;
        }

        // Fallback: the event channel is unavailable (standalone or a failed
        // open), so poll the queue. This path is intentionally slower but keeps
        // the example functional without the companion event service.
        std::this_thread::sleep_for(k_receive_poll_period);
        static_cast<void>(
            drain_once(subscriber, tracer, **infra.logger(), counter, latency, link, allowed));
        return true;
    });
}

} // namespace simulation_suite
