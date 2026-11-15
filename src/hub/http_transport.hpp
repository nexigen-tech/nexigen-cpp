// nexigen::hub — listen-address parsing extracted from HttpServer
// (quality-tail-cleanup; P3-30).
#pragma once

#include <cstdint>
#include <string>
#include <string_view>

namespace nexigen::hub::detail {

/// @brief Parsed `--listen` target.
struct ListenAddress {
    std::string host{};
    std::uint16_t port{0};
    bool ipv6{false};
};

/// @brief Parse `host:port`, `[ipv6]:port`, or a bare `addr:port` form.
[[nodiscard]] bool parse_listen(std::string_view listen, ListenAddress& out) noexcept;

} // namespace nexigen::hub::detail
