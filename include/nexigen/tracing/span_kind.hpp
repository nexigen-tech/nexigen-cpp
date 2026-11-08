// nexigen::tracing — SpanKind enum.
#pragma once

#include <cstdint>
#include <string>
#include <string_view>

#include <nlohmann/json.hpp>

namespace nexigen::tracing {

/// Causal role of a span inside the traced system (OTel-compatible names).
enum class SpanKind : std::uint8_t {
    Internal, ///< Work not attributed to an external transaction (default).
    Server,   ///< Synchronous handler of an inbound request.
    Client,   ///< Synchronous issuer of an outbound request.
    Producer, ///< Asynchronous producer of a message/event.
    Consumer, ///< Asynchronous consumer of a message/event.
};

[[nodiscard]] constexpr std::string_view to_string(SpanKind kind) noexcept {
    switch (kind) {
        case SpanKind::Internal: return "Internal";
        case SpanKind::Server:   return "Server";
        case SpanKind::Client:   return "Client";
        case SpanKind::Producer: return "Producer";
        case SpanKind::Consumer: return "Consumer";
        default:                 return "Unknown";
    }
}

[[nodiscard]] constexpr bool from_string(std::string_view name, SpanKind& out) noexcept {
    if (name == "Internal") {
        out = SpanKind::Internal;
        return true;
    }
    if (name == "Server") {
        out = SpanKind::Server;
        return true;
    }
    if (name == "Client") {
        out = SpanKind::Client;
        return true;
    }
    if (name == "Producer") {
        out = SpanKind::Producer;
        return true;
    }
    if (name == "Consumer") {
        out = SpanKind::Consumer;
        return true;
    }
    return false;
}

inline void from_json(const nlohmann::json& j, SpanKind& out) noexcept {
    const auto* const name = j.get_ptr<const nlohmann::json::string_t*>();
    if (name != nullptr) {
        static_cast<void>(from_string(std::string_view(*name), out));
    }
}


inline void to_json(nlohmann::json& j, const SpanKind spanKind) {
    j = std::string(to_string(spanKind));
}

} // namespace nexigen::tracing
