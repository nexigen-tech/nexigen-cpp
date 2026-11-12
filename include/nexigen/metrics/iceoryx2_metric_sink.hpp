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

/// @brief Sink that publishes one `MetricFrame` per harvested record over iceoryx2.
///
/// The provider's background harvester is the only caller of `on_batch`; the
/// sink fans the batch into one fixed frame per record, publishes each over the
/// configured pub-sub service and notifies listeners once per non-empty batch.
/// Nothing runs on a producer hot path, and publishing is allocation-free (one
/// reusable scratch frame copied into each published loan). Copy is deleted
/// because `MetricProvider` keeps a pointer to the sink, which must outlive the
/// provider; `node_` is borrowed and must outlive the sink.
///
/// @note Spec `metrics/iceoryx2-metric-sink`, requirement "Iceoryx2 metric sink
///       satisfies the sink concept".
class Iceoryx2MetricSink {
public:
    /// @brief Copy the configuration into fixed frame metadata.
    ///
    /// Truncates `app_id` to the frame field width, stamps the current process
    /// ID, and leaves the transport unopened until `start()` succeeds.
    ///
    /// @param config Sink configuration: app identifier, service name and
    ///        subscriber buffer depth.
    /// @param node iceoryx2 node that owns the publisher and notifier; borrowed,
    ///        must outlive this sink and may be null until `start()`.
    Iceoryx2MetricSink(const Iceoryx2MetricSinkConfig& config, com::Node* node) noexcept
        : config_{config}, node_{node}, pid_{static_cast<std::uint32_t>(::getpid())} {
        const std::size_t take = config_.app_id.size() < sizeof(frame_.app_id)
                                     ? config_.app_id.size()
                                     : sizeof(frame_.app_id);
        for (std::size_t i = 0; i < take; ++i) {
            frame_.app_id[i] = config_.app_id[i];
        }
        frame_.pid = pid_;
    }

    /// @brief Copy construction is deleted: the sink owns live transport handles.
    Iceoryx2MetricSink(const Iceoryx2MetricSink&) = delete;
    /// @brief Copy assignment is deleted: the sink owns live transport handles.
    Iceoryx2MetricSink& operator=(const Iceoryx2MetricSink&) = delete;

    /// @brief Cold start: open the pub-sub and event services and their endpoints.
    ///
    /// Creates (or opens) the configured pub-sub service, then the paired event
    /// service named `<service_name>Event`, and stores the publisher and
    /// notifier. Any failure leaves the sink unstarted and returns an error.
    ///
    /// @return Success once both endpoints exist, otherwise
    ///         `core::ErrorCode::SystemError` (null node, invalid service name,
    ///         or failed endpoint creation).
    /// @note Spec `metrics/iceoryx2-metric-sink`, requirement "Iceoryx2 metric
    ///       sink configuration".
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

    /// @brief Publish every record of the batch as a frame, then notify once.
    ///
    /// Each record is copied into the reusable scratch frame and published with
    /// `send_copy`; a transport that is out of samples drops that frame without
    /// blocking the harvester. The notifier fires once when at least one frame
    /// was published.
    ///
    /// @param batch Snapshot harvested by the provider; a null `records` pointer
    ///        is a successful no-op.
    /// @return Success, or `core::ErrorCode::NotInitialized` when `start()` has
    ///         not succeeded.
    /// @note Spec `metrics/iceoryx2-metric-sink`, requirement "Batches are
    ///       published as frames".
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
            if (!publisher_->send_copy(frame_)) {
                continue; // transport out of samples: dropped, never blocks
            }
            ++published;
        }
        if (published > 0U && notifier_.has_value()) {
            static_cast<void>(notifier_->notify());
        }
        return {};
    }

    /// @brief Complete a flush request; published frames are already durable.
    ///
    /// @return Success status, always: every frame is in shared memory at
    ///         publish time, so flush performs no I/O.
    /// @note Spec `metrics/iceoryx2-metric-sink`, requirement "Sink flush is a
    ///       no-op".
    [[nodiscard]] core::Status flush() noexcept { return {}; }

    /// @brief Drop the publisher and notifier, returning the sink to unstarted.
    ///
    /// Safe to call without a prior `start()`; `on_batch` then reports
    /// `NotInitialized` until the next successful `start()`.
    void stop() noexcept {
        publisher_.reset();
        notifier_.reset();
    }

    /// @brief Render the diagnostic string describing the transport binding.
    ///
    /// @return Service name, application identifier, PID and started flag;
    ///         allocates, cold path only.
    /// @note Spec `metrics/iceoryx2-metric-sink`, requirement "Sink diagnostics";
    ///       project mandate 9 (`to_string`).
    [[nodiscard]] std::string to_string() const {
        return "Iceoryx2MetricSink(service=" + config_.service_name + ", app=" + config_.app_id +
               ", pid=" + std::to_string(pid_) +
               ", started=" + (publisher_.has_value() ? "true" : "false") + ")";
    }

private:
    Iceoryx2MetricSinkConfig config_{};
    com::Node* node_{nullptr};
    std::optional<com::Publisher<MetricFrame>> publisher_{};
    std::optional<com::EventNotifier> notifier_{};
    std::uint32_t pid_{0};
    MetricFrame frame_{}; ///< reusable fixed scratch (no per-batch allocation)
};

/// @brief Free `to_string` helper mirroring the member function.
///
/// @param sink Sink to describe.
/// @return The same text as `Iceoryx2MetricSink::to_string`.
/// @note Spec `metrics/iceoryx2-metric-sink`, requirement "Sink diagnostics";
///       project mandate 9.
[[nodiscard]] inline std::string to_string(const Iceoryx2MetricSink& sink) {
    return sink.to_string();
}

} // namespace nexigen::metrics

/// @brief `std::formatter` for the iceoryx2 sink; renders its diagnostic string.
///
/// @note Spec `metrics/iceoryx2-metric-sink`, requirement "Sink diagnostics";
///       project mandate 11 (`std::formatter`).
template <>
struct std::formatter<nexigen::metrics::Iceoryx2MetricSink> : std::formatter<std::string_view> {
    /// @brief Write the sink's `to_string` text through the string formatter.
    ///
    /// @tparam Ctx Formatting context type supplied by `std::format`.
    /// @param sink Sink being formatted.
    /// @param ctx Destination formatting context.
    /// @return Iterator past the last written character.
    template <class Ctx>
    auto format(const nexigen::metrics::Iceoryx2MetricSink& sink, Ctx& ctx) const {
        const std::string text = sink.to_string();
        return std::formatter<std::string_view>::format(text, ctx);
    }
};
