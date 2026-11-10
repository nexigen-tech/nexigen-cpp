// nexigen::com — pending response handle (change add-com-module;
// spec com/request-response "Asynchronous pending responses").
//
// The handle returned by Client::send(): it carries the client-generated
// correlation id of its request, and receive() harvests the responses that the
// server sent for exactly that request. The transport matches responses to
// their request per connection, so every response observed through this
// handle IS the response to the request the handle was created for — the
// caller pairs asynchronous replies by correlation id without ambiguity.
#pragma once

#include <atomic>
#include <cstdint>
#include <format>
#include <memory>
#include <optional>
#include <string>
#include <utility>

#include <iox2/iceoryx2.hpp>

#include <nexigen/com/concepts/message_concept.hpp>
#include <nexigen/com/detail/error_mapping.hpp>
#include <nexigen/com/error.hpp>

namespace nexigen::com {

namespace detail {

/// Shared by a client and its outstanding pending handles: the pending
/// counter is decremented when a PendingResponse is dropped, and the
/// correlation counter hands out fresh ids. One allocation at client
/// creation; the send path performs relaxed atomic stores only.
struct ClientTelemetry {
    std::atomic<std::uint64_t> outstanding_{0};
    std::atomic<std::uint64_t> correlation_{0};
};

} // namespace detail

template <concepts::Message Req, concepts::Message Resp> class Client;

/// Outstanding asynchronous request: poll receive() or drop the handle.
template <concepts::Message Req, concepts::Message Resp>
class PendingResponse {
public:
    /// Dropping the handle abandons the outstanding request (the transport
    /// recycles the request slot; late server answers are discarded). The
    /// client's pending counter is decremented.
    ~PendingResponse() noexcept {
        if (telemetry_) {
            telemetry_->outstanding_.fetch_sub(1, std::memory_order_relaxed);
        }
    }

    PendingResponse(const PendingResponse&) = delete;
    PendingResponse& operator=(const PendingResponse&) = delete;
    PendingResponse(PendingResponse&&) noexcept = default;
    PendingResponse& operator=(PendingResponse&&) noexcept = default;

    /// The unique id the client generated for the request this handle awaits.
    [[nodiscard]] std::uint64_t correlation_id() const noexcept { return correlation_id_; }

    /// Harvest one response; ok with no value when the server has not
    /// answered yet (the caller polls — no blocking).
    [[nodiscard]] Result<std::optional<Resp>> receive() noexcept {
        auto received = inner_.receive();
        if (!received) {
            return std::unexpected(detail::map_error(received.error()));
        }
        if (!received->has_value()) {
            return std::nullopt;
        }
        return (*received)->payload();
    }

    /// Cold-path diagnostic (mandate 9).
    [[nodiscard]] std::string to_string() const {
        return std::format("PendingResponse(correlation_id={})", correlation_id_);
    }

private:
    friend class Client<Req, Resp>;

    PendingResponse(iox2::PendingResponse<iox2::ServiceType::Ipc, Req, void, Resp, void> inner,
                    std::uint64_t correlation_id,
                    std::shared_ptr<detail::ClientTelemetry> telemetry) noexcept
        : inner_(std::move(inner)), correlation_id_(correlation_id), telemetry_(std::move(telemetry)) {}

    iox2::PendingResponse<iox2::ServiceType::Ipc, Req, void, Resp, void> inner_;
    std::uint64_t correlation_id_{0};
    std::shared_ptr<detail::ClientTelemetry> telemetry_;
};

/// Free helper (mandate 9): mirrors the member.
template <concepts::Message Req, concepts::Message Resp>
[[nodiscard]] inline std::string to_string(const PendingResponse<Req, Resp>& pending) {
    return pending.to_string();
}

} // namespace nexigen::com

/// The pending response formats as its diagnostic string (mandate 11).
template <nexigen::com::concepts::Message Req, nexigen::com::concepts::Message Resp>
struct std::formatter<nexigen::com::PendingResponse<Req, Resp>> : std::formatter<std::string_view> {
    template <class Ctx>
    auto format(const nexigen::com::PendingResponse<Req, Resp>& pending, Ctx& ctx) const {
        const std::string text = pending.to_string();
        return std::formatter<std::string_view>::format(text, ctx);
    }
};
