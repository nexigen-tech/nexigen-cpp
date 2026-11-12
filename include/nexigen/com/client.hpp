// nexigen::com — the Method (request-response) client (change add-com-module;
// spec com/request-response "Typed client and server" / "Synchronous call with
// timeout").
//
// Thin wrapper over iox2::Client: send() issues an asynchronous request and
// returns a PendingResponse tagged with a fresh correlation id; call() is the
// synchronous form — it blocks the calling thread until the response arrives
// or the caller-supplied timeout elapses (Error::Timeout), and abandons the
// request so no response is delivered afterwards.
#pragma once

#include <atomic>
#include <chrono>
#include <cstdint>
#include <format>
#include <memory>
#include <string>
#include <thread>
#include <utility>

#include <iox2/iceoryx2.hpp>

#include <nexigen/com/concepts/message_concept.hpp>
#include <nexigen/com/detail/error_mapping.hpp>
#include <nexigen/com/error.hpp>
#include <nexigen/com/node.hpp>
#include <nexigen/com/pending_response.hpp>
#include <nexigen/com/service_name.hpp>

namespace nexigen::com {

/// @brief Client side of one request-response service.
///
/// The requesting endpoint of the `com` Method pattern. It wraps
/// `iox2::Client` so application code never names an `iox2::` type (spec
/// `com/service-management`, "Protocol-independent boundary"). Two request
/// styles are available: `send()` issues an asynchronous request and returns a
/// `PendingResponse` handle the caller polls later, while `call()` blocks the
/// calling thread until the answer arrives or the timeout elapses.
///
/// Every request carries a client-generated correlation id that the response
/// repeats, so outstanding asynchronous replies pair unambiguously. Requests
/// and responses travel zero-copy through shared memory.
///
/// The client owns the transport handle, so copying is disabled and moving
/// transfers ownership. `send()` and `call()` must come from one thread; the
/// instance is not internally synchronized.
///
/// @tparam Req Request payload type; constrained by `com::concepts::Message`,
///         i.e. `std::is_trivially_copyable_v<Req>`.
/// @tparam Resp Response payload type; constrained by `com::concepts::Message`,
///         i.e. `std::is_trivially_copyable_v<Resp>`.
/// @note Implements `com/request-response`: "Typed client and server",
///       "Correlation of requests", "Synchronous call with timeout",
///       "Asynchronous pending responses", and "Method port diagnostics".
template <concepts::Message Req, concepts::Message Resp> class Client {
public:
    /// @brief Open or create the service on `node` and create the client.
    ///
    /// Daemon-less discovery: the builder opens the service when it already
    /// exists and creates it otherwise, so the client and server may start in
    /// either order.
    ///
    /// @param node Node that owns this process's transport membership; must
    ///        outlive the client.
    /// @param name Valid service name shared with the server.
    /// @return The constructed client, or a mapped `com::Error`
    ///         (`ConnectionFailure`, `IncompatibleTypeLayout`, ...) when the
    ///         service or client cannot be created.
    /// @note Spec `com/request-response`, requirement "Typed client and
    ///       server"; spec `com/service-management`, requirement "Node and
    ///       daemon-less discovery".
    [[nodiscard]] static Result<Client> create(Node& node, const ServiceName& name) noexcept {
        auto service = node.handle()
                           .service_builder(name.handle())
                           .request_response<Req, Resp>()
                           .open_or_create();
        if (!service) {
            return std::unexpected(detail::map_error(service.error()));
        }
        auto client = service->client_builder().create();
        if (!client) {
            return std::unexpected(detail::map_error(client.error()));
        }
        return Client{name, std::move(*client)};
    }

    /// @brief Copying is disabled: the client owns the transport handle.
    Client(const Client&) = delete;

    /// @brief Copy assignment is disabled: the client owns the transport
    ///        handle.
    Client& operator=(const Client&) = delete;

    /// @brief Move-construct a client, transferring the transport handle.
    ///
    /// Outstanding handles keep the shared telemetry alive.
    Client(Client&&) noexcept = default;

    /// @brief Move-assign a client, transferring the transport handle.
    ///
    /// The destination's previous handle is released; self-assignment is a
    /// no-op.
    /// @return `*this`.
    Client& operator=(Client&&) noexcept = default;

    /// @brief Send an asynchronous request.
    ///
    /// The payload is copied into shared memory once; the returned handle
    /// carries a fresh correlation id and harvests exactly this request's
    /// responses.
    ///
    /// @param request Request payload to send; copied into the shared-memory
    ///        slot and not retained by the client.
    /// @return The pending handle, or a mapped `com::Error` (`OutOfSamples`,
    ///         `UnableToDeliver`, ...) when the request cannot be sent.
    /// @note Spec `com/request-response`, requirements "Correlation of
    ///       requests" and "Asynchronous pending responses". Non-blocking and
    ///       allocation-free on the send path.
    [[nodiscard]] Result<PendingResponse<Req, Resp>> send(const Req& request) noexcept {
        auto pending = inner_.send_copy(request);
        if (!pending) {
            return std::unexpected(detail::map_error(pending.error()));
        }
        const std::uint64_t correlation_id =
            telemetry_->correlation_.fetch_add(1, std::memory_order_relaxed);
        telemetry_->outstanding_.fetch_add(1, std::memory_order_relaxed);
        return PendingResponse<Req, Resp>{std::move(*pending), correlation_id, telemetry_};
    }

    /// @brief Block until the response arrives or the timeout elapses.
    ///
    /// Internally sends the request and polls its pending handle in short
    /// steps until the answer lands or the caller-supplied deadline passes. On
    /// timeout the request is abandoned, so no response is delivered
    /// afterwards; the caller is free to ignore the failure or retry.
    ///
    /// @param request Request payload to send.
    /// @param timeout Maximum time to wait for the response, measured from the
    ///        moment the request was sent.
    /// @return The server's response, `com::Error::Timeout` when the deadline
    ///         passed first, or another mapped `com::Error` when the request
    ///         cannot be sent or received.
    /// @note Spec `com/request-response`, requirement "Synchronous call with
    ///       timeout". Blocking, and the caller must not use it from a handler
    ///       thread that is also expected to run the server.
    [[nodiscard]] Result<Resp> call(const Req& request,
                                    std::chrono::milliseconds timeout) noexcept {
        auto pending = send(request);
        if (!pending) {
            return std::unexpected(pending.error());
        }
        const auto deadline = std::chrono::steady_clock::now() + timeout;
        while (true) {
            auto response = pending->receive();
            if (!response) {
                return std::unexpected(response.error());
            }
            if (response->has_value()) {
                return **response;
            }
            if (std::chrono::steady_clock::now() >= deadline) {
                return std::unexpected(Error::Timeout);
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
    }

    /// @brief Number of requests sent and not yet completed.
    ///
    /// A request is complete when its response was harvested or when its
    /// `PendingResponse` handle was dropped.
    ///
    /// @return Relaxed snapshot of the outstanding-request counter.
    [[nodiscard]] std::uint64_t pending_count() const noexcept {
        return telemetry_->outstanding_.load(std::memory_order_relaxed);
    }

    /// @brief Render the cold-path diagnostic string.
    ///
    /// @return `Client(service=<name>, pending=<n>)`.
    /// @note Mandate 9 (universal `to_string`); `com/request-response`,
    ///       requirement "Method port diagnostics". Allocates, so cold path
    ///       only.
    [[nodiscard]] std::string to_string() const {
        return std::format("Client(service={}, pending={})", service_name_.to_string(),
                           pending_count());
    }

private:
    /// @brief Adopt an already-created transport client.
    ///
    /// @param name Service name retained for `to_string()`.
    /// @param inner Transport client returned by the iceoryx2 builder.
    /// @note Private by design: the only supported construction path is
    ///       `create()`, which also allocates the shared telemetry once.
    Client(ServiceName name,
           iox2::Client<iox2::ServiceType::Ipc, Req, void, Resp, void> inner) noexcept
        : service_name_(std::move(name)), inner_(std::move(inner)),
          telemetry_(std::make_shared<detail::ClientTelemetry>()) {}

    ServiceName service_name_; ///< Service identity rendered by `to_string()`.
    iox2::Client<iox2::ServiceType::Ipc, Req, void, Resp, void> inner_; ///< Owned transport handle.
    std::shared_ptr<detail::ClientTelemetry> telemetry_; ///< Shared with outstanding handles.
};

/// @brief Free helper mirroring `Client::to_string()`.
///
/// @tparam Req Request payload type satisfying `com::concepts::Message`.
/// @tparam Resp Response payload type satisfying `com::concepts::Message`.
/// @param client Client to render.
/// @return The client's diagnostic string.
/// @note Mandate 9 (universal `to_string`); allocates, so cold path only.
template <concepts::Message Req, concepts::Message Resp>
[[nodiscard]] inline std::string to_string(const Client<Req, Resp>& client) {
    return client.to_string();
}

} // namespace nexigen::com

/// @brief Formats a `Client` as its diagnostic string.
///
/// @tparam Req Request payload type satisfying `com::concepts::Message`.
/// @tparam Resp Response payload type satisfying `com::concepts::Message`.
/// @note Mandate 11 (universal `std::formatter`); delegates to
///       `Client::to_string()`, which allocates, so cold path only.
template <nexigen::com::concepts::Message Req, nexigen::com::concepts::Message Resp>
struct std::formatter<nexigen::com::Client<Req, Resp>> : std::formatter<std::string_view> {
    /// @brief Write the client's diagnostic text into the format context.
    ///
    /// @tparam Ctx `std::format` context type.
    /// @param client Client to format.
    /// @param ctx Output context supplied by `std::format`.
    /// @return Iterator one past the last written character.
    template <class Ctx>
    auto format(const nexigen::com::Client<Req, Resp>& client, Ctx& ctx) const {
        const std::string text = client.to_string();
        return std::formatter<std::string_view>::format(text, ctx);
    }
};
