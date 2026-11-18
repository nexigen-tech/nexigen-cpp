// nexigen::synchro — generator statistics
// (change add-synchro-event-sync; spec synchro/generator).
//
// The struct is a plain, trivially copyable snapshot value returned by
// SyncGenerator::stats(). The generator keeps the live counters in relaxed
// atomics written only by its worker, so reading a snapshot while the worker
// runs is race-free (each field is coherent on its own; the snapshot as a whole
// may straddle one publication step).
#pragma once

#include <cstdint>
#include <format>
#include <string>
#include <string_view>

#include <nlohmann/json.hpp>

namespace nexigen::synchro {

/// @brief Counters and timing snapshots of one generator lifecycle.
///
/// @note Spec `synchro/generator`, requirement "Generator observability".
/// @note Returned by value from a lock-free atomic snapshot; safe to read while
///       the generator worker runs.
struct SyncGeneratorStats {
    std::uint64_t cycles_completed{0};   ///< Cycles whose last phase was published.
    std::uint64_t phases_published{0};   ///< Phases sent through the producer.
    std::uint64_t overrun_slots{0};      ///< Phase slots skipped by overruns.
    std::uint64_t overrun_episodes{0};   ///< Wake-ups that skipped at least one slot.
    std::uint64_t send_failures{0};      ///< Phase publications rejected by transport.
    std::uint64_t notify_failures{0};    ///< Published samples whose wake failed.
    std::uint64_t start_time_ns{0};      ///< Resolved start instant of this lifecycle.
    std::uint64_t last_phase_time_ns{0}; ///< Publication instant of the last phase.
    std::int64_t last_jitter_ns{0};      ///< Signed jitter of the last phase.
    std::uint64_t max_jitter_ns{0};      ///< Largest absolute jitter observed.

    /// @brief Render the cold-path diagnostic string.
    /// @return Human-readable statistics.
    /// @note Spec `synchro/generator`, requirement "Generator observability";
    ///       mandate 9.
    [[nodiscard]] std::string to_string() const;
};

/// @brief Serialize generator statistics to diagnostic JSON.
/// @param j Destination JSON object.
/// @param stats Statistics to serialize.
/// @note Mandate 10; cold path only.
void to_json(nlohmann::json& j, const SyncGeneratorStats& stats);

/// @brief Parse generator statistics from JSON, strictly non-throwing.
/// @param j Source JSON object.
/// @param stats Destination, fields updated independently.
/// @note Mandate 10; cold path only.
void from_json(const nlohmann::json& j, SyncGeneratorStats& stats) noexcept;

} // namespace nexigen::synchro
