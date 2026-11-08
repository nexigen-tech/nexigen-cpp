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

struct BoundedSink {
    using value_type = char;
    using difference_type = std::ptrdiff_t;
    using reference = char&;
    using iterator_category = std::output_iterator_tag;

    char* p{nullptr};
    std::size_t left{0};
    char spill{};

    [[nodiscard]] char& operator*() noexcept { return left > 0U ? *p : spill; }
    BoundedSink& operator++() noexcept {
        if (left > 0U) {
            ++p;
            --left;
        }
        return *this;
    }
    BoundedSink operator++(int) noexcept {
        const BoundedSink copy = *this;
        ++*this;
        return copy;
    }
};

} // namespace nexigen::logging::detail
