// nexigen::diagnostics — borrowed-slot sink for ScopedStopwatch (design D7).
#pragma once

#include <cstdint>

namespace nexigen::diagnostics::detail {

struct OutRefSink {
    std::uint64_t* out; ///< Borrowed destination slot (never null in practice).

    constexpr explicit OutRefSink(std::uint64_t* slot) noexcept : out{slot} {}

    void operator()(std::uint64_t elapsed_ns) const noexcept {
        if (out != nullptr) {
            *out = elapsed_ns;
        }
    }
};

} // namespace nexigen::diagnostics::detail
