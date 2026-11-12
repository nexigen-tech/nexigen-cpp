// nexigen::logging — bounded output adapter for std::vformat_to (spec
// logging/log-record "Deferred type-erased argument formatting").
//
// The checked/bounded std::format_to overload needs a compile-time
// format_string; the record renderer holds a runtime pointer, so writes flow
// through this sink: bytes up to `left` land in the caller buffer, the rest
// are discarded into a one-byte spill. Never allocates.
#pragma once

#include <cstddef>
#include <iterator>

namespace nexigen::logging::detail {

/// @brief Output-iterator adapter that saturates at the end of a caller buffer.
///
/// `std::vformat_to` writes through `operator*`/`operator++`; bytes while
/// `left > 0` land in the caller buffer, every later byte is discarded into a
/// one-byte `spill`, so the cursor saturates at the buffer end and the caller
/// can flag truncation from the returned length. Used on the consumer thread
/// only, with `p`/`left` bound to pre-allocated record scratch.
///
/// @note Spec `logging/log-record`, requirement "Deferred type-erased argument
///       formatting"; overflow is silent by design, and `render_into` flags it
///       when the written length reaches the scratch capacity.
struct BoundedSink {
    /// @brief Byte type written through the sink.
    using value_type = char;
    /// @brief Iterator difference type required by the output-iterator traits.
    using difference_type = std::ptrdiff_t;
    /// @brief Reference yielded by `operator*`: a writable byte slot.
    using reference = char&;
    /// @brief Tags the adapter as an output iterator for `std::vformat_to`.
    using iterator_category = std::output_iterator_tag;

    /// @brief Destination cursor; advanced only while `left` bytes remain.
    char* p{nullptr};
    /// @brief Writable bytes left at `p`.
    std::size_t left{0};
    /// @brief Discard slot that absorbs writes once the buffer is full.
    char spill{};

    /// @brief Return the next write slot.
    ///
    /// @return The buffer position while capacity remains, otherwise `spill`.
    [[nodiscard]] char& operator*() noexcept { return left > 0U ? *p : spill; }
    /// @brief Advance to the next slot, or stay on `spill` once full.
    ///
    /// @return This sink, for iterator chaining.
    BoundedSink& operator++() noexcept {
        if (left > 0U) {
            ++p;
            --left;
        }
        return *this;
    }
    /// @brief Post-increment; returns a copy of the pre-increment cursor.
    ///
    /// @return The sink state before the advance.
    BoundedSink operator++(int) noexcept {
        const BoundedSink copy = *this;
        ++*this;
        return copy;
    }
};

} // namespace nexigen::logging::detail
