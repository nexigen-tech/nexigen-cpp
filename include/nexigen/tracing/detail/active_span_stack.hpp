// nexigen::tracing — fixed-depth thread-local active-span stack (design D7;
// spec tracing/span-context "Automatic intra-thread parent tracking").
//
// One stack per recording thread, shared by every provider on that thread
// (causal linkage is a property of the CODE the thread runs, not of the
// provider that records it). Fixed 16-frame array + plain depth byte: the
// stack never grows dynamically.
//
// INDEX-TRUNCATION SEMANTICS (design D7): a span remembers the index its
// frame was pushed at; ending truncates the stack to that index — removing
// the frame AND any frames above it. This makes an explicit end() while
// children are still active well-defined (their frames are discarded early;
// their later truncations clamp to no-ops) instead of corrupting LIFO.
// Over-deep spans (depth == max_span_depth) never push: the caller records
// them as bounded roots; their truncation index is the sentinel
// max_span_depth, a structural no-op.
//
// The stack is written ONLY by the owning thread (thread_local by
// construction); no atomics anywhere on this path.
#pragma once

#include <cstdint>

#include <nexigen/tracing/span_context.hpp>

namespace nexigen::tracing::detail {

/// @brief Fixed active-span stack depth, in frames (design D7).
///
/// Also doubles as the "bounded root" sentinel returned by
/// `ActiveSpanStack::push`: an over-deep span is recorded as a root and never
/// occupies a frame.
///
/// @note Spec `tracing/span-context`, requirement "Automatic intra-thread
///       parent tracking": the stack never grows dynamically.
inline constexpr std::uint8_t max_span_depth = 16;

/// @brief One active-span frame: enough to parent the next same-thread span.
///
/// Trivially copyable value type stored inline in the thread's fixed frame
/// array. Holds the trace the thread is currently executing and the id of the
/// span itself, so a nested span can inherit both without touching the
/// provider. Confined to the owning thread's stack and never shared.
///
/// @note Spec `tracing/span-context`, requirement "Automatic intra-thread
///       parent tracking".
struct ActiveFrame {
    TraceId trace_id{}; ///< Trace inherited by children of this frame.
    SpanId span_id{};   ///< Span id children record as `parent_span_id`.

    /// @brief Field-wise equality.
    ///
    /// The comparison is defaulted and member-wise over the two ids; the
    /// parameter is unnamed by design.
    ///
    /// @return `true` when both ids match.
    [[nodiscard]] constexpr bool operator==(const ActiveFrame&) const = default;
};

/// @brief The owning thread's active-span stack (fixed storage, index
/// truncation).
///
/// One instance per recording thread, shared by every provider on that thread:
/// causal linkage is a property of the code the thread runs, not of the
/// provider recording it. Storage is a fixed 16-frame array plus a plain depth
/// byte, so neither push nor truncate allocates or grows the stack.
///
/// Written only by the owning thread (the instance is `thread_local`); no
/// atomics, locks, or memory orders participate on this hot path.
///
/// @note Spec `tracing/span-context`, requirement "Automatic intra-thread
///       parent tracking"; design D7 index-truncation semantics.
class ActiveSpanStack {
public:
    /// @brief Current stack top.
    ///
    /// @return Pointer to the innermost active frame, or `nullptr` when the
    ///         stack is empty (the next span is a root).
    [[nodiscard]] const ActiveFrame* top() const noexcept {
        return depth_ == 0U ? nullptr : &frames_[static_cast<std::size_t>(depth_) - 1U];
    }

    /// @brief Current stack depth.
    ///
    /// @return Number of stored frames in `[0, max_span_depth]`.
    [[nodiscard]] std::uint8_t depth() const noexcept { return depth_; }

    /// @brief Push a frame and return the index its caller must later
    /// truncate to.
    ///
    /// When the stack is already full the span is not stored: the sentinel
    /// `max_span_depth` is returned so the caller records a bounded root whose
    /// later truncation is a structural no-op.
    ///
    /// @param frame Frame carrying the span's trace id and span id.
    /// @return Index in `[0, max_span_depth - 1]` on success, or the sentinel
    ///         `max_span_depth` when the stack is full and nothing was stored.
    /// @note Spec `tracing/span-context`, requirement "Automatic intra-thread
    ///       parent tracking"; design D7.
    std::uint8_t push(const ActiveFrame& frame) noexcept {
        if (depth_ >= max_span_depth) {
            return max_span_depth; // sentinel: bounded root, no push
        }
        frames_[static_cast<std::size_t>(depth_)] = frame;
        return depth_++;
    }

    /// @brief Truncate the stack to `index`, discarding that frame and all
    /// frames above it.
    ///
    /// Makes an explicit span end while children are still active
    /// well-defined: the children's frames are dropped early and their own
    /// later truncations clamp to no-ops. A shallow stack makes the call a
    /// no-op.
    ///
    /// @param index Truncation index previously returned by `push`.
    /// @note Spec `tracing/span-context`, requirement "Automatic intra-thread
    ///       parent tracking"; design D7 index-truncation semantics.
    void truncate_to(std::uint8_t index) noexcept {
        if (index < depth_) {
            depth_ = index;
        }
    }

    /// @brief Drop every frame.
    ///
    /// Diagnostic and test reset only; affects the owning thread's stack
    /// exclusively.
    void clear() noexcept { depth_ = 0U; }

private:
    ActiveFrame frames_[max_span_depth]{}; ///< Fixed frame storage, oldest first.
    std::uint8_t depth_{0};                ///< Number of live frames.
};

/// @brief The calling thread's own active-span stack accessor.
///
/// Function-local `thread_local`: the stack is created on first use, lives for
/// the lifetime of the thread, and is never shared across threads.
///
/// @return Reference to the calling thread's stack instance.
/// @note Spec `tracing/span-context`, requirement "Automatic intra-thread
///       parent tracking". No allocation or locking after the first call on a
///       thread.
[[nodiscard]] inline ActiveSpanStack& active_span_stack() noexcept {
    thread_local ActiveSpanStack stack{};
    return stack;
}

} // namespace nexigen::tracing::detail
