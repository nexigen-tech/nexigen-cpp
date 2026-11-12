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

/// @brief The causally-linking state carried across a thread between messages.
///
/// Holds the trace id and source span id extracted from the last received
/// message. It is persistent rather than consumed: it lives until the next
/// receive overwrites it, so every subsequent same-thread span keeps the same
/// trace. All fields default to zero (no inherited trace, root parent).
///
/// @note Spec `tracing/trace-propagation`, requirement "Thread-local trace
///       context links processes"; design D3/D5. Plain thread-confined value
///       type; the accessor's instance is `thread_local`.
struct TraceContext {
    TraceId trace_id{};      ///< Inherited trace id (0 = none).
    SpanId source_span_id{}; ///< Parent span of the incoming message (0 = root).

    /// @brief Render the context for diagnostics.
    ///
    /// Cold path only; allocates a `std::string` and is never called from the
    /// recording path.
    ///
    /// @return Text of the form `TraceContext(trace_id=..., source_span_id=...)`
    ///         with both ids in 16-character lowercase hex.
    /// @note Mandate 9 (diagnostic `to_string`); spec `tracing/trace-propagation`,
    ///       requirement "TraceHeader and TraceContext diagnostics".
    [[nodiscard]] std::string to_string() const {
        return std::format("TraceContext(trace_id={}, source_span_id={})", trace_id.to_string(),
                           source_span_id.to_string());
    }
};

/// @brief The calling thread's trace context accessor.
///
/// Function-local `thread_local`: created zero-initialized on first use and
/// alive for the thread. Every receive, span start, and publish on that thread
/// reads or writes this single instance.
///
/// @return Reference to the calling thread's persistent `TraceContext`.
/// @note Spec `tracing/trace-propagation`, requirement "Thread-local trace
///       context links processes". No atomics: the object is thread-confined.
[[nodiscard]] inline TraceContext& current_trace_context() noexcept {
    thread_local TraceContext context{};
    return context;
}

/// @brief Free-helper mirror of `TraceContext::to_string`.
///
/// @param context Context to render.
/// @return The same text produced by the member function.
/// @note Mandate 9; spec `tracing/trace-propagation`, requirement "TraceHeader
///       and TraceContext diagnostics". Cold path.
[[nodiscard]] inline std::string to_string(const TraceContext& context) {
    return context.to_string();
}

} // namespace nexigen::tracing

/// @brief `std::formatter` for `TraceContext`.
///
/// Delegates to `TraceContext::to_string()` and formats the resulting text as
/// a `std::string_view`, so `std::format("{}", context)` equals the member
/// diagnostic.
///
/// @note Mandate 11; spec `tracing/trace-propagation`, requirement "TraceHeader
///       and TraceContext diagnostics". Cold path only.
template <>
struct std::formatter<nexigen::tracing::TraceContext> : std::formatter<std::string_view> {
    /// @brief Format one context into the output iterator range.
    ///
    /// @tparam Ctx `std::basic_format_context` instantiation supplied by the
    ///         formatting machinery.
    /// @param context Context to render.
    /// @param ctx Output context receiving the rendered text.
    /// @return Iterator past the last written character.
    /// @note Mandate 11.
    template <class Ctx>
    auto format(const nexigen::tracing::TraceContext& context, Ctx& ctx) const {
        const std::string text = context.to_string();
        return std::formatter<std::string_view>::format(text, ctx);
    }
};

/// @brief Start a fresh trace on this thread for a data source.
///
/// Generates a new `TraceId` into the calling thread's `TraceContext` and
/// clears `source_span_id`, so the next span inherits the new trace id with a
/// zero parent and the following outgoing message carries that id.
///
/// @param name Source name; evaluated and discarded (accepted for call-site
///        readability and tooling association).
/// @note Spec `tracing/trace-propagation`, requirement "NEW_ROOT starts a fresh
///       trace"; design D5. Cold path: one id generation, no allocation.
#define NEXIGEN_TRACE_NEW_ROOT(name)                                                               \
    do {                                                                                           \
        static_cast<void>(name);                                                                   \
        auto& nexigen_trace_ctx_ = ::nexigen::tracing::current_trace_context();                    \
        nexigen_trace_ctx_.trace_id = ::nexigen::tracing::TraceId::generate();                     \
        nexigen_trace_ctx_.source_span_id = ::nexigen::tracing::SpanId{};                          \
    } while (false)
