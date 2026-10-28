// nexigen::metrics — policy vocabulary shared by every instrument (design D4;
// spec metrics/policy-enums).
//
// Four scoped enums classify how an instrument behaves:
//   ConcurrencyPolicy — single recording thread or sharded multi-threaded;
//   AccessPolicy      — the read side is public (Readable) or hidden behind
//                       the provider (WriteOnly, compile-time gated);
//   ResetPolicy       — all-time totals (Cumulative) or per-window deltas
//                       (ResetOnRead);
//   MetricKind        — which instrument family a slot holds.
//
// EVERY enum follows the project-wide serialization contract (mandates 9-11):
//   - constexpr to_string / from_string with canonical spellings;
//   - non-throwing ADL to_json / from_json (JSON_NOEXCEPTION=1, -fno-exceptions);
//   - a std::formatter specialization (templated context — libstdc++ 15
//     evaluates formattability against a substituted context, so a hard-coded
//     std::format_context& would not bind).
//
// Numeric values are append-only: existing enumerators keep their value, so
// recompiled and non-recompiled translation units stay compatible.
#pragma once

#include <cstdint>
#include <format>
#include <string>
#include <string_view>

#include <nlohmann/json.hpp>

namespace nexigen::metrics {

/// How an instrument is written: by one thread, or sharded across threads.
enum class ConcurrencyPolicy : std::uint8_t {
    SingleThreaded,
    MultiThreaded,
};

/// Whether the read side (.read() / .snapshot()) is part of the public API.
enum class AccessPolicy : std::uint8_t {
    Readable,
    WriteOnly,
};

/// Whether observations span the instrument's lifetime or the last window.
enum class ResetPolicy : std::uint8_t {
    Cumulative,
    ResetOnRead,
};

/// Which instrument family a registry slot holds.
enum class MetricKind : std::uint8_t {
    Counter,
    Gauge,
    DescriptiveStatistics,
    ExplicitHistogram,
    ExponentialHistogram,
};

/// Canonical enumerator spelling. The default arm makes a garbage-cast value
/// well-defined ("Unknown"); every enumerator is pinned by static_assert in the
/// test TU, compensating for the lost -Wswitch coverage.
[[nodiscard]] constexpr std::string_view to_string(ConcurrencyPolicy policy) noexcept {
    switch (policy) {
        case ConcurrencyPolicy::SingleThreaded: return "SingleThreaded";
        case ConcurrencyPolicy::MultiThreaded:  return "MultiThreaded";
        default:                                return "Unknown";
    }
}

[[nodiscard]] constexpr std::string_view to_string(AccessPolicy policy) noexcept {
    switch (policy) {
        case AccessPolicy::Readable: return "Readable";
        case AccessPolicy::WriteOnly: return "WriteOnly";
        default:                      return "Unknown";
    }
}

[[nodiscard]] constexpr std::string_view to_string(ResetPolicy policy) noexcept {
    switch (policy) {
        case ResetPolicy::Cumulative:  return "Cumulative";
        case ResetPolicy::ResetOnRead: return "ResetOnRead";
        default:                       return "Unknown";
    }
}

[[nodiscard]] constexpr std::string_view to_string(MetricKind kind) noexcept {
    switch (kind) {
        case MetricKind::Counter:                return "Counter";
        case MetricKind::Gauge:                  return "Gauge";
        case MetricKind::DescriptiveStatistics:  return "DescriptiveStatistics";
        case MetricKind::ExplicitHistogram:      return "ExplicitHistogram";
        case MetricKind::ExponentialHistogram:   return "ExponentialHistogram";
        default:                                return "Unknown";
    }
}

/// Parse a canonical enumerator name. Returns false leaving the
/// out-parameter UNCHANGED on any other spelling — including "Unknown",
/// which is a rendering, not an enumerator.
[[nodiscard]] constexpr bool from_string(std::string_view name, ConcurrencyPolicy& out) noexcept {
    if (name == "SingleThreaded") { out = ConcurrencyPolicy::SingleThreaded; return true; }
    if (name == "MultiThreaded")  { out = ConcurrencyPolicy::MultiThreaded;  return true; }
    return false;
}

[[nodiscard]] constexpr bool from_string(std::string_view name, AccessPolicy& out) noexcept {
    if (name == "Readable") { out = AccessPolicy::Readable; return true; }
    if (name == "WriteOnly") { out = AccessPolicy::WriteOnly; return true; }
    return false;
}

[[nodiscard]] constexpr bool from_string(std::string_view name, ResetPolicy& out) noexcept {
    if (name == "Cumulative")  { out = ResetPolicy::Cumulative;  return true; }
    if (name == "ResetOnRead") { out = ResetPolicy::ResetOnRead; return true; }
    return false;
}

[[nodiscard]] constexpr bool from_string(std::string_view name, MetricKind& out) noexcept {
    if (name == "Counter")               { out = MetricKind::Counter;               return true; }
    if (name == "Gauge")                 { out = MetricKind::Gauge;                 return true; }
    if (name == "DescriptiveStatistics") { out = MetricKind::DescriptiveStatistics; return true; }
    if (name == "ExplicitHistogram")     { out = MetricKind::ExplicitHistogram;     return true; }
    if (name == "ExponentialHistogram")  { out = MetricKind::ExponentialHistogram;  return true; }
    return false;
}

// --- nlohmann::json (cold path only — never on a hot path): the JSON value is
// exactly to_string(...). from_json is strictly non-throwing under
// JSON_NOEXCEPTION=1: get_ptr yields nullptr for any non-string value, an
// unknown name leaves `out` unchanged. Never aborts regardless of input.

inline void to_json(nlohmann::json& j, const ConcurrencyPolicy policy) {
    j = std::string(to_string(policy));
}

inline void to_json(nlohmann::json& j, const AccessPolicy policy) {
    j = std::string(to_string(policy));
}

inline void to_json(nlohmann::json& j, const ResetPolicy policy) {
    j = std::string(to_string(policy));
}

inline void to_json(nlohmann::json& j, const MetricKind kind) {
    j = std::string(to_string(kind));
}

inline void from_json(const nlohmann::json& j, ConcurrencyPolicy& out) noexcept {
    const auto* const name = j.get_ptr<const nlohmann::json::string_t*>();
    if (name != nullptr) {
        static_cast<void>(from_string(std::string_view(*name), out));
    }
}

inline void from_json(const nlohmann::json& j, AccessPolicy& out) noexcept {
    const auto* const name = j.get_ptr<const nlohmann::json::string_t*>();
    if (name != nullptr) {
        static_cast<void>(from_string(std::string_view(*name), out));
    }
}

inline void from_json(const nlohmann::json& j, ResetPolicy& out) noexcept {
    const auto* const name = j.get_ptr<const nlohmann::json::string_t*>();
    if (name != nullptr) {
        static_cast<void>(from_string(std::string_view(*name), out));
    }
}

inline void from_json(const nlohmann::json& j, MetricKind& out) noexcept {
    const auto* const name = j.get_ptr<const nlohmann::json::string_t*>();
    if (name != nullptr) {
        static_cast<void>(from_string(std::string_view(*name), out));
    }
}

} // namespace nexigen::metrics

/// Policy enums format as their canonical names (mandate 11): the parse and
/// fill/align handling is delegated to the string-view formatter.
template <>
struct std::formatter<nexigen::metrics::ConcurrencyPolicy> : std::formatter<std::string_view> {
    template <class Ctx>
    constexpr auto format(const nexigen::metrics::ConcurrencyPolicy policy, Ctx& ctx) const {
        return std::formatter<std::string_view>::format(nexigen::metrics::to_string(policy), ctx);
    }
};

template <>
struct std::formatter<nexigen::metrics::AccessPolicy> : std::formatter<std::string_view> {
    template <class Ctx>
    constexpr auto format(const nexigen::metrics::AccessPolicy policy, Ctx& ctx) const {
        return std::formatter<std::string_view>::format(nexigen::metrics::to_string(policy), ctx);
    }
};

template <>
struct std::formatter<nexigen::metrics::ResetPolicy> : std::formatter<std::string_view> {
    template <class Ctx>
    constexpr auto format(const nexigen::metrics::ResetPolicy policy, Ctx& ctx) const {
        return std::formatter<std::string_view>::format(nexigen::metrics::to_string(policy), ctx);
    }
};

template <>
struct std::formatter<nexigen::metrics::MetricKind> : std::formatter<std::string_view> {
    template <class Ctx>
    constexpr auto format(const nexigen::metrics::MetricKind kind, Ctx& ctx) const {
        return std::formatter<std::string_view>::format(nexigen::metrics::to_string(kind), ctx);
    }
};
