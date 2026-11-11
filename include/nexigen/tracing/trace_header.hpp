// nexigen::tracing — cross-process trace propagation header (change
// add-trace-propagation; spec tracing/trace-propagation).
//
// The 32-byte, pointer-free custom header that rides on every iceoryx2
// publish-subscribe sample (TRCC_004): the causally-linking trace id, the
// source span id, the publish timestamp, and a per-publisher sequence number.
// Trivially copyable, so the transport treats it as a safe custom header.
#pragma once

#include <cstdint>
#include <format>
#include <string>

#include <nexigen/chrono/timestamp.hpp>
#include <nexigen/com/concepts/message_concept.hpp>
#include <nexigen/tracing/detail/active_span_stack.hpp>
#include <nexigen/tracing/detail/trace_context.hpp>
#include <nexigen/tracing/span_context.hpp>

namespace nexigen::tracing {

/// The propagation header attached to a published sample.
struct alignas(8) TraceHeader {
    TraceId trace_id{};                    ///< Causal group (0 = unset).
    SpanId source_span_id{};               ///< The publishing span (0 = root).
    std::uint64_t publish_timestamp_ns{0}; ///< Publish wall/monotonic stamp (ns).
    std::uint32_t sequence_number{0};      ///< Per-publisher monotonic sequence.

    /// Cold-path diagnostic (mandate 9).
    [[nodiscard]] std::string to_string() const;
};

static_assert(sizeof(TraceHeader) == 32, "TraceHeader must stay 32 bytes");
static_assert(alignof(TraceHeader) == 8, "TraceHeader must be 8-byte aligned");
static_assert(std::is_trivially_copyable_v<TraceHeader>, "TraceHeader must be trivially copyable");
static_assert(com::concepts::Message<TraceHeader>, "TraceHeader must satisfy the Message contract");

/// Free helper (mandate 9): mirrors the member.
[[nodiscard]] inline std::string to_string(const TraceHeader& header) {
    return header.to_string();
}

/// Build the header a publisher attaches before send() (design D6, caller
/// side): the trace id comes from the thread's TraceContext, the source span
/// id from the active span (zero when no span is open), and the timestamp
/// from the monotonic clock. `sequence_number` stays caller-managed (a
/// per-publisher counter).
[[nodiscard]] inline TraceHeader make_trace_header(std::uint32_t sequence_number) noexcept {
    TraceHeader header{};
    header.trace_id = current_trace_context().trace_id;
    if (const detail::ActiveFrame* top = detail::active_span_stack().top(); top != nullptr) {
        header.source_span_id = top->span_id;
    }
    const auto now = chrono::Timestamp::now();
    header.publish_timestamp_ns = now.has_value() ? now->nanoseconds() : 0U;
    header.sequence_number = sequence_number;
    return header;
}

} // namespace nexigen::tracing

/// The header formats as its diagnostic string (mandate 11).
template <>
struct std::formatter<nexigen::tracing::TraceHeader> : std::formatter<std::string_view> {
    template <class Ctx> auto format(const nexigen::tracing::TraceHeader& header, Ctx& ctx) const {
        const std::string text = header.to_string();
        return std::formatter<std::string_view>::format(text, ctx);
    }
};
