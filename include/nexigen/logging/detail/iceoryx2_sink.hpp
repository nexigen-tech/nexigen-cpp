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

/// Publishes rendered LogFrames on the worker thread. Templated on the
/// Producer only so the drop-delta can read the producer's dropped() snapshot
/// (third-thread relaxed read, safe on the consumer thread).
template <class Producer> class Iceoryx2Sink {
public:
    /// Fixed-footprint snapshot of the validated config (copied at
    /// initialize() — the sink never holds caller string_views at runtime).
    struct Settings {
        char service_name[512]{};
        std::size_t service_name_len = 0;
        char app_id[4]{};
        std::uint32_t max_subscribers = 16;
        std::uint32_t history_size = 4;
    };

    Iceoryx2Sink(Settings settings, const Producer* producer, com::Node* node) noexcept
        : settings_{settings}, producer_(producer), node_(node) {}

    Iceoryx2Sink(const Iceoryx2Sink&) = delete;
    Iceoryx2Sink& operator=(const Iceoryx2Sink&) = delete;

    /// Worker-thread cold start: open_or_create the pub/sub service and the
    /// event service (two services per REQ_LOGC_005), and create the publisher
    /// and notifier. A failure is reported in the worker start-code snapshot
    /// and never stops the drain loop (design D15 philosophy).
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

    /// Render the deferred format into the frame payload, pack the metadata,
    /// and publish. Deferred rendering executes HERE (spec logging/log-record),
    /// allocation-free; the producer never formats.
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

        auto loaned = publisher_->loan_uninit();
        if (!loaned) {
            return; // transport out of samples: dropped, never blocks (DiscardData)
        }
        auto sample = loaned->write_payload(frame_);
        static_cast<void>(publisher_->send(std::move(sample)));
        if (notifier_.has_value()) {
            static_cast<void>(notifier_->notify());
        }
    }

    /// Data is already in shared memory at send() time; no fsync equivalent.
    [[nodiscard]] core::Status synchronize() noexcept { return {}; }

    void stop() noexcept {
        publisher_.reset();
        notifier_.reset();
    }

private:
    /// Drop delta since the previous frame (REQ_LOGC_007): the worker packs the
    /// freshly dropped count into the next frame and then treats the current
    /// total as the new baseline. Saturating: a summary-reset (counter drop)
    /// yields 0 rather than a bogus huge delta.
    [[nodiscard]] std::uint32_t dropped_delta() noexcept {
        const std::uint64_t total = producer_ != nullptr ? producer_->dropped().total() : 0ULL;
        const std::uint64_t delta =
            total > last_dropped_total_ ? total - last_dropped_total_ : 0ULL;
        last_dropped_total_ = total;
        return static_cast<std::uint32_t>(delta < UINT32_MAX ? delta : UINT32_MAX);
    }

    Settings settings_{};
    const Producer* producer_{};
    com::Node* node_{};
    std::optional<com::Publisher<LogFrame>> publisher_{};
    std::optional<com::EventNotifier> notifier_{};
    std::uint64_t last_dropped_total_{0};
    std::uint32_t sink_pid_{0};
    LogFrame frame_{};
};

} // namespace nexigen::logging::detail
