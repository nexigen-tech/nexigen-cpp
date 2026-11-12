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

/// @brief Strong trace identifier: the process-wide causal group of a span tree.
///
/// A trivially copyable wrapper over one 64-bit count (`sizeof == 8`). The
/// all-zero value is the "unset" sentinel, explicit values are preserved, and
/// ordering/equality compare the underlying counts. Generation runs on the
/// calling thread through the thread-local `detail::next_id()` source, so
/// creating an id never allocates, never locks, never blocks and never yields
/// zero; ids from different threads are distinct with overwhelming probability.
///
/// @note Spec `tracing/span-context`, requirement "Trace and span identifier
///       generation".
class TraceId {
public:
    /// @brief Construct the unset (all-zero) sentinel.
    constexpr TraceId() noexcept = default;

    /// @brief Wrap an explicit 64-bit value.
    ///
    /// Unit-bearing: `value` is the raw count, never parsed text.
    ///
    /// @param value Count to store; `0` selects the unset sentinel.
    explicit constexpr TraceId(std::uint64_t value) noexcept : value_{value} {}

    /// @brief Generate the next identifier on the calling thread.
    ///
    /// @return A non-zero id from the thread-local generator.
    /// @note Spec `tracing/span-context`, requirement "Trace and span identifier
    ///       generation": wait-free and allocation-free, never zero.
    [[nodiscard]] static TraceId generate() noexcept { return TraceId{detail::next_id()}; }

    /// @brief The raw 64-bit count.
    /// @return Stored count; `0` means unset.
    [[nodiscard]] constexpr std::uint64_t value() const noexcept { return value_; }

    /// @brief Whether the id is the zero "unset" sentinel.
    /// @return `true` when no id has been assigned.
    [[nodiscard]] constexpr bool is_unset() const noexcept { return value_ == 0ULL; }

    /// @brief Total ordering and equality over the underlying count.
    /// @return Strong comparison result of the two counts.
    [[nodiscard]] constexpr auto operator<=>(const TraceId&) const = default;

    /// @brief Cold-path diagnostic render: zero-padded 16-character lowercase hex.
    ///
    /// @return W3C-shaped textual id, e.g. `"00000000001a2b3c"`.
    /// @note Spec `tracing/record-format`, requirement "Identifier hex format is
    ///       fixed and round-trips".
    /// @note Mandate 9: allocates a `std::string` and is diagnostics/config/test
    ///       only — never a hot-path call. Body in `src/tracing/record_format.cpp`.
    [[nodiscard]] std::string to_string() const;

private:
    /// @brief The raw count backing the id; `0` is unset.
    std::uint64_t value_{};
};

/// @brief Strong span identifier: one node inside the trace's span tree.
///
/// The same 8-byte trivially copyable value shape as `TraceId`: the all-zero
/// value is the unset sentinel (zero is also the "no parent" marker in
/// `SpanRecord::parent_span_id`), and generation is thread-local, wait-free and
/// allocation-free.
///
/// @note Spec `tracing/span-context`, requirement "Trace and span identifier
///       generation".
class SpanId {
public:
    /// @brief Construct the unset (all-zero) sentinel.
    constexpr SpanId() noexcept = default;

    /// @brief Wrap an explicit 64-bit value.
    ///
    /// Unit-bearing: `value` is the raw count, never parsed text.
    ///
    /// @param value Count to store; `0` selects the unset sentinel.
    explicit constexpr SpanId(std::uint64_t value) noexcept : value_{value} {}

    /// @brief Generate the next identifier on the calling thread.
    ///
    /// @return A non-zero id from the thread-local generator.
    /// @note Spec `tracing/span-context`, requirement "Trace and span identifier
    ///       generation": wait-free and allocation-free, never zero.
    [[nodiscard]] static SpanId generate() noexcept { return SpanId{detail::next_id()}; }

    /// @brief The raw 64-bit count.
    /// @return Stored count; `0` means unset.
    [[nodiscard]] constexpr std::uint64_t value() const noexcept { return value_; }

    /// @brief Whether the id is the zero "unset" sentinel (root parent marker).
    /// @return `true` when no id has been assigned.
    [[nodiscard]] constexpr bool is_unset() const noexcept { return value_ == 0ULL; }

    /// @brief Total ordering and equality over the underlying count.
    /// @return Strong comparison result of the two counts.
    [[nodiscard]] constexpr auto operator<=>(const SpanId&) const = default;

    /// @brief Cold-path diagnostic render: zero-padded 16-character lowercase hex.
    ///
    /// @return W3C-shaped textual id, e.g. `"00000000001a2b3c"`.
    /// @note Spec `tracing/record-format`, requirement "Identifier hex format is
    ///       fixed and round-trips".
    /// @note Mandate 9: allocates a `std::string`; diagnostics only. Body in
    ///       `src/tracing/record_format.cpp`.
    [[nodiscard]] std::string to_string() const;

private:
    /// @brief The raw count backing the id; `0` is unset.
    std::uint64_t value_{};
};

static_assert(sizeof(TraceId) == 8 && std::is_trivially_copyable_v<TraceId>);
static_assert(sizeof(SpanId) == 8 && std::is_trivially_copyable_v<SpanId>);

/// @brief The propagable unit: enough state to link a child span to a live parent
///        across a thread or queue boundary.
///
/// Trivially copyable and ≤ 24 bytes, so a context fits bounded inter-thread
/// queues without allocation. `ScopedSpan::context()` mints one; the optional
/// first parameter of `Tracer::start_span` consumes one to override the
/// thread-local active span.
///
/// @note Spec `tracing/span-context`, requirement "Propagable span context".
struct SpanContext {
    TraceId trace_id{};                               ///< Causal group of the parent span.
    SpanId span_id{};                                 ///< The parent span itself (zero = unset).
    SamplingDecision sampled{SamplingDecision::Drop}; ///< Decision made at the parent's start.

    /// @brief Total ordering over the three fields, equality included.
    ///
    /// Field order is `trace_id`, then `span_id`, then `sampled`; a copied
    /// context compares equal to its original, which is the propagation
    /// contract.
    ///
    /// @return Strong comparison result of the three fields.
    [[nodiscard]] constexpr auto operator<=>(const SpanContext&) const = default;

    /// @brief Cold-path diagnostic render: both ids in hex plus the sampling decision.
    ///
    /// @return Human-readable context text for logs, config dumps and tests.
    /// @note Mandate 9: allocates; never called from the span hot path. Body in
    ///       `src/tracing/record_format.cpp`.
    [[nodiscard]] std::string to_string() const;
};

static_assert(sizeof(SpanContext) <= 24, "context must stay queue-friendly");
static_assert(std::is_trivially_copyable_v<SpanContext>);

/// @brief Free helper (mandate 9): mirrors `TraceId::to_string`.
///
/// @param id Identifier to render.
/// @return The same 16-character lowercase hex text as the member.
[[nodiscard]] std::string to_string(const TraceId& id);

/// @brief Free helper (mandate 9): mirrors `SpanId::to_string`.
///
/// @param id Identifier to render.
/// @return The same 16-character lowercase hex text as the member.
[[nodiscard]] std::string to_string(const SpanId& id);

/// @brief Free helper (mandate 9): mirrors `SpanContext::to_string`.
///
/// @param context Context to render.
/// @return The same diagnostic text as the member.
[[nodiscard]] std::string to_string(const SpanContext& context);

/// @brief JSON writer: the id as a zero-padded 16-character lowercase hex string.
///
/// @param j Destination document or field to overwrite.
/// @param id Identifier to serialize.
/// @note Spec `tracing/record-format`, requirement "Identifier hex format is
///       fixed and round-trips". Cold path only.
void to_json(nlohmann::json& j, const TraceId& id);

/// @brief Non-throwing JSON reader: accepts 1..16 hex digits of any case.
///
/// @param j JSON value to read.
/// @param out Destination; malformed or non-string input leaves it unchanged.
/// @note Spec `tracing/record-format`, requirement "Identifier hex format is
///       fixed and round-trips".
void from_json(const nlohmann::json& j, TraceId& out) noexcept;

/// @brief JSON writer: the id as a zero-padded 16-character lowercase hex string.
///
/// @param j Destination document or field to overwrite.
/// @param id Identifier to serialize.
/// @note Spec `tracing/record-format`, requirement "Identifier hex format is
///       fixed and round-trips". Cold path only.
void to_json(nlohmann::json& j, const SpanId& id);

/// @brief Non-throwing JSON reader: accepts 1..16 hex digits of any case.
///
/// @param j JSON value to read.
/// @param out Destination; malformed or non-string input leaves it unchanged.
/// @note Spec `tracing/record-format`, requirement "Identifier hex format is
///       fixed and round-trips".
void from_json(const nlohmann::json& j, SpanId& out) noexcept;

/// @brief JSON writer: the context as an object of its three fields.
///
/// `trace_id` and `span_id` render as hex strings, `sampled` as its canonical
/// decision name.
///
/// @param j Destination document or field to overwrite.
/// @param context Context to serialize.
/// @note Spec `tracing/record-format`, requirement "SpanContext JSON is
///       non-throwing and partial-field safe". Cold path only.
void to_json(nlohmann::json& j, const SpanContext& context);

/// @brief Non-throwing JSON reader with partial-field safety.
///
/// @param j JSON value to read.
/// @param out Destination; missing or wrong-typed fields keep their previous
///        values, and other fields are still updated.
/// @note Spec `tracing/record-format`, requirement "SpanContext JSON is
///       non-throwing and partial-field safe".
void from_json(const nlohmann::json& j, SpanContext& out) noexcept;

} // namespace nexigen::tracing

/// The identifier types format as their 16-char hex text (mandate 11);
/// contexts format as their to_string() text. Templated contexts (libstdc++
/// 15 substituted-formattability evaluation, same note as ErrorCode).
