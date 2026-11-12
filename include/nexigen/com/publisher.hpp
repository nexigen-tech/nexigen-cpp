// nexigen::com — zero-copy publisher (change add-com-module;
// spec com/publish-subscribe "Zero-copy publication" / "Port diagnostics").
//
// Thin wrapper over iox2::Publisher: loan_uninit() borrows a shared-memory
// slot, write_payload() constructs in place, send() hands the slot over.
// send_copy() is the single-call alternative for worker-owned, trivially
// copyable payloads: the transport loans a slot, copies the caller's value
// into it once, and sends, updating the same telemetry as send().
// Delivery is non-blocking with the discard policy (REQ_COM_007), and the
// send path performs one relaxed store into the telemetry cell.
#pragma once

#include <atomic>
#include <cstdint>
#include <format>
#include <string>
#include <type_traits>
#include <utility>

#include <iox2/iceoryx2.hpp>

#include <nexigen/com/concepts/message_concept.hpp>
#include <nexigen/com/detail/error_mapping.hpp>
#include <nexigen/com/error.hpp>
#include <nexigen/com/node.hpp>
#include <nexigen/com/sample_mut.hpp>
#include <nexigen/com/service_name.hpp>

namespace nexigen::com {

/// @brief Zero-copy publisher of payload type `T` for one service.
///
/// `Publisher` is the producing endpoint of the `com` publish-subscribe
/// transport. It wraps `iox2::Publisher` so application code never names an
/// `iox2::` type (spec `com/service-management`, "Protocol-independent
/// boundary"). Two publication styles are available:
/// - `loan_uninit()` + `write_payload()` + `send()` for payloads that must be
///   constructed in place or written incrementally;
/// - `send_copy()` for worker-owned, fully formed, trivially copyable values
///   published in a single transport call.
///
/// Delivery is always non-blocking with the discard policy: a slow subscriber
/// can never stall the publishing thread, and an undeliverable sample is
/// counted rather than retried.
///
/// The publisher owns the transport handle, so copying is disabled and moving
/// transfers ownership. All publication calls must come from one thread; the
/// instance itself is not internally synchronized.
///
/// @tparam T Payload type. Must satisfy `com::concepts::Message`, i.e.
///         `std::is_trivially_copyable_v<T>`: no pointers, virtual tables, or
///         owning members may cross shared memory (spec
///         `com/publish-subscribe`, "Trivially-copyable message contract").
/// @tparam H Optional user-header type published alongside the payload.
///         `void` (the default) means no header; a non-void `H` must also
///         satisfy `com::concepts::Message`.
/// @note Implements `com/publish-subscribe`: "Zero-copy publication",
///       "Non-blocking overflow", "Subscriber lag telemetry", and
///       "Port diagnostics". The service is opened or created by `create()`
///       (spec `com/service-management`, "Node and daemon-less discovery").
template <concepts::Message T, typename H = void> class Publisher {
    static_assert(std::is_void_v<H> || concepts::Message<H>,
                  "the custom header must satisfy com::concepts::Message");

public:
    /// @brief Open or create the service on `node` and create the publisher.
    ///
    /// Uses the transport's default subscriber buffer depth. The service is
    /// opened with safe overflow disabled and the publisher uses the
    /// `DiscardData` backpressure strategy, so `send()` and `send_copy()`
    /// never block on a subscriber and stay O(1).
    ///
    /// @param node Node that owns this process's transport membership; must
    ///        outlive the publisher.
    /// @param name Valid service name; a subscriber must use the same name to
    ///        connect.
    /// @return The constructed publisher, or a mapped `com::Error`
    ///         (`CreationFailed`, `ConnectionFailure`, ...) when the service
    ///         or publisher cannot be created.
    /// @note `com/publish-subscribe`, "Non-blocking overflow" and
    ///       "Configurable subscriber buffer depth". Both endpoints must
    ///       agree on the overflow contract; iceoryx2 verifies it on connect.
    [[nodiscard]] static Result<Publisher> create(Node& node, const ServiceName& name) noexcept {
        return create(node, name, 0U);
    }

    /// @brief Open or create the service with an explicit subscriber buffer depth.
    ///
    /// A service may already exist (daemon-less discovery): the builder opens
    /// it when present and creates it otherwise. The publisher always selects
    /// `DiscardData`, independently of the buffer depth.
    ///
    /// @param node Node that owns this process's transport membership; must
    ///        outlive the publisher.
    /// @param name Valid service name shared with the subscriber.
    /// @param subscriber_max_buffer_size Maximum number of samples one
    ///        subscriber may hold; `0` keeps the transport default (2). A
    ///        larger value lets a burst of N frames reach a subscriber
    ///        without deterministic discards.
    /// @return The constructed publisher, or a mapped `com::Error` on
    ///         failure.
    /// @note `com/publish-subscribe`, "Configurable subscriber buffer depth"
    ///       and "Compile-time sizing and pool allocation".
    [[nodiscard]] static Result<Publisher>
    create(Node& node, const ServiceName& name, std::uint64_t subscriber_max_buffer_size) noexcept {
        auto builder = node.handle().service_builder(name.handle()).publish_subscribe<T>();
        if (subscriber_max_buffer_size > 0U) {
            static_cast<void>(
                std::move(builder).subscriber_max_buffer_size(subscriber_max_buffer_size));
        }
        auto service = [&]() {
            if constexpr (std::is_void_v<H>) {
                return std::move(builder).enable_safe_overflow(false).open_or_create();
            } else {
                return std::move(builder)
                    .template user_header<H>()
                    .enable_safe_overflow(false)
                    .open_or_create();
            }
        }();
        if (!service) {
            return std::unexpected(detail::map_error(service.error()));
        }
        auto publisher = service->publisher_builder()
                             .backpressure_strategy(iox2::BackpressureStrategy::DiscardData)
                             .create();
        if (!publisher) {
            return std::unexpected(detail::map_error(publisher.error()));
        }
        return Publisher{name, std::move(*publisher)};
    }

    /// @brief Copying is disabled: the publisher owns the transport handle.
    Publisher(const Publisher&) = delete;

    /// @brief Copy assignment is disabled: the publisher owns the transport
    ///        handle.
    Publisher& operator=(const Publisher&) = delete;

    /// @brief Move-construct a publisher, transferring the transport handle.
    ///
    /// `std::atomic` is not movable, so the telemetry counters are copied
    /// from `other` instead of moved; the transport handle itself is moved.
    ///
    /// @param other Source publisher. After the move it no longer owns a
    ///        transport handle and must not be used for publication.
    /// @note The service name is moved as well; diagnostics on the moved-from
    ///       object are unspecified.
    Publisher(Publisher&& other) noexcept
        : service_name_(std::move(other.service_name_)), inner_(std::move(other.inner_)) {
        telemetry_.queued_samples_.store(
            other.telemetry_.queued_samples_.load(std::memory_order_relaxed),
            std::memory_order_relaxed);
        telemetry_.lost_samples_.store(
            other.telemetry_.lost_samples_.load(std::memory_order_relaxed),
            std::memory_order_relaxed);
        telemetry_.send_count_.store(other.telemetry_.send_count_.load(std::memory_order_relaxed),
                                     std::memory_order_relaxed);
        telemetry_.peak_recipients_.store(
            other.telemetry_.peak_recipients_.load(std::memory_order_relaxed),
            std::memory_order_relaxed);
    }

    /// @brief Move-assign a publisher, transferring the transport handle.
    ///
    /// The destination's current transport handle is released and its
    /// telemetry counters are replaced by `other`'s. Self-assignment is a
    /// no-op.
    ///
    /// @param other Source publisher; left without a transport handle.
    /// @return `*this`.
    Publisher& operator=(Publisher&& other) noexcept {
        if (this != &other) {
            service_name_ = std::move(other.service_name_);
            inner_ = std::move(other.inner_);
            telemetry_.queued_samples_.store(
                other.telemetry_.queued_samples_.load(std::memory_order_relaxed),
                std::memory_order_relaxed);
            telemetry_.lost_samples_.store(
                other.telemetry_.lost_samples_.load(std::memory_order_relaxed),
                std::memory_order_relaxed);
            telemetry_.send_count_.store(
                other.telemetry_.send_count_.load(std::memory_order_relaxed),
                std::memory_order_relaxed);
            telemetry_.peak_recipients_.store(
                other.telemetry_.peak_recipients_.load(std::memory_order_relaxed),
                std::memory_order_relaxed);
        }
        return *this;
    }

    /// @brief Borrow an uninitialized slot from the shared-memory pool.
    ///
    /// The returned `SampleMutUninit` constructs the payload in place with
    /// `write_payload()`; no serialization or intermediate buffer is involved.
    /// Dropping the returned value without sending returns the slot to the
    /// pool.
    ///
    /// @return The borrowed slot, or a mapped `com::Error` (`OutOfSamples`,
    ///         `CreationFailed`, ...) when the pool cannot lend a chunk.
    /// @note `com/publish-subscribe`, "Zero-copy publication". The slot is
    ///       owned by the calling thread; it must not be shared between
    ///       threads. Publication never blocks, so a failed loan is reported
    ///       rather than retried.
    [[nodiscard]] Result<SampleMutUninit<T, H>> loan_uninit() noexcept {
        auto loaned = inner_.loan_uninit();
        if (!loaned) {
            return std::unexpected(detail::map_error(loaned.error()));
        }
        return SampleMutUninit<T, H>{std::move(*loaned)};
    }

    /// @brief Hand an initialized sample over to every subscriber.
    ///
    /// The transport reports how many subscribers received the sample; that
    /// count updates the lag telemetry read by `lost_samples()`. Delivery is
    /// non-blocking: when a subscriber's queue is full, `DiscardData` evicts
    /// that subscriber's oldest unread sample and the call still succeeds.
    ///
    /// @param sample Initialized sample produced by `loan_uninit()` followed
    ///        by `SampleMutUninit::write_payload()`; ownership is transferred
    ///        to the transport and the parameter must not be reused.
    /// @return Empty `Status` on success, or a mapped `com::Error`
    ///         (`UnableToDeliver`, `ConnectionFailure`, ...) when the sample
    ///         could not be handed over.
    /// @note `com/publish-subscribe`, "Zero-copy publication",
    ///       "Non-blocking overflow", and "Subscriber lag telemetry".
    [[nodiscard]] Status send(SampleMut<T, H> sample) noexcept {
        auto sent = iox2::send(std::move(sample.handle()));
        if (!sent) {
            return std::unexpected(detail::map_error(sent.error()));
        }
        record_delivery(*sent);
        return {};
    }

    /// @brief Publish a caller-owned payload in one transport call.
    ///
    /// Equivalent to `loan_uninit()` + `write_payload(payload)` + `send()`:
    /// the transport loans a slot, copies `payload` into shared memory once,
    /// and sends. Prefer it for worker-owned frames that are already fully
    /// formed; use the loan/send pair when the payload must be written in
    /// place or inspected before sending.
    ///
    /// @param payload Fully formed payload; copied into shared memory exactly
    ///        once and never retained by the publisher.
    /// @return Empty `Status` on success, or a mapped `com::Error`
    ///         (`OutOfSamples`, `UnableToDeliver`, ...) when the transport
    ///         cannot loan a slot or hand the sample over.
    /// @note `com/publish-subscribe`, "Single-call publication of a
    ///       caller-owned payload": non-blocking, allocation-free, and updates
    ///       the same published/lost telemetry as `send()`.
    [[nodiscard]] Status send_copy(const T& payload) noexcept {
        auto sent = inner_.send_copy(payload);
        if (!sent) {
            return std::unexpected(detail::map_error(sent.error()));
        }
        record_delivery(*sent);
        return {};
    }

    /// @brief Cumulative number of samples evicted by the discard policy.
    ///
    /// Reads the publisher's relaxed telemetry counter. A caller compares
    /// snapshots taken before and after one publication to learn that a
    /// receiver's queue was full; the publication itself still succeeds, so
    /// this value is diagnostic only and must never drive a control-flow
    /// decision.
    ///
    /// @return Monotonic total of evicted samples since construction.
    /// @note `com/publish-subscribe`, "Subscriber lag telemetry". The read is
    ///       `std::memory_order_relaxed`; ordering across threads is not
    ///       guaranteed beyond the counter's own monotonicity.
    [[nodiscard]] std::uint64_t lost_samples() const noexcept {
        return telemetry_.lost_samples_.load(std::memory_order_relaxed);
    }

    /// @brief Render the cold-path diagnostic string.
    ///
    /// @return `Publisher(service=<name>, sent=<n>, queued=<n>, lost=<n>)`,
    ///         where `sent` counts successful publications, `queued` counts
    ///         observed full-queue events, and `lost` counts evicted samples.
    /// @note Mandate 9 (universal `to_string`); `com/publish-subscribe`,
    ///       "Port diagnostics". Allocates a `std::string`, so it must not be
    ///       called on a hot path.
    [[nodiscard]] std::string to_string() const {
        return std::format("Publisher(service={}, sent={}, queued={}, lost={})",
                           service_name_.to_string(),
                           telemetry_.send_count_.load(std::memory_order_relaxed),
                           telemetry_.queued_samples_.load(std::memory_order_relaxed),
                           telemetry_.lost_samples_.load(std::memory_order_relaxed));
    }

private:
    /// @brief Update the publication and eviction telemetry after a delivery.
    ///
    /// Shared by `send()` and `send_copy()` so both publication styles report
    /// identical counters. When fewer subscribers received the sample than the
    /// observed peak, the difference is attributed to the `DiscardData`
    /// policy: the lost counter advances by that difference and one fill event
    /// is recorded.
    ///
    /// @param delivered Number of subscribers the transport delivered the
    ///        sample to.
    /// @note `com/publish-subscribe`, "Subscriber lag telemetry". All accesses
    ///       are `std::memory_order_relaxed` because the counters are
    ///       diagnostics with a single publishing writer.
    void record_delivery(std::uint64_t delivered) noexcept {
        telemetry_.send_count_.fetch_add(1, std::memory_order_relaxed);
        const std::uint64_t peak = telemetry_.peak_recipients_.load(std::memory_order_relaxed);
        if (delivered < peak) {
            // Some receiver's queue was full: those samples are discarded by
            // the DiscardData policy — count them as lost and record one fill
            // event.
            telemetry_.lost_samples_.fetch_add(peak - delivered, std::memory_order_relaxed);
            telemetry_.queued_samples_.fetch_add(1, std::memory_order_relaxed);
        } else if (delivered > peak) {
            telemetry_.peak_recipients_.store(delivered, std::memory_order_relaxed);
        }
    }

    /// @brief Adopt an already-created transport publisher.
    ///
    /// @param name Service name retained for `to_string()`.
    /// @param inner Transport publisher returned by the iceoryx2 builder.
    /// @note Private by design: the only supported construction path is
    ///       `create()`, which configures the service and the discard policy.
    Publisher(ServiceName name, iox2::Publisher<iox2::ServiceType::Ipc, T, H> inner) noexcept
        : service_name_(std::move(name)), inner_(std::move(inner)) {}

    /// @brief Per-publisher publication and lag counters.
    ///
    /// Cache-line separated (`alignas(64)`) from the payload pool to avoid
    /// false sharing with the transport. Values are written by the publishing
    /// thread only and read with `std::memory_order_relaxed`; they are
    /// diagnostics, never control-flow inputs.
    ///
    /// @note `com/publish-subscribe`, "Subscriber lag telemetry".
    struct alignas(64) Telemetry {
        /// @brief Full-queue events: one per send that observed a full
        ///        receiver queue.
        std::atomic<std::uint64_t> queued_samples_{0};
        /// @brief Samples evicted by the `DiscardData` policy.
        std::atomic<std::uint64_t> lost_samples_{0};
        /// @brief Successful publications (`send()` and `send_copy()`).
        std::atomic<std::uint64_t> send_count_{0};
        /// @brief Highest delivered-subscriber count observed; the baseline
        ///        for deriving evictions.
        std::atomic<std::uint64_t> peak_recipients_{0};
    };

    ServiceName service_name_; ///< Service identity rendered by `to_string()`.
    iox2::Publisher<iox2::ServiceType::Ipc, T, H> inner_; ///< Owned transport handle.
    Telemetry telemetry_{};                               ///< Publication and lag counters.
};

/// @brief Free helper mirroring `Publisher::to_string()`.
///
/// @tparam T Payload type satisfying `com::concepts::Message`.
/// @tparam H Optional user-header type; `void` means no header.
/// @param publisher Publisher to render.
/// @return The publisher's diagnostic string.
/// @note Mandate 9 (universal `to_string`); allocates, so cold path only.
template <concepts::Message T, typename H = void>
[[nodiscard]] inline std::string to_string(const Publisher<T, H>& publisher) {
    return publisher.to_string();
}

} // namespace nexigen::com

/// @brief Formats a `Publisher` as its diagnostic string.
///
/// @tparam T Payload type satisfying `com::concepts::Message`.
/// @tparam H Optional user-header type; `void` means no header.
/// @note Mandate 11 (universal `std::formatter`); delegates to
///       `Publisher::to_string()`, which allocates, so cold path only.
template <nexigen::com::concepts::Message T, typename H>
struct std::formatter<nexigen::com::Publisher<T, H>> : std::formatter<std::string_view> {
    /// @brief Write the publisher's diagnostic text into the format context.
    ///
    /// @tparam Ctx `std::format` context type.
    /// @param publisher Publisher to format.
    /// @param ctx Output context supplied by `std::format`.
    /// @return Iterator one past the last written character.
    template <class Ctx>
    auto format(const nexigen::com::Publisher<T, H>& publisher, Ctx& ctx) const {
        const std::string text = publisher.to_string();
        return std::formatter<std::string_view>::format(text, ctx);
    }
};
