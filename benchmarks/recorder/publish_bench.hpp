// nexigen storage-format benchmark — iceoryx2 publish-path header measurement.
#pragma once

#include <cstdint>

#include <nexigen/core/result.hpp>

#include "metrics.hpp"

namespace nexigen::bench {

/// @brief Publish-path measurements for the SampleHeader decision.
struct PublishResult {
    LatencyStats send_copy{};
    LatencyStats loan_with_header{};
    LatencyStats producer_try_push{};
    std::int64_t heap_delta_bytes{0};
    bool ok{false};
};

/// @brief Measure send_copy vs loan/write/header/send and the producer enqueue.
[[nodiscard]] core::Result<PublishResult> run_publish_bench() noexcept;

} // namespace nexigen::bench
