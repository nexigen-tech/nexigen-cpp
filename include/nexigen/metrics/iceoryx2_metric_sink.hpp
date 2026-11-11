// nexigen::metrics — client-side sink publishing harvested batches to
// iceoryx2 (change add-metrics-daemon; spec metrics/iceoryx2-metric-sink;
// design D1/D2/D3).
//
// Satisfies concepts::MetricSink. The provider's background harvester calls
// on_batch; the sink fans the batch out into one MetricFrame per record,
// publishes each over the configured pub-sub service, and notifies listeners
// once per batch. Nothing here runs on a producer hot path: the provider only
// delivers batches on its harvester thread, and the publish path is
// allocation-free (a fixed scratch frame + zero-copy shared-memory loans).
#pragma once

#include <cstdint>
#include <cstring>
#include <format>
#include <optional>
#include <string>
#include <string_view>

#include <unistd.h>

#include <nexigen/com/event.hpp>
#include <nexigen/com/node.hpp>
#include <nexigen/com/publisher.hpp>
#include <nexigen/com/service_name.hpp>
#include <nexigen/core/result.hpp>
#include <nexigen/metrics/iceoryx2_metric_sink_config.hpp>
#include <nexigen/metrics/metric_frame.hpp>
#include <nexigen/metrics/metric_snapshot.hpp>

namespace nexigen::metrics {

/// A sink that publishes one MetricFrame per harvested record over iceoryx2.
/// Copy is deleted because MetricProvider binds a pointer to the sink; the
/// sink must outlive the provider.
class Iceoryx2MetricSink {
public:
    Iceoryx2MetricSink(const Iceoryx2MetricSinkConfig& config, com::Node* node) noexcept
        : config_{config}, node_{node}, pid_{static_cast<std::uint32_t>(::getpid())} {
        const std::size_t take =
            config_.app_id.size() < sizeof(frame_.app_id) ? config_.app_id.size()
                                                          : sizeof(frame_.app_id);
        for (std::size_t i = 0; i < take; ++i) {
            frame_.app_id[i] = config_.app_id[i];
        }
        frame_.pid = pid_;
    }

    Iceoryx2MetricSink(const Iceoryx2MetricSink&) = delete;
    Iceoryx2MetricSink& operator=(const Iceoryx2MetricSink&) = delete;

    /// Cold start: open (or create) the pub-sub service and the paired event
    /// service, then create the publisher and the notifier.
    [[nodiscard]] core::Status start() noexcept {
        if (node_ == nullptr) {
            return std::unexpected(core::ErrorCode::SystemError);
        }
        const auto name = com::ServiceName::create(config_.service_name.c_str());
        if (!name) {
            return std::unexpected(core::ErrorCode::SystemError);
        }
        auto publisher =
            com::Publisher<MetricFrame>::create(*node_, *name, config_.subscriber_max_buffer_size);
        if (!publisher) {
            return std::unexpected(core::ErrorCode::SystemError);
        }
        publisher_.emplace(std::move(*publisher));

        const std::string event_service = config_.service_name + "Event";
        const auto event_name = com::ServiceName::create(event_service.c_str());
        if (!event_name) {
            return std::unexpected(core::ErrorCode::SystemError);
        }
        auto notifier = com::EventNotifier::create(*node_, *event_name);
        if (!notifier) {
            return std::unexpected(core::ErrorCode::SystemError);
        }
        notifier_.emplace(std::move(*notifier));
        return {};
    }

    /// Publish every record of the batch as a frame, notify once per batch,
    /// and never block the harvester (out-of-samples drops the frame).
    [[nodiscard]] core::Status on_batch(const MetricBatchSnapshot& batch) noexcept {
        if (!publisher_.has_value()) {
            return std::unexpected(core::ErrorCode::NotInitialized);
        }
        if (batch.records == nullptr) {
            return {};
        }
        frame_.timestamp_ns = static_cast<std::int64_t>(batch.stamp.nanoseconds());
        std::uint32_t published = 0;
        for (std::uint32_t i = 0; i < batch.record_count; ++i) {
            frame_.record = batch.records[i];
            auto loaned = publisher_->loan_uninit();
            if (!loaned) {
                continue; // transport out of samples: dropped, never blocks
            }
            auto sample = loaned->write_payload(frame_);
            static_cast<void>(publisher_->send(std::move(sample)));
            ++published;
        }
        if (published > 0U && notifier_.has_value()) {
            static_cast<void>(notifier_->notify());
        }
        return {};
    }

    /// Every frame is already durable in shared memory at publish time.
    [[nodiscard]] core::Status flush() noexcept { return {}; }

    void stop() noexcept {
        publisher_.reset();
        notifier_.reset();
    }

    /// Cold-path diagnostic (mandate 9).
    [[nodiscard]] std::string to_string() const {
        return "Iceoryx2MetricSink(service=" + config_.service_name + ", app=" + config_.app_id +
               ", pid=" + std::to_string(pid_) + ", started=" +
               (publisher_.has_value() ? "true" : "false") + ")";
    }

private:
    Iceoryx2MetricSinkConfig config_{};
    com::Node* node_{nullptr};
    std::optional<com::Publisher<MetricFrame>> publisher_{};
    std::optional<com::EventNotifier> notifier_{};
    std::uint32_t pid_{0};
    MetricFrame frame_{}; ///< reusable fixed scratch (no per-batch allocation)
};

/// Free helper (mandate 9): mirrors the member.
[[nodiscard]] inline std::string to_string(const Iceoryx2MetricSink& sink) {
    return sink.to_string();
}

} // namespace nexigen::metrics

/// The sink formats as its diagnostic string (mandate 11).
template <>
struct std::formatter<nexigen::metrics::Iceoryx2MetricSink> : std::formatter<std::string_view> {
    template <class Ctx>
    auto format(const nexigen::metrics::Iceoryx2MetricSink& sink, Ctx& ctx) const {
        const std::string text = sink.to_string();
        return std::formatter<std::string_view>::format(text, ctx);
    }
};
