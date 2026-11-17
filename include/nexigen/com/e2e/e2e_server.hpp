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
/// @tparam Req Request payload type satisfying `com::concepts::Message`.
/// @tparam Resp Response payload type satisfying `com::concepts::Message`.
/// @note Spec `com/e2e`, requirements "Method protection" and "Wrappers require
///       a protection profile".
template <concepts::Message Req, concepts::Message Resp> class E2eServer {
public:
    /// @brief Open the service and create the enveloped server.
    ///
    /// @param node Node that owns this process's transport membership; must
    ///        outlive the server.
    /// @param name Valid service name shared with the client.
    /// @param config E2E profile, data id, and counter tolerance.
    /// @return The constructed server, or `InvalidArgument` when `config`
    ///         selects `E2eProfile::None`, or another mapped transport error.
    /// @note Spec `com/e2e`, requirement "Wrappers require a protection
    ///       profile": the profile is validated before any port is opened.
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

    /// @brief Copying is disabled: the server owns the transport handle.
    E2eServer(const E2eServer&) = delete;
    /// @brief Copy assignment is disabled: the server owns the transport handle.
    /// @return This server.
    E2eServer& operator=(const E2eServer&) = delete;
    /// @brief Move-construct a server, transferring the transport handle.
    E2eServer(E2eServer&&) noexcept = default;
    /// @brief Move-assign a server, transferring the transport handle.
    /// @return This server.
    E2eServer& operator=(E2eServer&&) noexcept = default;

    /// @brief Check one request and build the sealed response.
    ///
    /// Pure with respect to the transport: the handler always runs, even for a
    /// corrupted request, and receives the failure status so it can answer
    /// safely.
    ///
    /// @tparam Handler Callable with signature
    ///         `Resp(const Req&, E2eStatus, const E2eState&)`.
    /// @param request Envelope to check.
    /// @param handler Callable that builds the response payload.
    /// @return The sealed response envelope.
    /// @note Spec `com/e2e`, requirements "Method protection", "Receiver
    ///       supervision", and "Sender counter progression".
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
    /// @tparam Handler Callable with signature
    ///         `Resp(const Req&, E2eStatus, const E2eState&)`.
    /// @param handler Callable that builds the response payload.
    /// @return Empty status when no request was pending or the response was
    ///         sent, or the mapped transport error.
    /// @note Spec `com/e2e`, requirements "Method protection" and "Zero-copy
    ///       E2E ports"; non-blocking when no request is queued.
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
    /// @return Borrowed per-endpoint counters.
    /// @note Spec `com/e2e`, requirement "Receiver supervision".
    [[nodiscard]] const E2eState& state() const noexcept { return receiver_.state(); }

    /// @brief Cold-path diagnostic rendering.
    /// @return `E2eServer(profile=..., data_id=..., E2eState(...))`.
    /// @note Spec `com/e2e`, requirement "Method protection".
    [[nodiscard]] std::string to_string() const {
        return std::format("E2eServer(profile={}, data_id={}, {})",
                           nexigen::com::to_string(config_.profile), config_.data_id,
                           receiver_.state().to_string());
    }

private:
    /// @brief Adopt an existing enveloped server and its configuration.
    /// @param inner Wrapped `com::Server` instance.
    /// @param config E2E profile, data id, and counter tolerance.
    E2eServer(Server<E2eMessage<Req>, E2eMessage<Resp>> inner, E2eConfig config) noexcept
        : inner_(std::move(inner)), config_(config),
          receiver_(config.profile, config.data_id, config.max_delta_counter) {}

    Server<E2eMessage<Req>, E2eMessage<Resp>> inner_; ///< Enveloped server.
    E2eConfig config_;                                ///< Service configuration.
    E2eReceiver receiver_;                            ///< Request check state.
    std::uint32_t next_counter_{1};                   ///< Response counter.
};

/// @brief Free helper mirroring `E2eServer::to_string()` (mandate 9).
///
/// @tparam Req Request payload type.
/// @tparam Resp Response payload type.
/// @param server Server to render.
/// @return The diagnostic string.
/// @note Spec `com/e2e`, requirement "Method protection".
template <concepts::Message Req, concepts::Message Resp>
[[nodiscard]] std::string to_string(const E2eServer<Req, Resp>& server) {
    return server.to_string();
}

} // namespace nexigen::com

/// @brief `std::formatter` for the E2E server (project mandate 11).
template <nexigen::com::concepts::Message Req, nexigen::com::concepts::Message Resp>
struct std::formatter<nexigen::com::E2eServer<Req, Resp>> : std::formatter<std::string_view> {
    /// @brief Write the server diagnostic into the format context.
    /// @tparam Ctx `std::format` context type.
    /// @param server Server to format.
    /// @param ctx Output context supplied by `std::format`.
    /// @return Iterator one past the last written character.
    /// @note Spec `com/e2e`, requirement "Method protection".
    template <class Ctx>
    auto format(const nexigen::com::E2eServer<Req, Resp>& server, Ctx& ctx) const {
        return std::formatter<std::string_view>::format(server.to_string(), ctx);
    }
};
