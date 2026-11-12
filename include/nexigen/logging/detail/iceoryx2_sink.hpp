// nexigen::logging — consumer-thread sink publishing LogFrame to iceoryx2
// (change add-iceoryx2-logger; spec logging/iceoryx2-logger; design D3-D5).
//
// The Sink contract (duck-typed, see detail::async_worker.hpp): start() /
// handle(Record&) / synchronize() / stop(), constructed in place by
// AsyncWorker::start on the worker thread. Unlike FileSink (which renders a
// JSON line and writes a file), this sink renders the message text, packs a
// fixed LogFrame, and publishes it over pub-sub + an event notify — the final
// line formatting and disk I/O belong to the LogDaemon (out of scope).
#pragma once

#include <cstdint>
#include <cstring>
#include <optional>
#include <string>
#include <unistd.h>

#include <nexigen/com/event.hpp>
#include <nexigen/com/node.hpp>
#include <nexigen/com/publisher.hpp>
#include <nexigen/com/service_name.hpp>
#include <nexigen/core/result.hpp>
#include <nexigen/logging/log_frame.hpp>

namespace nexigen::logging::detail {

/// @brief Consumer-thread sink publishing rendered `LogFrame`s to iceoryx2.
///
/// Implements the duck-typed Sink contract (`start()` / `handle(Record&)` /
/// `synchronize()` / `stop()`) and is constructed in place by
/// `AsyncWorker::start` on the worker thread. Unlike the file sink, it renders
/// the message text, packs a fixed `LogFrame`, publishes it over pub-sub, then
/// notifies the companion event service; final line formatting and disk I/O
/// belong to the `LogDaemon` (out of scope).
///
/// @tparam Producer Producer type exposing `dropped()`, whose snapshot has
///         `total()`; read relaxed from the consumer thread only.
/// @note Spec `logging/iceoryx2-logger`, requirement "Worker publishes frames
///       off the hot path"; the producer never formats or touches the
///       transport.
template <class Producer> class Iceoryx2Sink {
public:
    /// @brief Fixed-footprint snapshot of the validated config.
    ///
    /// Copied at `initialize()` so the sink never holds caller string_views at
    /// runtime; the service name is staged NUL-terminated in place, so
    /// `start()` can hand it to the transport without allocating.
    ///
    /// @note Spec `logging/iceoryx2-logger`, requirement "Iceoryx2 logger
    ///       configuration".
    struct Settings {
        /// @brief Service name bytes, NUL-terminated inside the fixed array.
        char service_name[512]{};
        /// @brief Number of meaningful bytes in `service_name`.
        std::size_t service_name_len = 0;
        /// @brief Fixed 4-byte application tag copied into every frame.
        char app_id[4]{};
        /// @brief Configured subscriber ceiling, validated before the worker.
        std::uint32_t max_subscribers = 16;
        /// @brief Configured history depth, validated before the worker.
        std::uint32_t history_size = 4;
    };

    /// @brief Construct the sink around a config snapshot and transport node.
    ///
    /// @param settings Validated config copy; stored by value.
    /// @param producer Producer supplying the drop snapshot; may be null.
    /// @param node Transport node used by `start()`; may be null (then
    ///        `start()` fails) and must outlive the sink.
    Iceoryx2Sink(Settings settings, const Producer* producer, com::Node* node) noexcept
        : settings_{settings}, producer_(producer), node_(node) {}

    /// @brief Copy construction is deleted: the sink owns transport handles.
    Iceoryx2Sink(const Iceoryx2Sink&) = delete;
    /// @brief Copy assignment is deleted: the sink owns transport handles.
    Iceoryx2Sink& operator=(const Iceoryx2Sink&) = delete;

    /// @brief Worker-thread cold start: create the pub/sub and event services.
    ///
    /// Opens or creates the service and its `"<service>Event"` companion (two
    /// services per REQ_LOGC_005), then creates the publisher and notifier and
    /// caches the process id. A failure is reported in the worker start-code
    /// snapshot and never stops the drain loop.
    ///
    /// @return `Status`: OK when both services are ready; `SystemError` when
    ///         the node is null, the service names are rejected, or the
    ///         publisher/notifier cannot be created.
    /// @note Spec `logging/iceoryx2-logger`, requirement "Iceoryx2 logger
    ///       configuration" and requirement "Worker publishes frames off the
    ///       hot path"; cold path, may allocate inside the transport.
    [[nodiscard]] core::Status start() noexcept {
        if (node_ == nullptr) {
            return std::unexpected(core::ErrorCode::SystemError);
        }
        const std::string service(settings_.service_name, settings_.service_name_len);
        const auto name = com::ServiceName::create(service.c_str());
        if (!name) {
            return std::unexpected(core::ErrorCode::SystemError);
        }
        auto publisher = com::Publisher<LogFrame>::create(*node_, *name);
        if (!publisher) {
            return std::unexpected(core::ErrorCode::SystemError);
        }
        publisher_.emplace(std::move(*publisher));

        const std::string event_service = service + "Event";
        const auto event_name = com::ServiceName::create(event_service.c_str());
        if (!event_name) {
            return std::unexpected(core::ErrorCode::SystemError);
        }
        auto notifier = com::EventNotifier::create(*node_, *event_name);
        if (!notifier) {
            return std::unexpected(core::ErrorCode::SystemError);
        }
        notifier_.emplace(std::move(*notifier));
        sink_pid_ = static_cast<std::uint32_t>(::getpid());
        return {};
    }

    /// @brief Render one record into a frame, pack the metadata, and publish.
    ///
    /// Deferred rendering executes here, on the consumer thread, into the
    /// frame payload, allocation-free. A frame has a single transport call
    /// (loan, copy, send); a failed publish drops that frame and never blocks
    /// (`DiscardData`), and the event notify is best-effort.
    ///
    /// @tparam Record Record type exposing `render_into`, `stamp`,
    ///         `thread_id`, and `level`.
    /// @param rec Drained record; read only, never retained.
    /// @note Spec `logging/iceoryx2-logger`, requirement "Worker publishes
    ///       frames off the hot path" and requirement "Log frame is a fixed
    ///       zero-copy frame"; deferred rendering per `logging/log-record`,
    ///       requirement "Consumer-side rendering of packed records".
    ///       Publication works with zero subscribers.
    template <class Record> void handle(Record& rec) noexcept {
        if (!publisher_.has_value()) {
            return;
        }
        const std::size_t rendered =
            rec.render_into(reinterpret_cast<char*>(frame_.payload), sizeof(frame_.payload));
        frame_.timestamp_ns = rec.stamp.nanoseconds();
        frame_.thread_id = rec.thread_id;
        frame_.pid = sink_pid_;
        frame_.log_level = rec.level;
        frame_.context_id = 0U; // reserved until per-context thresholds land (LOGC_003)
        frame_.payload_size = static_cast<std::uint16_t>(
            rendered < sizeof(frame_.payload) ? rendered : sizeof(frame_.payload));
        frame_.dropped_counter = dropped_delta();
        std::memcpy(frame_.app_id, settings_.app_id, sizeof(frame_.app_id));

        // One transport call: loan, copy and send. A failed publish drops the
        // frame and never blocks (DiscardData).
        if (!publisher_->send_copy(frame_)) {
            return;
        }
        if (notifier_.has_value()) {
            static_cast<void>(notifier_->notify());
        }
    }

    /// @brief No-op synchronization: frame data is already in shared memory.
    ///
    /// @return `Status`: always OK; there is no fsync equivalent for a
    ///         published frame.
    /// @note Spec `logging/iceoryx2-logger`, requirement "Worker publishes
    ///       frames off the hot path".
    [[nodiscard]] core::Status synchronize() noexcept { return {}; }

    /// @brief Release the publisher and notifier; safe to call repeatedly.
    void stop() noexcept {
        publisher_.reset();
        notifier_.reset();
    }

private:
    /// @brief Compute the drop delta carried by the next frame.
    ///
    /// Reads the producer's relaxed `dropped().total()` snapshot, subtracts
    /// the previous baseline, then makes the current total the new baseline.
    /// Saturating: a summary-driven counter reset yields 0, never a bogus huge
    /// delta.
    ///
    /// @return Drops since the previous frame, clamped to `UINT32_MAX`; 0 when
    ///         no producer is attached or the total went backwards.
    /// @note Spec `logging/iceoryx2-logger`, requirement "Drop count crosses
    ///       the process boundary"; consumer-thread only.
    [[nodiscard]] std::uint32_t dropped_delta() noexcept {
        const std::uint64_t total = producer_ != nullptr ? producer_->dropped().total() : 0ULL;
        const std::uint64_t delta =
            total > last_dropped_total_ ? total - last_dropped_total_ : 0ULL;
        last_dropped_total_ = total;
        return static_cast<std::uint32_t>(delta < UINT32_MAX ? delta : UINT32_MAX);
    }

    /// @brief Validated config snapshot; never re-read from the caller.
    Settings settings_{};
    /// @brief Producer drop-snapshot source; may be null.
    const Producer* producer_{};
    /// @brief Transport node supplied by the logger; owned by the logger.
    com::Node* node_{};
    /// @brief Live publisher, empty until `start()` succeeds.
    std::optional<com::Publisher<LogFrame>> publisher_{};
    /// @brief Live event notifier, empty until `start()` succeeds.
    std::optional<com::EventNotifier> notifier_{};
    /// @brief Previous `dropped().total()` baseline for the frame delta.
    std::uint64_t last_dropped_total_{0};
    /// @brief Cached OS pid published in every frame.
    std::uint32_t sink_pid_{0};
    /// @brief Reused frame storage; packed in place before each publication.
    LogFrame frame_{};
};

} // namespace nexigen::logging::detail
