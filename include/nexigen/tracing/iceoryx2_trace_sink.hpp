// nexigen::tracing — client-side sink publishing spans to iceoryx2 (change
// add-iceoryx2-trace-sink; spec tracing/iceoryx2-trace-sink; design D1–D9).
//
// Satisfies tracing::concepts::TraceSink. The provider's single background
// consumer calls on_span; the sink maps the in-process SpanRecord into the
// pointer-free SpanFrame, fills the publisher identity, and publishes it with
// Publisher::send_copy() — one transport call that loans a shared-memory slot,
// copies the frame into it, and sends. No producer thread ever enters this
// code, and the publish path allocates nothing.
//
// State is WORKER-CONFINED: the frame scratch, sequence, counters, and the
// source-location memo cache are touched only by the provider's consumer
// thread. No atomics are introduced; iceoryx2's own telemetry is its concern.
#pragma once

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <format>
#include <optional>
#include <string>
#include <string_view>
#include <type_traits>

#include <unistd.h>

#include <nlohmann/json.hpp>

#include <nexigen/chrono/timestamp.hpp>
#include <nexigen/com/concepts/message_concept.hpp>
#include <nexigen/com/node.hpp>
#include <nexigen/com/publisher.hpp>
#include <nexigen/com/service_name.hpp>
#include <nexigen/core/result.hpp>
#include <nexigen/tracing/concepts/tracer_concept.hpp>
#include <nexigen/tracing/detail/fnv1a_64.hpp>
#include <nexigen/tracing/iceoryx2_trace_sink_config.hpp>
#include <nexigen/tracing/span_frame.hpp>
#include <nexigen/tracing/span_record.hpp>

namespace nexigen::tracing {

/// @brief Publishes one `SpanFrame` per delivered span over an iceoryx2 service.
///
/// The provider's single background consumer calls `on_span`; the sink maps the
/// in-process `SpanRecord` into the pointer-free `SpanFrame`, stamps the
/// publisher identity (application code, pid, format version), and publishes it
/// with one `Publisher::send_copy()` call that loans a shared-memory slot,
/// copies the frame in place, and sends. Source locations cross as memoized
/// FNV-1a hashes, and the sequence counter advances on every attempt so a
/// consumer can observe gaps. A missing slot drops the frame, counts it, and
/// still reports success so the worker keeps draining.
///
/// All mutable state is worker-confined plain state: the frame scratch,
/// sequence, counters, and hash cache are touched only by the consumer thread,
/// with no atomics and no cross-thread synchronization. The publish path
/// allocates nothing after `start()`.
///
/// Copy and move are deleted because `TracerProvider` binds a pointer to the
/// sink and the sink must outlive the provider.
///
/// @note Spec `tracing/iceoryx2-trace-sink`, requirements "Sink publishes each
///       span zero-copy from the worker", "Publication never blocks and drops
///       are counted", and "Per-span formation stays within its budget"
///       (400 ns median in strict Release x86-64).
class Iceoryx2TraceSink {
public:
    /// @brief Source-location memo cache capacity: 64 slots of 16 B = 1 KiB.
    ///
    /// A power of two so the pointer hash maps to a slot with a mask; a
    /// collision merely recomputes and counts a miss.
    static constexpr std::size_t hash_cache_capacity = 64;

    /// @brief Construct the sink and pre-fill the frame's publisher identity.
    ///
    /// Copies the configuration, clamps the application code to the frame's
    /// four-byte field, caches the pid, and pins the frame format version. No
    /// port is created until `start()`.
    ///
    /// @param config Service name, application code, and subscriber depth.
    /// @param node Communication node used by `start()` to create the publisher;
    ///        must outlive the sink, may be null only until `start()` is called.
    /// @note Spec `tracing/iceoryx2-trace-sink`, requirement "Sink configuration
    ///       and diagnostics".
    Iceoryx2TraceSink(const Iceoryx2TraceSinkConfig& config, com::Node* node) noexcept
        : config_{config}, node_{node}, pid_{static_cast<std::uint32_t>(::getpid())} {
        const std::size_t take = config_.app_id.size() < sizeof(frame_.app_id)
                                     ? config_.app_id.size()
                                     : sizeof(frame_.app_id);
        for (std::size_t i = 0; i < take; ++i) {
            frame_.app_id[i] = config_.app_id[i];
        }
        frame_.pid = pid_;
        frame_.version = SpanFrame::format_version;
    }

    /// @brief Copy construction is deleted: the provider holds a pointer to the
    ///        sink and the sink must outlive it.
    Iceoryx2TraceSink(const Iceoryx2TraceSink&) = delete;
    /// @brief Copy assignment is deleted: the sink is bound by address.
    Iceoryx2TraceSink& operator=(const Iceoryx2TraceSink&) = delete;
    /// @brief Move construction is deleted: the sink is bound by address.
    Iceoryx2TraceSink(Iceoryx2TraceSink&&) = delete;
    /// @brief Move assignment is deleted: the sink is bound by address.
    Iceoryx2TraceSink& operator=(Iceoryx2TraceSink&&) = delete;

    /// @brief Validate the configuration and open the publisher (cold start).
    ///
    /// Validates the configuration first, then resolves the service name and
    /// creates the publisher with the configured subscriber buffer depth.
    ///
    /// @return Success when the publisher exists; `InvalidArgument` from
    ///         validation, or `SystemError` when the node is null, the name is
    ///         rejected, or the publisher cannot be created.
    /// @note Spec `tracing/iceoryx2-trace-sink`, requirements "Sink
    ///       configuration and diagnostics" and "Sink publishes each span
    ///       zero-copy from the worker".
    [[nodiscard]] core::Status start() noexcept {
        const core::Status valid = config_.validate();
        if (!valid) {
            return valid;
        }
        if (node_ == nullptr) {
            return std::unexpected(core::ErrorCode::SystemError);
        }
        const auto name = com::ServiceName::create(config_.service_name.c_str());
        if (!name) {
            return std::unexpected(core::ErrorCode::SystemError);
        }
        auto publisher =
            com::Publisher<SpanFrame>::create(*node_, *name, config_.subscriber_max_buffer_depth);
        if (!publisher) {
            return std::unexpected(core::ErrorCode::SystemError);
        }
        publisher_.emplace(std::move(*publisher));
        return {};
    }

    /// @brief Map one span into the wire frame and publish it in one call.
    ///
    /// The sequence advances on every attempt, so a dropped frame leaves an
    /// observable gap; a frame discarded for lack of a slot (or for one
    /// receiver's full queue) is counted in `dropped()` and still reports
    /// success, and the worker is never blocked. Mapping clips zero-copy string
    /// attributes to the inline bound.
    ///
    /// @param record Span to map and publish.
    /// @return Success including the drop case; `NotInitialized` when called
    ///         before `start()`.
    /// @note Spec `tracing/iceoryx2-trace-sink`, requirements "Publication never
    ///       blocks and drops are counted" and "Sequence numbers make gaps
    ///       observable"; allocation-free after `start()`.
    [[nodiscard]] core::Status on_span(const SpanRecord& record) noexcept {
        if (!publisher_.has_value()) {
            return std::unexpected(core::ErrorCode::NotInitialized);
        }
        map_into_frame(record);
        const std::uint64_t lost_before = publisher_->lost_samples();
        if (!publisher_->send_copy(frame_)) {
            ++dropped_;
            return {};
        }
        ++published_;
        if (publisher_->lost_samples() != lost_before) {
            // At least one receiver's queue was full: the DiscardData policy
            // dropped this sample for that receiver while send() succeeded.
            ++dropped_;
        }
        return {};
    }

    /// @brief Complete a flush request without doing any work.
    ///
    /// @return Success status, always; a sent frame is already durable in
    ///         shared memory.
    /// @note Spec `tracing/iceoryx2-trace-sink`, requirement "Sink publishes
    ///       each span zero-copy from the worker".
    [[nodiscard]] core::Status flush() noexcept { return {}; }

    /// @brief Release the publisher and its ports.
    ///
    /// Delivery after `stop()` reports `NotInitialized` until `start()` runs
    /// again; counters and the hash cache are retained.
    /// @note Spec `tracing/iceoryx2-trace-sink`, requirement "Sink publishes
    ///       each span zero-copy from the worker".
    void stop() noexcept { publisher_.reset(); }

    // --- diagnostics (worker-confined counters; cold-path readers) ----------

    /// @brief Read the construction-time configuration.
    ///
    /// @return The configuration captured at construction.
    [[nodiscard]] const Iceoryx2TraceSinkConfig& config() const noexcept { return config_; }

    /// @brief Read the cached publisher process id.
    ///
    /// @return The `::getpid()` value cached at construction.
    [[nodiscard]] std::uint32_t pid() const noexcept { return pid_; }

    /// @brief Read the number of frames successfully published.
    ///
    /// @return Published-frame counter (worker-confined plain state).
    [[nodiscard]] std::uint64_t published() const noexcept { return published_; }

    /// @brief Read the number of discarded frames.
    ///
    /// @return Dropped-frame counter, including full-slot and full-receiver
    ///         discards.
    [[nodiscard]] std::uint64_t dropped() const noexcept { return dropped_; }

    /// @brief Read the source-hash memo cache hit count.
    ///
    /// @return Number of source locations resolved from the cache.
    /// @note Spec `tracing/iceoryx2-trace-sink`, requirement "Source hashes are
    ///       memoized on the worker".
    [[nodiscard]] std::uint64_t hash_hits() const noexcept { return hash_hits_; }

    /// @brief Read the source-hash memo cache miss count.
    ///
    /// @return Number of source locations that required hashing.
    /// @note Spec `tracing/iceoryx2-trace-sink`, requirement "Source hashes are
    ///       memoized on the worker"; misses recompute but never fail.
    [[nodiscard]] std::uint64_t hash_misses() const noexcept { return hash_misses_; }

    /// @brief Report whether the publisher currently exists.
    ///
    /// @return `true` between a successful `start()` and `stop()`.
    [[nodiscard]] bool started() const noexcept { return publisher_.has_value(); }

    /// @brief Render the sink state as a cold-path diagnostic string.
    ///
    /// @return Text naming the service, application code, pid, published and
    ///         dropped totals, hash-cache counters, and start flag; allocates,
    ///         never called on the delivery path.
    /// @note Project mandate 9 (`to_string`).
    [[nodiscard]] std::string to_string() const {
        return std::format(
            "Iceoryx2TraceSink(service={}, app={}, pid={}, published={}, dropped={}, "
            "hash_hits={}, hash_misses={}, started={})",
            config_.service_name, config_.app_id, pid_, published_, dropped_, hash_hits_,
            hash_misses_, publisher_.has_value());
    }

private:
    struct HashCacheEntry {
        const char* text{nullptr};
        std::uint64_t hash{0U};
    };

    /// Stable FNV-1a hash of one source-location string, memoized by pointer.
    /// `source_location` strings live in program-static storage for the
    /// process lifetime, so pointer identity implies text identity.
    [[nodiscard]] std::uint64_t hash_location(const char* text) noexcept {
        if (text == nullptr) {
            return 0U;
        }
        const auto slot =
            (reinterpret_cast<std::uintptr_t>(text) >> 3U) & (hash_cache_capacity - 1U);
        HashCacheEntry& entry = hash_cache_[slot];
        if (entry.text == text) {
            ++hash_hits_;
            return entry.hash;
        }
        const std::uint64_t hash = detail::fnv1a_64(text);
        entry.text = text;
        entry.hash = hash;
        ++hash_misses_;
        return hash;
    }

    /// Long string attributes are stored zero-copy in the in-process record as
    /// a pointer; the wire frame cannot carry it, so clip to the inline bound.
    static void clamp_attribute(SpanAttribute& attribute) noexcept {
        if (attribute.type != SpanAttributeType::String || attribute.text_inline) {
            return;
        }
        const std::string_view text = attribute.text_view();
        const std::size_t take = text.size() < attribute_inline_text_capacity
                                     ? text.size()
                                     : attribute_inline_text_capacity;
        for (std::size_t i = 0; i < take; ++i) {
            attribute.value.inline_text.text[i] = text[i];
        }
        attribute.value.inline_text.length = static_cast<std::uint8_t>(take);
        attribute.text_inline = true;
    }

    void map_into_frame(const SpanRecord& record) noexcept {
        frame_.trace_id = record.trace_id;
        frame_.span_id = record.span_id;
        frame_.parent_span_id = record.parent_span_id;
        frame_.kind = record.kind;
        frame_.status = record.status;
        frame_.flags = record.flags;
        frame_.attribute_count = record.attribute_count <= MaxAttributes
                                     ? record.attribute_count
                                     : static_cast<std::uint8_t>(MaxAttributes);
        frame_.thread_id = record.thread_id;
        frame_.start_ns = record.start.nanoseconds();
        frame_.end_ns = record.end.nanoseconds();
        frame_.file_hash = hash_location(record.file_name);
        frame_.function_hash = hash_location(record.function_name);
        frame_.line = record.line;
        frame_.column = record.column;
        std::memcpy(frame_.name, record.name, sizeof(frame_.name));
        for (std::size_t i = 0; i < MaxAttributes; ++i) {
            frame_.attributes[i] = record.attributes[i];
            clamp_attribute(frame_.attributes[i]);
        }
        frame_.sequence = sequence_++;
        const auto now = chrono::Timestamp::now();
        frame_.publish_ts_ns = now.has_value() ? now->nanoseconds() : 0ULL;
    }

    Iceoryx2TraceSinkConfig config_{};
    com::Node* node_{nullptr};
    std::optional<com::Publisher<SpanFrame>> publisher_{};
    std::uint32_t pid_{0U};
    std::uint64_t published_{0U};
    std::uint64_t dropped_{0U};
    std::uint64_t sequence_{0U};
    std::uint64_t hash_hits_{0U};
    std::uint64_t hash_misses_{0U};
    SpanFrame frame_{}; ///< reusable fixed scratch (no per-span allocation)
    HashCacheEntry hash_cache_[hash_cache_capacity]{};
};

static_assert(concepts::TraceSink<Iceoryx2TraceSink>);
static_assert(com::concepts::Message<SpanFrame>, "SpanFrame must satisfy the Message contract");

/// @brief Free diagnostic helper mirroring `Iceoryx2TraceSink::to_string()`.
///
/// @param sink Sink to describe.
/// @return The same text the member returns.
/// @note Project mandate 9 (`to_string`); cold path only.
[[nodiscard]] inline std::string to_string(const Iceoryx2TraceSink& sink) {
    return sink.to_string();
}

/// @brief Serialize the live sink's configuration and counters as JSON.
///
/// The live sink is not reconstructible: there is deliberately no `from_json`
/// (spec "Sink configuration and diagnostics").
///
/// @param j Destination JSON value, overwritten with the sink state.
/// @param sink Sink to describe.
/// @note Spec `tracing/iceoryx2-trace-sink`, requirement "Sink configuration
///       and diagnostics"; project mandate 10 (JSON).
inline void to_json(nlohmann::json& j, const Iceoryx2TraceSink& sink) {
    j = nlohmann::json{
        {"service_name", sink.config().service_name},
        {"app_id", sink.config().app_id},
        {"subscriber_max_buffer_depth", sink.config().subscriber_max_buffer_depth},
        {"pid", sink.pid()},
        {"published", sink.published()},
        {"dropped", sink.dropped()},
        {"hash_hits", sink.hash_hits()},
        {"hash_misses", sink.hash_misses()},
        {"started", sink.started()},
    };
}

} // namespace nexigen::tracing

/// @brief `std::formatter` for `Iceoryx2TraceSink`.
///
/// @note Project mandate 11 (`std::formatter`); formatting is cold path only.
template <>
struct std::formatter<nexigen::tracing::Iceoryx2TraceSink> : std::formatter<std::string_view> {
    /// @brief Format the sink through its `to_string()` text.
    ///
    /// @tparam Ctx Format context type supplied by `std::format`.
    /// @param sink Sink to render.
    /// @param ctx Output context receiving the text.
    /// @return Iterator past the last written character.
    template <class Ctx>
    auto format(const nexigen::tracing::Iceoryx2TraceSink& sink, Ctx& ctx) const {
        const std::string text = sink.to_string();
        return std::formatter<std::string_view>::format(text, ctx);
    }
};
