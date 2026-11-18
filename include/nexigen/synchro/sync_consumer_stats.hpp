// nexigen::synchro — consumer statistics
// (change add-synchro-event-sync; spec synchro/consumer).
//
// Counters are plain values updated only on the consumer's dispatch thread (the
// runner thread or the caller of drain()). Read them from that thread or after
// the run.
#pragma once

#include <cstdint>
#include <format>
#include <string>
#include <string_view>

#include <nlohmann/json.hpp>

namespace nexigen::synchro {

/// @brief Counters and timing snapshots of one consumer lifecycle.
///
/// Each loss cause has its own counter so transport loss, duplicates, producer
/// restarts, and generator skips never mask one another.
///
/// @note Spec `synchro/consumer`, requirement "Consumer observability".
struct SyncConsumerStats {
    std::uint64_t received{0};           ///< Accepted (possibly late) events.
    std::uint64_t dropped_late{0};       ///< Late events suppressed by policy.
    std::uint64_t late_delivered{0};     ///< Late events delivered with the flag.
    std::uint64_t transport_lost{0};     ///< Samples the transport reported missing.
    std::uint64_t generator_skips{0};    ///< Phase slots the generator skipped.
    std::uint64_t duplicates{0};         ///< Duplicate transport samples dropped.
    std::uint64_t unexpected_resets{0};  ///< Sequence resets without an epoch change.
    std::uint64_t producer_restarts{0};  ///< Producer epoch changes.
    std::uint64_t resyncs{0};            ///< Logical continuity resynchronizations.
    std::uint64_t sync_losses{0};        ///< Watchdog expiries.
    std::int64_t last_ipc_latency_ns{0}; ///< Last publication-to-receive latency.
    std::int64_t last_e2e_latency_ns{0}; ///< Last scheduled-to-receive latency.
    std::uint64_t max_e2e_latency_ns{0}; ///< Largest end-to-end latency observed.
    std::uint64_t last_sequence{0};      ///< Transport sequence of the last event.
    std::uint64_t last_epoch{0};         ///< Producer epoch of the last event.
    std::uint64_t absolute_phase{0};     ///< Last logical absolute phase position.

    /// @brief Render the cold-path diagnostic string.
    /// @return Human-readable statistics.
    /// @note Spec `synchro/consumer`, requirement "Consumer observability";
    ///       mandate 9.
    [[nodiscard]] std::string to_string() const;
};

/// @brief Serialize consumer statistics to diagnostic JSON.
/// @param j Destination JSON object.
/// @param stats Statistics to serialize.
/// @note Mandate 10; cold path only.
void to_json(nlohmann::json& j, const SyncConsumerStats& stats);

/// @brief Parse consumer statistics from JSON, strictly non-throwing.
/// @param j Source JSON object.
/// @param stats Destination, fields updated independently.
/// @note Mandate 10; cold path only.
void from_json(const nlohmann::json& j, SyncConsumerStats& stats) noexcept;

} // namespace nexigen::synchro
