// nexigen::com — the Method (request-response) server (change add-com-module;
// spec com/request-response "Typed client and server").
//
// Thin wrapper over iox2::Server: receive() harvests the next request as a
// zero-copy ServerRequest view (the exact bytes the client wrote — no
// serialization step); the request handle routes the answer back to exactly
// the client that issued it.
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
#include <nexigen/com/node.hpp>
#include <nexigen/com/service_name.hpp>

namespace nexigen::com {

/// @brief Forward declaration of the server that produces request handles.
template <concepts::Message Req, concepts::Message Resp> class Server;

/// @brief One active request: a zero-copy view plus response routing.
///
/// Returned by `Server::receive()`. `payload()` exposes the exact bytes the
/// client wrote, with no serialization step; `send_copy()` answers that
/// request, and the transport routes the response back to exactly the client
/// that issued it. Each request may be answered at most once.
///
/// The handle is move-only and owns the active request; dropping it without
/// answering leaves the client unanswered until its own timeout or abandoned
/// handle releases the slot.
///
/// @tparam Req Request payload type; constrained by `com::concepts::Message`.
/// @tparam Resp Response payload type; constrained by `com::concepts::Message`.
/// @note Spec `com/request-response`, requirements "Typed client and server"
///       and "Method zero-copy and non-POD rejection".
template <concepts::Message Req, concepts::Message Resp> class ServerRequest {
public:
    /// @brief Copying is disabled: the handle owns the active request.
    ServerRequest(const ServerRequest&) = delete;

    /// @brief Copy assignment is disabled: the handle owns the active request.
    ServerRequest& operator=(const ServerRequest&) = delete;

    /// @brief Move-construct a handle, transferring the active request.
    ///
    /// The moved-from handle no longer routes an answer.
    ServerRequest(ServerRequest&&) noexcept = default;

    /// @brief Move-assign a handle, transferring the active request.
    ///
    /// The destination's previous request is released first.
    /// @return `*this`.
    ServerRequest& operator=(ServerRequest&&) noexcept = default;

    /// @brief The exact bytes the client wrote.
    ///
    /// @return Const reference to the request payload in shared memory; valid
    ///         until the handle is dropped.
    /// @note Spec `com/request-response`, requirement "Method zero-copy and
    ///       non-POD rejection": no serialization or copy occurs.
    [[nodiscard]] const Req& payload() const noexcept { return inner_.payload(); }

    /// @brief Answer this request.
    ///
    /// The response is copied into shared memory once and reaches only the
    /// client that issued the request, thanks to transport-level correlation.
    ///
    /// @param response Response payload to send.
    /// @return Empty `Status` on success, or a mapped `com::Error`
    ///         (`OutOfSamples`, `UnableToDeliver`, ...) when the answer cannot
    ///         be handed over.
    /// @note Spec `com/request-response`, requirements "Typed client and
    ///       server" and "Correlation of requests". Non-blocking; a received
    ///       answer also advances the server's answered counter.
    [[nodiscard]] Status send_copy(const Resp& response) noexcept {
        auto sent = inner_.send_copy(response);
        if (!sent) {
            return std::unexpected(detail::map_error(sent.error()));
        }
        if (answered_) {
            answered_->fetch_add(1, std::memory_order_relaxed);
        }
        return {};
    }

    /// @brief Render the cold-path diagnostic string.
    ///
    /// @return The constant marker `ServerRequest()`; the request payload is
    ///         application data and intentionally not rendered.
    /// @note Mandate 9 (universal `to_string`); allocates, so cold path only.
    [[nodiscard]] std::string to_string() const { return "ServerRequest()"; }

private:
    friend class Server<Req, Resp>;

    /// @brief Adopt an active transport request from `Server::receive()`.
    ///
    /// @param inner Transport active request.
    /// @param answered Shared answered counter of the producing server; may be
    ///        empty when no server telemetry is attached.
    /// @note Private by design: only `Server::receive()` produces handles.
    ServerRequest(iox2::ActiveRequest<iox2::ServiceType::Ipc, Req, void, Resp, void> inner,
                  std::shared_ptr<std::atomic<std::uint64_t>> answered) noexcept
        : inner_(std::move(inner)), answered_(std::move(answered)) {}

    iox2::ActiveRequest<iox2::ServiceType::Ipc, Req, void, Resp, void> inner_; ///< Owned request.
    std::shared_ptr<std::atomic<std::uint64_t>> answered_; ///< Shared answered counter.
};

/// @brief Free helper mirroring `ServerRequest::to_string()`.
///
/// @tparam Req Request payload type satisfying `com::concepts::Message`.
/// @tparam Resp Response payload type satisfying `com::concepts::Message`.
/// @param request Handle to render.
/// @return The handle's diagnostic string.
/// @note Mandate 9 (universal `to_string`); allocates, so cold path only.
template <concepts::Message Req, concepts::Message Resp>
[[nodiscard]] inline std::string to_string(const ServerRequest<Req, Resp>& request) {
    return request.to_string();
}

/// @brief Server side of one request-response service.
///
/// The answering endpoint of the `com` Method pattern. It wraps
/// `iox2::Server` so application code never names an `iox2::` type (spec
/// `com/service-management`, "Protocol-independent boundary"). `receive()`
/// harvests the next request as a zero-copy `ServerRequest` view; answering it
/// routes the response to exactly the client that issued it.
///
/// The server owns the transport handle, so copying is disabled and moving
/// transfers ownership. `receive()` is a non-blocking, `noexcept` poll that
/// must be called from one thread; the instance is not internally
/// synchronized.
///
/// @tparam Req Request payload type; constrained by `com::concepts::Message`,
///         i.e. `std::is_trivially_copyable_v<Req>`.
/// @tparam Resp Response payload type; constrained by `com::concepts::Message`,
///         i.e. `std::is_trivially_copyable_v<Resp>`.
/// @note Implements `com/request-response`: "Typed client and server",
///       "Correlation of requests", "Method zero-copy and non-POD rejection",
///       and "Method port diagnostics".
template <concepts::Message Req, concepts::Message Resp> class Server {
public:
    /// @brief Open or create the service on `node` and create the server.
    ///
    /// Daemon-less discovery: the builder opens the service when it already
    /// exists and creates it otherwise. The server answers exactly the service
    /// it was created for.
    ///
    /// @param node Node that owns this process's transport membership; must
    ///        outlive the server.
    /// @param name Valid service name shared with the client.
    /// @return The constructed server, or a mapped `com::Error`
    ///         (`ConnectionFailure`, `IncompatibleTypeLayout`, ...) when the
    ///         service or server cannot be created.
    /// @note Spec `com/request-response`, requirement "Typed client and
    ///       server"; spec `com/service-management`, requirement "Node and
    ///       daemon-less discovery".
    [[nodiscard]] static Result<Server> create(Node& node, const ServiceName& name) noexcept {
        auto service = node.handle()
                           .service_builder(name.handle())
                           .request_response<Req, Resp>()
                           .open_or_create();
        if (!service) {
            return std::unexpected(detail::map_error(service.error()));
        }
        auto server = service->server_builder().create();
        if (!server) {
            return std::unexpected(detail::map_error(server.error()));
        }
        return Server{name, std::move(*server)};
    }

    /// @brief Copying is disabled: the server owns the transport handle.
    Server(const Server&) = delete;

    /// @brief Copy assignment is disabled: the server owns the transport
    ///        handle.
    Server& operator=(const Server&) = delete;

    /// @brief Move-construct a server, transferring the transport handle.
    ///
    /// The shared telemetry moves with the handle, so outstanding request
    /// views keep routing answers through the same counters.
    Server(Server&&) noexcept = default;

    /// @brief Move-assign a server, transferring the transport handle.
    ///
    /// The destination's previous handle is released; self-assignment is a
    /// no-op.
    /// @return `*this`.
    Server& operator=(Server&&) noexcept = default;

    /// @brief Harvest the next request.
    ///
    /// Non-blocking: returns an empty optional when no request is pending.
    ///
    /// @return The next `ServerRequest` handle, an empty optional when no
    ///         request is pending, or a mapped `com::Error` (for example
    ///         `ConnectionFailure`) when the transport cannot receive.
    /// @note Spec `com/request-response`, requirement "Typed client and
    ///       server": the payload is the client's shared-memory bytes.
    ///       Single consumer: call from one thread.
    [[nodiscard]] Result<std::optional<ServerRequest<Req, Resp>>> receive() noexcept {
        auto received = inner_.receive();
        if (!received) {
            return std::unexpected(detail::map_error(received.error()));
        }
        if (!received->has_value()) {
            return std::nullopt;
        }
        telemetry_->received_.fetch_add(1, std::memory_order_relaxed);
        return ServerRequest<Req, Resp>{std::move(*(*received)), telemetry_->answered_};
    }

    /// @brief Number of requests received and not yet answered.
    ///
    /// @return `received - answered` as a relaxed snapshot, clamped at zero.
    [[nodiscard]] std::uint64_t pending_count() const noexcept {
        const std::uint64_t received = telemetry_->received_.load(std::memory_order_relaxed);
        const std::uint64_t answered = telemetry_->answered_->load(std::memory_order_relaxed);
        return received > answered ? received - answered : 0U;
    }

    /// @brief Render the cold-path diagnostic string.
    ///
    /// @return `Server(service=<name>, pending=<n>)`.
    /// @note Mandate 9 (universal `to_string`); `com/request-response`,
    ///       requirement "Method port diagnostics". Allocates, so cold path
    ///       only.
    [[nodiscard]] std::string to_string() const {
        return std::format("Server(service={}, pending={})", service_name_.to_string(),
                           pending_count());
    }

private:
    /// @brief Request counters shared by the server and its request handles.
    ///
    /// Stays exact after the server moves because the answered counter is
    /// shared with every outstanding `ServerRequest`. One allocation at
    /// `create()`; the hot paths touch relaxed atomics only.
    ///
    /// @note Spec `com/request-response`, requirement "Method port
    ///       diagnostics".
    struct Telemetry {
        std::atomic<std::uint64_t> received_{0}; ///< Requests received so far.
        /// @brief Requests answered so far, shared with request handles.
        std::shared_ptr<std::atomic<std::uint64_t>> answered_{
            std::make_shared<std::atomic<std::uint64_t>>(0)};
    };

    /// @brief Adopt an already-created transport server.
    ///
    /// @param name Service name retained for `to_string()`.
    /// @param inner Transport server returned by the iceoryx2 builder.
    /// @note Private by design: the only supported construction path is
    ///       `create()`, which also allocates the shared telemetry once.
    Server(ServiceName name,
           iox2::Server<iox2::ServiceType::Ipc, Req, void, Resp, void> inner) noexcept
        : service_name_(std::move(name)), inner_(std::move(inner)),
          telemetry_(std::make_shared<Telemetry>()) {}

    ServiceName service_name_; ///< Service identity rendered by `to_string()`.
    iox2::Server<iox2::ServiceType::Ipc, Req, void, Resp, void> inner_; ///< Owned transport handle.
    std::shared_ptr<Telemetry> telemetry_; ///< Shared with outstanding request handles.
};

/// @brief Free helper mirroring `Server::to_string()`.
///
/// @tparam Req Request payload type satisfying `com::concepts::Message`.
/// @tparam Resp Response payload type satisfying `com::concepts::Message`.
/// @param server Server to render.
/// @return The server's diagnostic string.
/// @note Mandate 9 (universal `to_string`); allocates, so cold path only.
template <concepts::Message Req, concepts::Message Resp>
[[nodiscard]] inline std::string to_string(const Server<Req, Resp>& server) {
    return server.to_string();
}

} // namespace nexigen::com

/// @brief Formats a `ServerRequest` as its diagnostic string.
///
/// @tparam Req Request payload type satisfying `com::concepts::Message`.
/// @tparam Resp Response payload type satisfying `com::concepts::Message`.
/// @note Mandate 11 (universal `std::formatter`); delegates to
///       `ServerRequest::to_string()`, which allocates, so cold path only.
template <nexigen::com::concepts::Message Req, nexigen::com::concepts::Message Resp>
struct std::formatter<nexigen::com::ServerRequest<Req, Resp>> : std::formatter<std::string_view> {
    /// @brief Write the request's diagnostic text into the format context.
    ///
    /// @tparam Ctx `std::format` context type.
    /// @param request Handle to format.
    /// @param ctx Output context supplied by `std::format`.
    /// @return Iterator one past the last written character.
    template <class Ctx>
    auto format(const nexigen::com::ServerRequest<Req, Resp>& request, Ctx& ctx) const {
        const std::string text = request.to_string();
        return std::formatter<std::string_view>::format(text, ctx);
    }
};

/// @brief Formats a `Server` as its diagnostic string.
///
/// @tparam Req Request payload type satisfying `com::concepts::Message`.
/// @tparam Resp Response payload type satisfying `com::concepts::Message`.
/// @note Mandate 11 (universal `std::formatter`); delegates to
///       `Server::to_string()`, which allocates, so cold path only.
template <nexigen::com::concepts::Message Req, nexigen::com::concepts::Message Resp>
struct std::formatter<nexigen::com::Server<Req, Resp>> : std::formatter<std::string_view> {
    /// @brief Write the server's diagnostic text into the format context.
    ///
    /// @tparam Ctx `std::format` context type.
    /// @param server Server to format.
    /// @param ctx Output context supplied by `std::format`.
    /// @return Iterator one past the last written character.
    template <class Ctx>
    auto format(const nexigen::com::Server<Req, Resp>& server, Ctx& ctx) const {
        const std::string text = server.to_string();
        return std::formatter<std::string_view>::format(text, ctx);
    }
};
