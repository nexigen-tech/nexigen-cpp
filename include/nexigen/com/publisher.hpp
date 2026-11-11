// nexigen::com — zero-copy publisher (change add-com-module;
// spec com/publish-subscribe "Zero-copy publication" / "Port diagnostics").
//
// Thin wrapper over iox2::Publisher: loan_uninit() borrows a shared-memory
// slot, write_payload() constructs in place, send() hands the slot over.
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

/// Zero-copy publisher of payload type T for one service (optional custom
/// header H, default void = no header).
template <concepts::Message T, typename H = void> class Publisher {
    static_assert(std::is_void_v<H> || concepts::Message<H>,
                  "the custom header must satisfy com::concepts::Message");

public:
    /// Open (or create) the service on the node and create the publisher.
    /// Delivery policy is DiscardData (design D6/REQ_COM_007): safe overflow
    /// is switched off on the service, so a sample that cannot be delivered
    /// to an overflowed subscriber is dropped — send() never blocks on any
    /// subscriber and stays O(1) allocation-free. Both sides of the service
    /// must carry the same overflow contract (iceoryx2 verifies it on
    /// connect).
    [[nodiscard]] static Result<Publisher> create(Node& node, const ServiceName& name) noexcept {
        return create(node, name, 0U);
    }

    /// Open (or create) the service with an explicit `subscriber_max_buffer_size`
    /// (0 leaves the service default of 2). A larger value lets one publish
    /// burst of N frames reach a subscriber without deterministic discards.
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

    Publisher(const Publisher&) = delete;
    Publisher& operator=(const Publisher&) = delete;

    /// Move transfers the transport handle; the atomics are re-initialized
    /// from the source counters (std::atomic is not movable).
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

    /// Borrow a slot from the shared pool (zero-copy loan).
    [[nodiscard]] Result<SampleMutUninit<T, H>> loan_uninit() noexcept {
        auto loaned = inner_.loan_uninit();
        if (!loaned) {
            return std::unexpected(detail::map_error(loaned.error()));
        }
        return SampleMutUninit<T, H>{std::move(*loaned)};
    }

    /// Hand the initialized sample over to every subscriber (non-blocking).
    /// The return value of the transport is the number of subscribers the
    /// sample was delivered to; the lag telemetry below derives the evicted-
    /// sample count from it.
    [[nodiscard]] Status send(SampleMut<T, H> sample) noexcept {
        auto sent = iox2::send(std::move(sample.handle()));
        if (!sent) {
            return std::unexpected(detail::map_error(sent.error()));
        }
        const std::uint64_t delivered = *sent;
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
        return {};
    }

    /// Cold-path diagnostic (mandate 9): the service name and the telemetry.
    [[nodiscard]] std::string to_string() const {
        return std::format("Publisher(service={}, sent={}, queued={}, lost={})",
                           service_name_.to_string(),
                           telemetry_.send_count_.load(std::memory_order_relaxed),
                           telemetry_.queued_samples_.load(std::memory_order_relaxed),
                           telemetry_.lost_samples_.load(std::memory_order_relaxed));
    }

private:
    Publisher(ServiceName name, iox2::Publisher<iox2::ServiceType::Ipc, T, H> inner) noexcept
        : service_name_(std::move(name)), inner_(std::move(inner)) {}

    /// Per-publisher telemetry (design D7): relaxed atomics touched only on
    /// the send path (one writer); the cell is cache-line separated from the
    /// payload pool. queued_samples_ counts observed fill events (a receiver's
    /// queue was full), lost_samples_ counts evicted samples (DiscardData),
    /// send_count_ counts successful sends, peak_recipients_ is the transport
    /// bookkeeping needed to derive the eviction count.
    struct alignas(64) Telemetry {
        std::atomic<std::uint64_t> queued_samples_{0};
        std::atomic<std::uint64_t> lost_samples_{0};
        std::atomic<std::uint64_t> send_count_{0};
        std::atomic<std::uint64_t> peak_recipients_{0};
    };

    ServiceName service_name_;
    iox2::Publisher<iox2::ServiceType::Ipc, T, H> inner_;
    Telemetry telemetry_{};
};

/// Free helper (mandate 9): mirrors the member.
template <concepts::Message T, typename H = void>
[[nodiscard]] inline std::string to_string(const Publisher<T, H>& publisher) {
    return publisher.to_string();
}

} // namespace nexigen::com

/// The publisher formats as its diagnostic string (mandate 11).
template <nexigen::com::concepts::Message T, typename H>
struct std::formatter<nexigen::com::Publisher<T, H>> : std::formatter<std::string_view> {
    template <class Ctx>
    auto format(const nexigen::com::Publisher<T, H>& publisher, Ctx& ctx) const {
        const std::string text = publisher.to_string();
        return std::formatter<std::string_view>::format(text, ctx);
    }
};
