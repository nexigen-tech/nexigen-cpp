// nexigen::tracing — trace/span identifiers and the propagable context
// (design D6; spec tracing/span-context).
//
// TraceId and SpanId are strong trivially copyable wrappers over uint64_t:
// the all-zero value is the "unset" sentinel (default), explicit values are
// preserved, and ordering/equality compare the underlying counts. Both render
// as zero-padded 16-character lowercase hex (std::to_chars, base 16) in
// to_string/JSON — the W3C-compatible textual shape.
//
// SpanContext is the cross-thread propagation unit: {trace_id, span_id,
// sampled} — trivially copyable, ≤ 24 bytes, small enough to be pushed
// through bounded queues between threads to link parent-child spans across
// thread boundaries (within one process; no wire formats — design non-goal).
//
// The serialization trio (mandates 9–11) is declared here; the allocating
// cold bodies live in src/tracing/record_format.cpp (design D2) so the hot
// headers never pull std::string/nlohmann machinery.
#pragma once

#include <compare>
#include <cstdint>
#include <format>
#include <string>
#include <type_traits>

#include <nlohmann/json.hpp>

#include <nexigen/tracing/detail/id_generator.hpp>
#include <nexigen/tracing/span_enums.hpp>

namespace nexigen::tracing {

/// Strong trace identifier: the process-wide causal group of a span tree.
class TraceId {
public:
    constexpr TraceId() noexcept = default;

    /// Wraps an explicit value (unit-bearing: raw count, not text).
    explicit constexpr TraceId(std::uint64_t value) noexcept : value_{value} {}

    /// Generate the next identifier on THIS thread (design D6: the only id
    /// source; wait-free, allocation-free, never zero).
    [[nodiscard]] static TraceId generate() noexcept {
        return TraceId{detail::next_id()};
    }

    /// Raw 64-bit count.
    [[nodiscard]] constexpr std::uint64_t value() const noexcept { return value_; }

    /// True when the id is the zero "unset" sentinel.
    [[nodiscard]] constexpr bool is_unset() const noexcept { return value_ == 0ULL; }

    /// Total ordering on the count; == included.
    [[nodiscard]] constexpr auto operator<=>(const TraceId&) const = default;

    /// Cold-path diagnostic (mandate 9): zero-padded 16-char lowercase hex.
    /// Allocates a std::string — diagnostics/config/tests only, never a
    /// hot-path call. Body in src/tracing/record_format.cpp.
    [[nodiscard]] std::string to_string() const;

private:
    std::uint64_t value_{};
};

/// Strong span identifier: one node inside the trace's span tree.
class SpanId {
public:
    constexpr SpanId() noexcept = default;

    /// Wraps an explicit value (unit-bearing: raw count, not text).
    explicit constexpr SpanId(std::uint64_t value) noexcept : value_{value} {}

    /// Generate the next identifier on THIS thread (design D6).
    [[nodiscard]] static SpanId generate() noexcept {
        return SpanId{detail::next_id()};
    }

    /// Raw 64-bit count.
    [[nodiscard]] constexpr std::uint64_t value() const noexcept { return value_; }

    /// True when the id is the zero "unset" sentinel (root parent marker).
    [[nodiscard]] constexpr bool is_unset() const noexcept { return value_ == 0ULL; }

    /// Total ordering on the count; == included.
    [[nodiscard]] constexpr auto operator<=>(const SpanId&) const = default;

    /// Cold-path diagnostic (mandate 9): 16-char lowercase hex. Body in
    /// src/tracing/record_format.cpp.
    [[nodiscard]] std::string to_string() const;

private:
    std::uint64_t value_{};
};

static_assert(sizeof(TraceId) == 8 && std::is_trivially_copyable_v<TraceId>);
static_assert(sizeof(SpanId) == 8 && std::is_trivially_copyable_v<SpanId>);

/// The propagable unit: enough state to link a child span to a live parent
/// across a thread/queue boundary. Trivially copyable, ≤ 24 bytes.
struct SpanContext {
    TraceId trace_id{};                    ///< Causal group of the parent span.
    SpanId span_id{};                      ///< The parent span itself (zero = unset).
    SamplingDecision sampled{SamplingDecision::Drop}; ///< Decision made at the parent's start.

    /// Total ordering over the three fields; == included (cross-thread copy
    /// equality is the propagation contract).
    [[nodiscard]] constexpr auto operator<=>(const SpanContext&) const = default;

    /// Cold-path diagnostic (mandate 9): both ids in hex plus the sampling
    /// decision. Body in src/tracing/record_format.cpp.
    [[nodiscard]] std::string to_string() const;
};

static_assert(sizeof(SpanContext) <= 24, "context must stay queue-friendly");
static_assert(std::is_trivially_copyable_v<SpanContext>);

/// Free helpers (mandate 9): mirror the members.
[[nodiscard]] std::string to_string(const TraceId& id);
[[nodiscard]] std::string to_string(const SpanId& id);
[[nodiscard]] std::string to_string(const SpanContext& context);

/// ADL JSON (cold path): ids as zero-padded 16-char lowercase hex strings;
/// the context as an object of its three fields. Non-throwing readers under
/// JSON_NOEXCEPTION=1 — wrong-typed input leaves values unchanged. Bodies in
/// src/tracing/record_format.cpp.
void to_json(nlohmann::json& j, const TraceId& id);
void from_json(const nlohmann::json& j, TraceId& out) noexcept;
void to_json(nlohmann::json& j, const SpanId& id);
void from_json(const nlohmann::json& j, SpanId& out) noexcept;
void to_json(nlohmann::json& j, const SpanContext& context);
void from_json(const nlohmann::json& j, SpanContext& out) noexcept;

} // namespace nexigen::tracing

/// The identifier types format as their 16-char hex text (mandate 11);
/// contexts format as their to_string() text. Templated contexts (libstdc++
/// 15 substituted-formattability evaluation, same note as ErrorCode).
template <>
struct std::formatter<nexigen::tracing::TraceId> : std::formatter<std::string_view> {
    template <class Ctx>
    auto format(const nexigen::tracing::TraceId& id, Ctx& ctx) const {
        const std::string text = id.to_string();
        return std::formatter<std::string_view>::format(text, ctx);
    }
};
template <>
struct std::formatter<nexigen::tracing::SpanId> : std::formatter<std::string_view> {
    template <class Ctx>
    auto format(const nexigen::tracing::SpanId& id, Ctx& ctx) const {
        const std::string text = id.to_string();
        return std::formatter<std::string_view>::format(text, ctx);
    }
};
template <>
struct std::formatter<nexigen::tracing::SpanContext> : std::formatter<std::string_view> {
    template <class Ctx>
    auto format(const nexigen::tracing::SpanContext& context, Ctx& ctx) const {
        const std::string text = context.to_string();
        return std::formatter<std::string_view>::format(text, ctx);
    }
};
