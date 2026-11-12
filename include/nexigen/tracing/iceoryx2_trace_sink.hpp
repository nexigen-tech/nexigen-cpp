// nexigen::tracing — client-side sink publishing spans to iceoryx2 (change
// add-iceoryx2-trace-sink; spec tracing/iceoryx2-trace-sink; design D1–D9).
//
// Satisfies tracing::concepts::TraceSink. The provider's single background
// consumer calls on_span; the sink maps the in-process SpanRecord into the
// pointer-free SpanFrame, fills the publisher identity, loans one shared-memory
// slot, writes the frame in place, and sends. No producer thread ever enters
// this code, and the publish path allocates nothing.
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

/// Publishes one SpanFrame per delivered span over an iceoryx2 pub-sub
/// service. Copy and move are deleted because TracerProvider binds a pointer
/// to the sink and the sink must outlive the provider.
class Iceoryx2TraceSink {
public:
    /// Source-location memo cache size (power of two; 64 × 16 B = 1 KiB).
    static constexpr std::size_t hash_cache_capacity = 64;

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

    Iceoryx2TraceSink(const Iceoryx2TraceSink&) = delete;
    Iceoryx2TraceSink& operator=(const Iceoryx2TraceSink&) = delete;
    Iceoryx2TraceSink(Iceoryx2TraceSink&&) = delete;
    Iceoryx2TraceSink& operator=(Iceoryx2TraceSink&&) = delete;

    /// Cold start: validate the configuration, open (or create) the pub-sub
    /// service, and create the publisher with the configured subscriber depth.
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

    /// Map one span into the wire frame and publish it. The sequence advances
    /// on every attempt (so a dropped frame leaves an observable gap), a
    /// discarded frame is counted and still reports success, and the worker
    /// is never blocked.
    [[nodiscard]] core::Status on_span(const SpanRecord& record) noexcept {
        if (!publisher_.has_value()) {
            return std::unexpected(core::ErrorCode::NotInitialized);
        }
        map_into_frame(record);
        const std::uint64_t lost_before = publisher_->lost_samples();
        auto loaned = publisher_->loan_uninit();
        if (!loaned) {
            ++dropped_;
            return {};
        }
        auto sample = loaned->write_payload(frame_);
        const auto sent = publisher_->send(std::move(sample));
        if (!sent) {
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

    /// Every sent frame is already durable in shared memory.
    [[nodiscard]] core::Status flush() noexcept { return {}; }

    void stop() noexcept { publisher_.reset(); }

    // --- diagnostics (worker-confined counters; cold-path readers) ----------

    [[nodiscard]] const Iceoryx2TraceSinkConfig& config() const noexcept { return config_; }
    [[nodiscard]] std::uint32_t pid() const noexcept { return pid_; }
    [[nodiscard]] std::uint64_t published() const noexcept { return published_; }
    [[nodiscard]] std::uint64_t dropped() const noexcept { return dropped_; }
    [[nodiscard]] std::uint64_t hash_hits() const noexcept { return hash_hits_; }
    [[nodiscard]] std::uint64_t hash_misses() const noexcept { return hash_misses_; }
    [[nodiscard]] bool started() const noexcept { return publisher_.has_value(); }

    /// Cold-path diagnostic (mandate 9).
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

/// Free helper (mandate 9): mirrors the member.
[[nodiscard]] inline std::string to_string(const Iceoryx2TraceSink& sink) {
    return sink.to_string();
}

/// Cold-path ADL JSON (mandate 10). The live sink is NOT reconstructible: there
/// is deliberately no from_json.
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

/// The sink formats as its diagnostic string (mandate 11).
template <>
struct std::formatter<nexigen::tracing::Iceoryx2TraceSink> : std::formatter<std::string_view> {
    template <class Ctx>
    auto format(const nexigen::tracing::Iceoryx2TraceSink& sink, Ctx& ctx) const {
        const std::string text = sink.to_string();
        return std::formatter<std::string_view>::format(text, ctx);
    }
};
