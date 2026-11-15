// nexigen::com — E2E-protected request/response client (change
// add-e2e-protection).
#pragma once

#include <chrono>
#include <cstdint>
#include <format>
#include <string>
#include <utility>

#include <nexigen/com/client.hpp>
#include <nexigen/com/e2e/e2e_codec.hpp>
#include <nexigen/com/e2e/e2e_config.hpp>
#include <nexigen/com/e2e/e2e_receiver.hpp>
#include <nexigen/com/node.hpp>
#include <nexigen/com/service_name.hpp>
#include <nexigen/core/result.hpp>

namespace nexigen::com {

/// @brief One protected response: payload plus the E2E verdict.
template <concepts::Message Resp> struct E2eResponse {
    Resp payload{};                       ///< Response payload.
    E2eStatus status{E2eStatus::Ok};      ///< Check result.
    E2eState state{};                     ///< Counter snapshot.
};

/// @brief Client that seals requests and checks responses.
///
/// @note Spec `com/e2e`, requirement "Method protection".
template <concepts::Message Req, concepts::Message Resp> class E2eClient {
public:
    /// @brief Open the service and create the enveloped client.
    [[nodiscard]] static Result<E2eClient> create(Node& node, const ServiceName& name,
                                                  E2eConfig config) noexcept {
        if (config.profile == E2eProfile::None) {
            return std::unexpected(Error::InvalidArgument);
        }
        auto inner = Client<E2eMessage<Req>, E2eMessage<Resp>>::create(node, name);
        if (!inner) {
            return std::unexpected(inner.error());
        }
        return E2eClient{std::move(*inner), config};
    }

    E2eClient(const E2eClient&) = delete;
    E2eClient& operator=(const E2eClient&) = delete;
    E2eClient(E2eClient&&) noexcept = default;
    E2eClient& operator=(E2eClient&&) noexcept = default;

    /// @brief Seal the request, call the method, and check the response.
    [[nodiscard]] Result<E2eResponse<Resp>> call(const Req& request,
                                                 std::chrono::milliseconds timeout) noexcept {
        E2eMessage<Req> envelope{};
        envelope.payload = request;
        e2e::seal_message(envelope, config_.profile, config_.data_id, next_counter_);
        next_counter_ = (next_counter_ + 1U) & e2e::counter_mask(config_.profile);
        auto response = inner_.call(envelope, timeout);
        if (!response) {
            return std::unexpected(response.error());
        }
        const E2eStatus status = receiver_.accept(*response);
        return E2eResponse<Resp>{response->payload, status, receiver_.state()};
    }

    /// @brief Counters accumulated on the response direction.
    [[nodiscard]] const E2eState& state() const noexcept { return receiver_.state(); }

    /// @brief Cold-path diagnostic rendering.
    [[nodiscard]] std::string to_string() const {
        return std::format("E2eClient(profile={}, data_id={}, {})",
                           nexigen::com::to_string(config_.profile), config_.data_id,
                           receiver_.state().to_string());
    }

private:
    E2eClient(Client<E2eMessage<Req>, E2eMessage<Resp>> inner, E2eConfig config) noexcept
        : inner_(std::move(inner)), config_(config),
          receiver_(config.profile, config.data_id, config.max_delta_counter) {}

    Client<E2eMessage<Req>, E2eMessage<Resp>> inner_; ///< Enveloped client.
    E2eConfig config_;                               ///< Service configuration.
    E2eReceiver receiver_;                           ///< Response check state.
    std::uint32_t next_counter_{1};                  ///< Request counter.
};

/// @brief Free helper mirroring `E2eClient::to_string()` (mandate 9).
template <concepts::Message Req, concepts::Message Resp>
[[nodiscard]] std::string to_string(const E2eClient<Req, Resp>& client) {
    return client.to_string();
}

} // namespace nexigen::com

/// @brief `std::formatter` for the E2E client (project mandate 11).
template <nexigen::com::concepts::Message Req, nexigen::com::concepts::Message Resp>
struct std::formatter<nexigen::com::E2eClient<Req, Resp>> : std::formatter<std::string_view> {
    template <class Ctx> auto format(const nexigen::com::E2eClient<Req, Resp>& client,
                                     Ctx& ctx) const {
        return std::formatter<std::string_view>::format(client.to_string(), ctx);
    }
};
