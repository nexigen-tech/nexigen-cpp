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

/// @brief Shared counter block of a client and its outstanding handles.
///
/// The pending counter is incremented by `Client::send()` and decremented when
/// a `PendingResponse` is dropped; the correlation counter hands out fresh
/// request ids. One allocation at client creation, after which the send and
/// destroy paths perform relaxed atomic updates only.
///
/// @note Spec `com/request-response`, requirements "Correlation of requests"
///       and "Asynchronous pending responses".
struct ClientTelemetry {
    std::atomic<std::uint64_t> outstanding_{0}; ///< Sent, not yet completed requests.
    std::atomic<std::uint64_t> correlation_{0}; ///< Source of unique correlation ids.
};

} // namespace detail

/// @brief Forward declaration of the client that produces pending handles.
template <concepts::Message Req, concepts::Message Resp> class Client;

/// @brief Outstanding asynchronous request handle.
///
/// Returned by `Client::send()`. It carries the client-generated correlation
/// id of its request, and `receive()` harvests the responses the server sent
/// for exactly that request: the transport matches responses to their request
/// per connection, so every response observed here belongs to this handle. The
/// caller polls `receive()` or drops the handle; it is never required to block.
///
/// The handle is move-only. Dropping it abandons the request, recycles the
/// request slot, discards any late server answer, and decrements the client's
/// pending counter.
///
/// @tparam Req Request payload type; constrained by `com::concepts::Message`.
/// @tparam Resp Response payload type; constrained by `com::concepts::Message`.
/// @note Spec `com/request-response`, requirement "Asynchronous pending
///       responses".
template <concepts::Message Req, concepts::Message Resp> class PendingResponse {
public:
    /// @brief Abandon the outstanding request and release its slot.
    ///
    /// The transport recycles the request slot, late server answers are
    /// discarded, and the client's pending counter is decremented.
    ///
    /// @note Spec `com/request-response`, requirement "Asynchronous pending
    ///       responses".
    ~PendingResponse() noexcept {
        if (telemetry_) {
            telemetry_->outstanding_.fetch_sub(1, std::memory_order_relaxed);
        }
    }

    /// @brief Copying is disabled: one handle owns one outstanding request.
    PendingResponse(const PendingResponse&) = delete;

    /// @brief Copy assignment is disabled: one handle owns one request.
    PendingResponse& operator=(const PendingResponse&) = delete;

    /// @brief Move-construct a handle, transferring the outstanding request.
    ///
    /// The moved-from handle no longer decrements the pending counter.
    PendingResponse(PendingResponse&&) noexcept = default;

    /// @brief Move-assign a handle, transferring the outstanding request.
    ///
    /// The destination's previous request is abandoned first.
    /// @return `*this`.
    PendingResponse& operator=(PendingResponse&&) noexcept = default;

    /// @brief The unique id the client generated for this request.
    ///
    /// @return The correlation id that pairs this handle with its response.
    /// @note Spec `com/request-response`, requirement "Correlation of
    ///       requests": one call to `Client::send()` advances the id, so two
    ///       outstanding handles never share one.
    [[nodiscard]] std::uint64_t correlation_id() const noexcept { return correlation_id_; }

    /// @brief Harvest one response without blocking.
    ///
    /// @return The response when the server has answered, an engaged result
    ///         holding an empty optional when it has not answered yet, or a
    ///         mapped `com::Error` when the transport cannot receive.
    /// @note Spec `com/request-response`, requirement "Asynchronous pending
    ///       responses": the caller polls, so the call never blocks the
    ///       thread. Single consumer: call from one thread.
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

    /// @brief Render the cold-path diagnostic string.
    ///
    /// @return `PendingResponse(correlation_id=<n>)`.
    /// @note Mandate 9 (universal `to_string`); `com/request-response`,
    ///       requirement "Method port diagnostics". Allocates, so cold path
    ///       only.
    [[nodiscard]] std::string to_string() const {
        return std::format("PendingResponse(correlation_id={})", correlation_id_);
    }

private:
    friend class Client<Req, Resp>;

    /// @brief Adopt a transport pending response from `Client::send()`.
    ///
    /// @param inner Transport pending response returned by the client.
    /// @param correlation_id Id generated for this request.
    /// @param telemetry Counter block shared with the producing client.
    /// @note Private by design: only `Client::send()` produces handles.
    PendingResponse(iox2::PendingResponse<iox2::ServiceType::Ipc, Req, void, Resp, void> inner,
                    std::uint64_t correlation_id,
                    std::shared_ptr<detail::ClientTelemetry> telemetry) noexcept
        : inner_(std::move(inner)), correlation_id_(correlation_id),
          telemetry_(std::move(telemetry)) {}

    iox2::PendingResponse<iox2::ServiceType::Ipc, Req, void, Resp, void> inner_; ///< Owned handle.
    std::uint64_t correlation_id_{0};                    ///< Client-generated request id.
    std::shared_ptr<detail::ClientTelemetry> telemetry_; ///< Counters shared with the client.
};

/// @brief Free helper mirroring `PendingResponse::to_string()`.
///
/// @tparam Req Request payload type satisfying `com::concepts::Message`.
/// @tparam Resp Response payload type satisfying `com::concepts::Message`.
/// @param pending Handle to render.
/// @return The handle's diagnostic string.
/// @note Mandate 9 (universal `to_string`); allocates, so cold path only.
template <concepts::Message Req, concepts::Message Resp>
[[nodiscard]] inline std::string to_string(const PendingResponse<Req, Resp>& pending) {
    return pending.to_string();
}

} // namespace nexigen::com

/// @brief Formats a `PendingResponse` as its diagnostic string.
///
/// @tparam Req Request payload type satisfying `com::concepts::Message`.
/// @tparam Resp Response payload type satisfying `com::concepts::Message`.
/// @note Mandate 11 (universal `std::formatter`); delegates to
///       `PendingResponse::to_string()`, which allocates, so cold path only.
template <nexigen::com::concepts::Message Req, nexigen::com::concepts::Message Resp>
struct std::formatter<nexigen::com::PendingResponse<Req, Resp>> : std::formatter<std::string_view> {
    /// @brief Write the handle's diagnostic text into the format context.
    ///
    /// @tparam Ctx `std::format` context type.
    /// @param pending Handle to format.
    /// @param ctx Output context supplied by `std::format`.
    /// @return Iterator one past the last written character.
    template <class Ctx>
    auto format(const nexigen::com::PendingResponse<Req, Resp>& pending, Ctx& ctx) const {
        const std::string text = pending.to_string();
        return std::formatter<std::string_view>::format(text, ctx);
    }
};
