// nexigen::tracing — span vocabulary catalogue (design D4; spec
// tracing/span-enums).
//
// Three scoped enums fix the tracing vocabulary: SpanKind (causal role of the
// span), SpanStatus (outcome), SamplingDecision (the two-value sampling
// vocabulary applied at span start). uint8_t underlying type keeps each one
// byte inside the fixed SpanRecord layout; the declaration order is the
// numeric order and append-only (recompiled and non-recompiled translation
// units stay compatible).
//
// HOT PATH: to_string is constexpr over static-storage literals — no
// allocation, no exception path, exactly like nexigen::core::to_string(
// ErrorCode). The `default:` arm keeps a garbage-cast value well-defined
// ("Unknown"); every enumerator is pinned by static_assert in the test TU.
//
// Serialization trio (mandates 9–11): constexpr to_string/from_string,
// non-throwing ADL to_json/from_json under JSON_NOEXCEPTION=1 (the
// result.hpp get_ptr pattern), std::formatter with a templated format
// context (libstdc++ 15 evaluates formattability against a substituted
// context — same note as ErrorCode/LogLevel).
#pragma once

#include <cstdint>
#include <format>
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

/// Outcome of a span; Unset is the start-time default.
enum class SpanStatus : std::uint8_t {
    Unset, ///< No explicit outcome recorded (default).
    Ok,    ///< Completed successfully.
    Error, ///< Completed with a failure.
};

/// The whole sampling vocabulary (design non-goal: no policy engines).
enum class SamplingDecision : std::uint8_t {
    Drop,           ///< Do not record: start_span short-circuits.
    RecordAndSample,///< Record the span (default).
};

/// Compile-time canonical name — the exact enumerator spelling used by JSON
/// and diagnostics. Static storage, allocation-free; the `default` arm makes
/// a garbage-cast value well-defined ("Unknown").
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

/// Parse a canonical SpanKind name; false leaves the out-parameter UNCHANGED
/// on any other spelling (including "Unknown" — a rendering, not an enumerator).
[[nodiscard]] constexpr bool from_string(std::string_view name, SpanKind& out) noexcept {
    if (name == "Internal") { out = SpanKind::Internal; return true; }
    if (name == "Server")   { out = SpanKind::Server;   return true; }
    if (name == "Client")   { out = SpanKind::Client;   return true; }
    if (name == "Producer") { out = SpanKind::Producer; return true; }
    if (name == "Consumer") { out = SpanKind::Consumer; return true; }
    return false;
}

/// Canonical name of a status value; same contract as SpanKind::to_string.
[[nodiscard]] constexpr std::string_view to_string(SpanStatus status) noexcept {
    switch (status) {
        case SpanStatus::Unset: return "Unset";
        case SpanStatus::Ok:    return "Ok";
        case SpanStatus::Error: return "Error";
        default:                return "Unknown";
    }
}

/// Parse a canonical SpanStatus name; same contract as SpanKind::from_string.
[[nodiscard]] constexpr bool from_string(std::string_view name, SpanStatus& out) noexcept {
    if (name == "Unset") { out = SpanStatus::Unset; return true; }
    if (name == "Ok")    { out = SpanStatus::Ok;    return true; }
    if (name == "Error") { out = SpanStatus::Error; return true; }
    return false;
}

/// Canonical name of a sampling decision; same contract as above.
[[nodiscard]] constexpr std::string_view to_string(SamplingDecision decision) noexcept {
    switch (decision) {
        case SamplingDecision::Drop:             return "Drop";
        case SamplingDecision::RecordAndSample:  return "RecordAndSample";
        default:                                 return "Unknown";
    }
}

/// Parse a canonical SamplingDecision name; same contract as above.
[[nodiscard]] constexpr bool from_string(std::string_view name, SamplingDecision& out) noexcept {
    if (name == "Drop")            { out = SamplingDecision::Drop;            return true; }
    if (name == "RecordAndSample") { out = SamplingDecision::RecordAndSample; return true; }
    return false;
}

/// ADL JSON writer (cold path only): the JSON value is exactly to_string(v).
inline void to_json(nlohmann::json& j, const SpanKind kind) {
    j = std::string(to_string(kind));
}
/// ADL JSON reader, strictly non-throwing under JSON_NOEXCEPTION=1: non-string
/// values yield nullptr from get_ptr; unknown names leave `out` unchanged.
inline void from_json(const nlohmann::json& j, SpanKind& out) noexcept {
    const auto* const name = j.get_ptr<const nlohmann::json::string_t*>();
    if (name != nullptr) {
        static_cast<void>(from_string(std::string_view(*name), out));
    }
}

/// ADL JSON writer (cold path only); see SpanKind::to_json.
inline void to_json(nlohmann::json& j, const SpanStatus status) {
    j = std::string(to_string(status));
}
/// ADL JSON reader, strictly non-throwing; see SpanKind::from_json.
inline void from_json(const nlohmann::json& j, SpanStatus& out) noexcept {
    const auto* const name = j.get_ptr<const nlohmann::json::string_t*>();
    if (name != nullptr) {
        static_cast<void>(from_string(std::string_view(*name), out));
    }
}

/// ADL JSON writer (cold path only); see SpanKind::to_json.
inline void to_json(nlohmann::json& j, const SamplingDecision decision) {
    j = std::string(to_string(decision));
}
/// ADL JSON reader, strictly non-throwing; see SpanKind::from_json.
inline void from_json(const nlohmann::json& j, SamplingDecision& out) noexcept {
    const auto* const name = j.get_ptr<const nlohmann::json::string_t*>();
    if (name != nullptr) {
        static_cast<void>(from_string(std::string_view(*name), out));
    }
}

} // namespace nexigen::tracing

/// Span enums format as their canonical names (mandate 11): parse and
/// fill/align handled by the string_view formatter; contexts templated because
/// libstdc++ 15 evaluates formattability against a substituted context.
template <>
struct std::formatter<nexigen::tracing::SpanKind> : std::formatter<std::string_view> {
    template <class Ctx>
    constexpr auto format(const nexigen::tracing::SpanKind kind, Ctx& ctx) const {
        return std::formatter<std::string_view>::format(nexigen::tracing::to_string(kind), ctx);
    }
};

/// SpanStatus formats as its canonical name (mandate 11).
template <>
struct std::formatter<nexigen::tracing::SpanStatus> : std::formatter<std::string_view> {
    template <class Ctx>
    constexpr auto format(const nexigen::tracing::SpanStatus status, Ctx& ctx) const {
        return std::formatter<std::string_view>::format(nexigen::tracing::to_string(status), ctx);
    }
};

/// SamplingDecision formats as its canonical name (mandate 11).
template <>
struct std::formatter<nexigen::tracing::SamplingDecision> : std::formatter<std::string_view> {
    template <class Ctx>
    constexpr auto format(const nexigen::tracing::SamplingDecision decision, Ctx& ctx) const {
        return std::formatter<std::string_view>::format(nexigen::tracing::to_string(decision), ctx);
    }
};
