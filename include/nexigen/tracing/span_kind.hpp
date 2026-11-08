// nexigen::tracing — SpanKind enum.
#pragma once

#include <array>
#include <cstdint>
#include <string>
#include <string_view>

#include <nlohmann/json.hpp>

#include <nexigen/core/enum_traits.hpp>
namespace nexigen::tracing {

/// Causal role of a span inside the traced system (OTel-compatible names).
enum class SpanKind : std::uint8_t {
    Internal, ///< Work not attributed to an external transaction (default).
    Server,   ///< Synchronous handler of an inbound request.
    Client,   ///< Synchronous issuer of an outbound request.
    Producer, ///< Asynchronous producer of a message/event.
    Consumer, ///< Asynchronous consumer of a message/event.
};

} // namespace nexigen::tracing

namespace nexigen::tracing {

[[nodiscard]] constexpr std::string_view to_string(SpanKind kind) noexcept {
    return core::enum_name(kind);
}

[[nodiscard]] constexpr bool from_string(std::string_view name, SpanKind& out) noexcept {
    return core::enum_value(name, out);
}

inline void from_json(const nlohmann::json& j, SpanKind& out) noexcept {
    core::mandate::enum_from_json(j, out);
}

inline void to_json(nlohmann::json& j, const SpanKind spanKind) {
    core::mandate::enum_to_json(j, spanKind);
}

} // namespace nexigen::tracing

// Vocabulary table for core::enum_name/enum_value (change refactor-enum-traits-helper).
namespace nexigen::core {

template <> struct EnumTraits<nexigen::tracing::SpanKind> {
    struct Entry {
        nexigen::tracing::SpanKind value;
        std::string_view name;
    };
    static constexpr std::array<Entry, 5> entries{{
        {nexigen::tracing::SpanKind::Internal, "Internal"},
        {nexigen::tracing::SpanKind::Server, "Server"},
        {nexigen::tracing::SpanKind::Client, "Client"},
        {nexigen::tracing::SpanKind::Producer, "Producer"},
        {nexigen::tracing::SpanKind::Consumer, "Consumer"},
    }};
};

} // namespace nexigen::core
