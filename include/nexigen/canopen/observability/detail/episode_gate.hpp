// nexigen::canopen::observability::detail — fixed error-episode slot (change
// add-canopen-observability; design D5).
#pragma once

#include <cstdint>
#include <type_traits>

namespace nexigen::canopen::observability::detail {

/// @brief State of one `(node, domain)` error episode.
///
/// The layout is part of the contract: 40 bytes, 8-byte aligned, plain
/// non-atomic fields. The runtime thread is the only reader and writer
/// (`on_error`, `poll_recovery`, and the summary run on the same thread), so no
/// cache-line padding or atomic is needed. A later control-thread design must
/// snapshot the array under explicit synchronization instead of reading it
/// concurrently.
///
/// @note Spec `canopen/observability`, requirement "Error tracking is
///       fixed-size"; design D5.
struct alignas(8) EpisodeGate {
    std::uint64_t last_error_ns;    ///< Time of the most recent occurrence.
    std::uint64_t last_log_ns;      ///< Time of the last repeat record.
    std::uint64_t episode_start_ns; ///< Time the current episode opened.
    std::uint32_t suppressed;       ///< Occurrences since the last emitted record.
    std::uint32_t last_code;        ///< Most recent error code.
    std::uint8_t active;            ///< `1` while an episode is open.
    std::uint8_t logged;            ///< `1` once the episode emitted at least one record.
    std::uint8_t reserved[6];       ///< Keeps the slot at 40 bytes.
};

static_assert(sizeof(EpisodeGate) == 40, "EpisodeGate must stay at 40 bytes");
static_assert(alignof(EpisodeGate) == 8, "EpisodeGate must stay 8-byte aligned");
static_assert(std::is_trivially_copyable_v<EpisodeGate>,
              "EpisodeGate must be a plain trivially copyable value");

} // namespace nexigen::canopen::observability::detail
