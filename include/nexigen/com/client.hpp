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

/// Client side of one request-response service.
template <concepts::Message Req, concepts::Message Resp>
class Client {
public:
    /// Open (or create) the service on the node and create the client.
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

    Client(const Client&) = delete;
    Client& operator=(const Client&) = delete;
    Client(Client&&) noexcept = default;
    Client& operator=(Client&&) noexcept = default;

    /// Send an asynchronous request; the returned handle carries a fresh
    /// correlation id and harvests exactly this request's responses.
    [[nodiscard]] Result<PendingResponse<Req, Resp>> send(const Req& request) noexcept {
        auto pending = inner_.send_copy(request);
        if (!pending) {
            return std::unexpected(detail::map_error(pending.error()));
        }
        const std::uint64_t correlation_id = telemetry_->correlation_.fetch_add(1, std::memory_order_relaxed);
        telemetry_->outstanding_.fetch_add(1, std::memory_order_relaxed);
        return PendingResponse<Req, Resp>{std::move(*pending), correlation_id, telemetry_};
    }

    /// Synchronous call: blocks until the response arrives or the timeout
    /// elapses. On timeout the request is abandoned and Error::Timeout is
    /// returned; the caller is free to ignore or retry.
    [[nodiscard]] Result<Resp> call(const Req& request, std::chrono::milliseconds timeout) noexcept {
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

    /// Number of requests sent and not yet completed (abandoned or answered).
    [[nodiscard]] std::uint64_t pending_count() const noexcept {
        return telemetry_->outstanding_.load(std::memory_order_relaxed);
    }

    /// Cold-path diagnostic (mandate 9): service name and the pending count.
    [[nodiscard]] std::string to_string() const {
        return std::format("Client(service={}, pending={})", service_name_.to_string(),
                           pending_count());
    }

private:
    Client(ServiceName name, iox2::Client<iox2::ServiceType::Ipc, Req, void, Resp, void> inner) noexcept
        : service_name_(std::move(name)),
          inner_(std::move(inner)),
          telemetry_(std::make_shared<detail::ClientTelemetry>()) {}

    ServiceName service_name_;
    iox2::Client<iox2::ServiceType::Ipc, Req, void, Resp, void> inner_;
    std::shared_ptr<detail::ClientTelemetry> telemetry_;
};

/// Free helper (mandate 9): mirrors the member.
template <concepts::Message Req, concepts::Message Resp>
[[nodiscard]] inline std::string to_string(const Client<Req, Resp>& client) {
    return client.to_string();
}

} // namespace nexigen::com

/// The client formats as its diagnostic string (mandate 11).
template <nexigen::com::concepts::Message Req, nexigen::com::concepts::Message Resp>
struct std::formatter<nexigen::com::Client<Req, Resp>> : std::formatter<std::string_view> {
    template <class Ctx>
    auto format(const nexigen::com::Client<Req, Resp>& client, Ctx& ctx) const {
        const std::string text = client.to_string();
        return std::formatter<std::string_view>::format(text, ctx);
    }
};
