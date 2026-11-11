// nexigen::tracing — thread-local trace context (change add-trace-propagation;
// spec tracing/trace-propagation; design D3/D5).
//
// The per-thread context holding the trace id and source span id extracted
// from the last received message. It links a subscriber's next span start (and
// the next published header) to the incoming trace. Persistent: it lives
// until the next receive overwrites it, so subsequent same-thread spans keep
// the same trace.
#pragma once

#include <format>
#include <string>

#include <nexigen/tracing/span_context.hpp>

namespace nexigen::tracing {

/// The causally-linking state carried across a thread between messages.
struct TraceContext {
    TraceId trace_id{};      ///< Inherited trace id (0 = none).
    SpanId source_span_id{}; ///< Parent span of the incoming message (0 = root).

    /// Cold-path diagnostic (mandate 9).
    [[nodiscard]] std::string to_string() const {
        return std::format("TraceContext(trace_id={}, source_span_id={})", trace_id.to_string(),
                           source_span_id.to_string());
    }
};

/// The current thread's trace context (thread_local, persistent).
[[nodiscard]] inline TraceContext& current_trace_context() noexcept {
    thread_local TraceContext context{};
    return context;
}

/// Free helper (mandate 9): mirrors the member.
[[nodiscard]] inline std::string to_string(const TraceContext& context) {
    return context.to_string();
}

} // namespace nexigen::tracing

/// The context formats as its diagnostic string (mandate 11).
template <>
struct std::formatter<nexigen::tracing::TraceContext> : std::formatter<std::string_view> {
    template <class Ctx>
    auto format(const nexigen::tracing::TraceContext& context, Ctx& ctx) const {
        const std::string text = context.to_string();
        return std::formatter<std::string_view>::format(text, ctx);
    }
};

// NEW_ROOT (design D5): start a fresh trace on this thread for a data source
// (camera driver, lidar, ...). The next span inherits the new trace id and a
// zero parent; the following outgoing message carries that id.
#define NEXIGEN_TRACE_NEW_ROOT(name)                                                               \
    do {                                                                                           \
        static_cast<void>(name);                                                                   \
        auto& nexigen_trace_ctx_ = ::nexigen::tracing::current_trace_context();                    \
        nexigen_trace_ctx_.trace_id = ::nexigen::tracing::TraceId::generate();                     \
        nexigen_trace_ctx_.source_span_id = ::nexigen::tracing::SpanId{};                          \
    } while (false)
