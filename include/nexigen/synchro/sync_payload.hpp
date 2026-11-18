// nexigen::synchro — synchronization event payload
// (change add-synchro-event-sync; spec synchro/protocol).
//
// The payload repeats the schedule identity (phase count and cycle period) so a
// consumer can validate the producer against its own configuration and a late
// joiner does not need a second lookup. Exactly 32 bytes, 8-byte aligned and
// trivially copyable: it is exchanged zero-copy through an event channel.
#pragma once

#include <cstdint>
#include <format>
#include <string>
#include <string_view>
#include <type_traits>

#include <nlohmann/json.hpp>

#include <nexigen/com/concepts/event_payload_concept.hpp>

namespace nexigen::synchro {

/// @brief One phase event: its logical position and the instant it was due.
///
/// `scheduled_time_ns` is the original grid deadline and never moves; the
/// actual publication instant travels in the event header timestamp. Together
/// they let a consumer measure end-to-end latency without trusting its own
/// wake-up time.
///
/// @note Spec `synchro/protocol`, requirement "Synchronization payload and
///       channel configuration".
/// @note Mandate 9: `to_string` is cold-path diagnostics only.
struct alignas(8) SyncPayload {
    std::uint64_t cycle_number{0};      ///< Cycle index since generator start, zero-based.
    std::uint32_t phase_index{0};       ///< Phase index inside the cycle, zero-based.
    std::uint32_t phases_total{0};      ///< Phases per cycle declared by the producer.
    std::uint64_t scheduled_time_ns{0}; ///< Raw-monotonic deadline of this phase.
    std::uint64_t cycle_period_ns{0};   ///< Cycle period declared by the producer.

    /// @brief Render the cold-path diagnostic string.
    /// @return Human-readable payload summary.
    /// @note Spec `synchro/protocol`, requirement "Synchronization payload and
    ///       channel configuration"; mandate 9.
    [[nodiscard]] std::string to_string() const;
};

static_assert(sizeof(SyncPayload) == 32U, "SyncPayload must stay 32 bytes");
static_assert(alignof(SyncPayload) == 8U, "SyncPayload must stay 8-byte aligned");
static_assert(std::is_trivially_copyable_v<SyncPayload>,
              "SyncPayload must be trivially copyable for zero-copy transport");
static_assert(com::concepts::EventPayload<SyncPayload>,
              "SyncPayload must satisfy the com event payload contract");

/// @brief Serialize a payload to diagnostic JSON.
/// @param j Destination JSON object.
/// @param payload Payload to serialize.
/// @note Mandate 10; cold path only.
void to_json(nlohmann::json& j, const SyncPayload& payload);

/// @brief Parse a payload from diagnostic JSON, strictly non-throwing.
/// @param j Source JSON object.
/// @param payload Destination payload, untouched on a non-object input.
/// @note Mandate 10; cold path only.
void from_json(const nlohmann::json& j, SyncPayload& payload) noexcept;

} // namespace nexigen::synchro
