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

/// Fixed stack depth (design D7).
inline constexpr std::uint8_t max_span_depth = 16;

/// One active-span frame: enough to parent the next same-thread span.
struct Active_frame {
    TraceId trace_id{};
    SpanId span_id{};

    /// Field-wise equality (propagation comparisons; == included).
    [[nodiscard]] constexpr bool operator==(const Active_frame&) const = default;
};

/// The owning thread's active-span stack (fixed storage, index truncation).
class Active_span_stack {
public:
    /// Current stack top; nullptr when empty (the next span is a root).
    [[nodiscard]] const Active_frame* top() const noexcept {
        return depth_ == 0U ? nullptr : &frames_[static_cast<std::size_t>(depth_) - 1U];
    }

    /// Current depth (0..max_span_depth).
    [[nodiscard]] std::uint8_t depth() const noexcept { return depth_; }

    /// Push a frame; returns its index (0..max_span_depth-1), or the sentinel
    /// max_span_depth when the stack is full — the caller then records the
    /// span as a bounded root and no frame is stored.
    std::uint8_t push(const Active_frame& frame) noexcept {
        if (depth_ >= max_span_depth) {
            return max_span_depth; // sentinel: bounded root, no push
        }
        frames_[static_cast<std::size_t>(depth_)] = frame;
        return depth_++;
    }

    /// Truncate to `index`: the frame at `index` and everything above it are
    /// removed; clamps to a no-op when the stack is already shallower (the
    /// children-ended-first case of design D7).
    void truncate_to(std::uint8_t index) noexcept {
        if (index < depth_) {
            depth_ = index;
        }
    }

    /// Drop everything (test/diagnostic reset for the owning thread only).
    void clear() noexcept { depth_ = 0U; }

private:
    Active_frame frames_[max_span_depth]{};
    std::uint8_t depth_{0};
};

/// The thread's own stack (function-local thread_local: created on first
/// use, alive for the thread, never a cross-thread object).
[[nodiscard]] inline Active_span_stack& active_span_stack() noexcept {
    thread_local Active_span_stack stack{};
    return stack;
}

} // namespace nexigen::tracing::detail
