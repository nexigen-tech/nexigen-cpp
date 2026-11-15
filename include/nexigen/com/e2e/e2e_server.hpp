// nexigen::com — E2E-protected request/response server (change
// add-e2e-protection).
#pragma once

#include <cstdint>
#include <format>
#include <string>
#include <utility>

#include <nexigen/com/e2e/e2e_codec.hpp>
#include <nexigen/com/e2e/e2e_config.hpp>
#include <nexigen/com/e2e/e2e_receiver.hpp>
#include <nexigen/com/node.hpp>
#include <nexigen/com/server.hpp>
#include <nexigen/com/service_name.hpp>
#include <nexigen/core/result.hpp>

namespace nexigen::com {

/// @brief Server that checks requests and seals responses.
///
/// The handler receives the request payload together with its E2E status, so a
/// corrupted request still reaches the application and can be answered with a
/// safe response instead of being silently dropped.
///
/// @note Spec `com/e2e`, requirement "Method protection".
template <concepts::Message Req, concepts::Message Resp> class E2eServer {
public:
    /// @brief Open the service and create the enveloped server.
    [[nodiscard]] static Result<E2eServer> create(Node& node, const ServiceName& name,
                                                  E2eConfig config) noexcept {
        if (config.profile == E2eProfile::None) {
            return std::unexpected(Error::InvalidArgument);
        }
        auto inner = Server<E2eMessage<Req>, E2eMessage<Resp>>::create(node, name);
        if (!inner) {
            return std::unexpected(inner.error());
        }
        return E2eServer{std::move(*inner), config};
    }

    E2eServer(const E2eServer&) = delete;
    E2eServer& operator=(const E2eServer&) = delete;
    E2eServer(E2eServer&&) noexcept = default;
    E2eServer& operator=(E2eServer&&) noexcept = default;

    /// @brief Check one request and build the sealed response.
    ///
    /// Pure with respect to the transport: the handler always runs, even for a
    /// corrupted request, and receives the failure status so it can answer
    /// safely.
    ///
    /// @param request Envelope to check.
    /// @param handler Callable `(const Req&, E2eStatus, const E2eState&) -> Resp`.
    /// @return The sealed response envelope.
    template <class Handler>
    [[nodiscard]] E2eMessage<Resp> handle(const E2eMessage<Req>& request,
                                          Handler&& handler) noexcept {
        const E2eStatus status = receiver_.accept(request);
        E2eMessage<Resp> response{};
        response.payload = handler(request.payload, status, receiver_.state());
        e2e::seal_message(response, config_.profile, config_.data_id, next_counter_);
        next_counter_ = (next_counter_ + 1U) & e2e::counter_mask(config_.profile);
        return response;
    }

    /// @brief Receive one request and answer it.
    ///
    /// @param handler Callable `(const Req&, E2eStatus, const E2eState&) -> Resp`.
    /// @return Empty status when no request was pending or the response was
    ///         sent, or the mapped transport error.
    template <class Handler> [[nodiscard]] Status serve_once(Handler&& handler) noexcept {
        auto received = inner_.receive();
        if (!received) {
            return std::unexpected(received.error());
        }
        if (!received->has_value()) {
            return {};
        }
        const E2eMessage<Resp> response = handle((*received)->payload(), handler);
        return (*received)->send_copy(response);
    }

    /// @brief Counters accumulated on the request direction.
    [[nodiscard]] const E2eState& state() const noexcept { return receiver_.state(); }

    /// @brief Cold-path diagnostic rendering.
    [[nodiscard]] std::string to_string() const {
        return std::format("E2eServer(profile={}, data_id={}, {})",
                           nexigen::com::to_string(config_.profile), config_.data_id,
                           receiver_.state().to_string());
    }

private:
    E2eServer(Server<E2eMessage<Req>, E2eMessage<Resp>> inner, E2eConfig config) noexcept
        : inner_(std::move(inner)), config_(config),
          receiver_(config.profile, config.data_id, config.max_delta_counter) {}

    Server<E2eMessage<Req>, E2eMessage<Resp>> inner_; ///< Enveloped server.
    E2eConfig config_;                               ///< Service configuration.
    E2eReceiver receiver_;                           ///< Request check state.
    std::uint32_t next_counter_{1};                  ///< Response counter.
};

/// @brief Free helper mirroring `E2eServer::to_string()` (mandate 9).
template <concepts::Message Req, concepts::Message Resp>
[[nodiscard]] std::string to_string(const E2eServer<Req, Resp>& server) {
    return server.to_string();
}

} // namespace nexigen::com

/// @brief `std::formatter` for the E2E server (project mandate 11).
template <nexigen::com::concepts::Message Req, nexigen::com::concepts::Message Resp>
struct std::formatter<nexigen::com::E2eServer<Req, Resp>> : std::formatter<std::string_view> {
    template <class Ctx> auto format(const nexigen::com::E2eServer<Req, Resp>& server,
                                     Ctx& ctx) const {
        return std::formatter<std::string_view>::format(server.to_string(), ctx);
    }
};
