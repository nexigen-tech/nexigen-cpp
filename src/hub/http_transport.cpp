// nexigen::hub — listen-address parsing
// (quality-tail-cleanup; P3-30).
#include "http_transport.hpp"

namespace nexigen::hub::detail {

namespace {

[[nodiscard]] bool parse_port(std::string_view listen, std::size_t begin,
                              std::uint16_t& out_port) noexcept {
    if (begin >= listen.size()) {
        return false;
    }
    std::uint64_t port_value = 0;
    for (std::size_t index = begin; index < listen.size(); ++index) {
        const char c = listen[index];
        if (c < '0' || c > '9') {
            return false;
        }
        port_value = port_value * std::uint64_t{10} + static_cast<std::uint64_t>(c - '0');
        if (port_value > 65535ULL) {
            return false;
        }
    }
    out_port = static_cast<std::uint16_t>(port_value);
    return true;
}

} // namespace

bool parse_listen(std::string_view listen, ListenAddress& out) noexcept {
    out = ListenAddress{};
    if (listen.empty()) {
        return false;
    }
    if (listen.front() == '[') {
        const std::size_t closing = listen.find(']');
        if (closing == std::string_view::npos || closing + 2 >= listen.size() ||
            listen[closing + 1] != ':') {
            return false;
        }
        out.host = std::string(listen.substr(1, closing - 1));
        out.ipv6 = true;
        return parse_port(listen, closing + 2, out.port);
    }
    const std::size_t colon = listen.find(':');
    if (colon == std::string_view::npos || colon == 0 ||
        listen.find(':', colon + 1) != std::string_view::npos) {
        return false;
    }
    out.host = std::string(listen.substr(0, colon));
    if (out.host == "localhost") {
        out.host = "127.0.0.1";
    }
    out.ipv6 = out.host.find(':') != std::string_view::npos;
    return parse_port(listen, colon + 1, out.port);
}

} // namespace nexigen::hub::detail
