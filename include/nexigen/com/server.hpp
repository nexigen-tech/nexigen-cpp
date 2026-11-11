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

template <concepts::Message Req, concepts::Message Resp> class Server;

/// One active request: a zero-copy view of the request payload plus the
/// routing that sends the response back to the requesting client.
template <concepts::Message Req, concepts::Message Resp> class ServerRequest {
public:
    ServerRequest(const ServerRequest&) = delete;
    ServerRequest& operator=(const ServerRequest&) = delete;
    ServerRequest(ServerRequest&&) noexcept = default;
    ServerRequest& operator=(ServerRequest&&) noexcept = default;

    /// The exact bytes the client wrote (no serialization step).
    [[nodiscard]] const Req& payload() const noexcept { return inner_.payload(); }

    /// Answer this request; the response reaches only the client that issued
    /// it (transport-level correlation).
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

    /// Cold-path diagnostic (mandate 9).
    [[nodiscard]] std::string to_string() const { return "ServerRequest()"; }

private:
    friend class Server<Req, Resp>;

    ServerRequest(iox2::ActiveRequest<iox2::ServiceType::Ipc, Req, void, Resp, void> inner,
                  std::shared_ptr<std::atomic<std::uint64_t>> answered) noexcept
        : inner_(std::move(inner)), answered_(std::move(answered)) {}

    iox2::ActiveRequest<iox2::ServiceType::Ipc, Req, void, Resp, void> inner_;
    std::shared_ptr<std::atomic<std::uint64_t>> answered_;
};

/// Free helper (mandate 9): mirrors the member.
template <concepts::Message Req, concepts::Message Resp>
[[nodiscard]] inline std::string to_string(const ServerRequest<Req, Resp>& request) {
    return request.to_string();
}

/// Server side of one request-response service.
template <concepts::Message Req, concepts::Message Resp> class Server {
public:
    /// Open (or create) the service on the node and create the server.
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

    Server(const Server&) = delete;
    Server& operator=(const Server&) = delete;
    Server(Server&&) noexcept = default;
    Server& operator=(Server&&) noexcept = default;

    /// Harvest the next request; ok with no value when none is pending.
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

    /// Number of requests received and not yet answered.
    [[nodiscard]] std::uint64_t pending_count() const noexcept {
        const std::uint64_t received = telemetry_->received_.load(std::memory_order_relaxed);
        const std::uint64_t answered = telemetry_->answered_->load(std::memory_order_relaxed);
        return received > answered ? received - answered : 0U;
    }

    /// Cold-path diagnostic (mandate 9): service name and the pending count.
    [[nodiscard]] std::string to_string() const {
        return std::format("Server(service={}, pending={})", service_name_.to_string(),
                           pending_count());
    }

private:
    /// Shared by the server and the request handles it hands out, so the
    /// pending counter (received - answered) stays exact after the server
    /// moves. One allocation at create(); the hot paths touch relaxes
    /// atomics only.
    struct Telemetry {
        std::atomic<std::uint64_t> received_{0};
        std::shared_ptr<std::atomic<std::uint64_t>> answered_{
            std::make_shared<std::atomic<std::uint64_t>>(0)};
    };

    Server(ServiceName name,
           iox2::Server<iox2::ServiceType::Ipc, Req, void, Resp, void> inner) noexcept
        : service_name_(std::move(name)), inner_(std::move(inner)),
          telemetry_(std::make_shared<Telemetry>()) {}

    ServiceName service_name_;
    iox2::Server<iox2::ServiceType::Ipc, Req, void, Resp, void> inner_;
    std::shared_ptr<Telemetry> telemetry_;
};

/// Free helper (mandate 9): mirrors the member.
template <concepts::Message Req, concepts::Message Resp>
[[nodiscard]] inline std::string to_string(const Server<Req, Resp>& server) {
    return server.to_string();
}

} // namespace nexigen::com

/// The server request formats as its diagnostic string (mandate 11).
template <nexigen::com::concepts::Message Req, nexigen::com::concepts::Message Resp>
struct std::formatter<nexigen::com::ServerRequest<Req, Resp>> : std::formatter<std::string_view> {
    template <class Ctx>
    auto format(const nexigen::com::ServerRequest<Req, Resp>& request, Ctx& ctx) const {
        const std::string text = request.to_string();
        return std::formatter<std::string_view>::format(text, ctx);
    }
};

/// The server formats as its diagnostic string (mandate 11).
template <nexigen::com::concepts::Message Req, nexigen::com::concepts::Message Resp>
struct std::formatter<nexigen::com::Server<Req, Resp>> : std::formatter<std::string_view> {
    template <class Ctx>
    auto format(const nexigen::com::Server<Req, Resp>& server, Ctx& ctx) const {
        const std::string text = server.to_string();
        return std::formatter<std::string_view>::format(text, ctx);
    }
};
