// nexigen::synchro — per-event consumer context
// (change add-synchro-event-sync; spec synchro/consumer).
//
// One value is built per accepted event and handed to application handlers. It
// is fixed-size (88 bytes) and trivially copyable: the handler may copy it, and
// no handler reference outlives the dispatch.
#pragma once

#include <cstdint>
#include <format>
#include <string>
#include <string_view>

#include <nlohmann/json.hpp>

namespace nexigen::synchro {

/// @brief Schedule position and measured latencies of one delivered phase.
///
/// One instance is built per accepted event and passed to the registered
/// handlers. It is fixed-size (88 bytes) and trivially copyable, so a handler
/// may copy it, but no handler reference outlives the dispatch. The value
/// carries the producer identity (`cycle_number`, `phase_index`,
/// `phases_total`), the original grid deadline, the header publication time,
/// the receive time, both latency measurements, and the late-event flag.
///
/// @note Spec `synchro/consumer`, requirement "Late-event classification".
struct alignas(8) SyncContext {
    std::uint64_t cycle_number{0};      ///< Cycle index from the payload.
    std::uint32_t phase_index{0};       ///< Phase index from the payload.
    std::uint32_t phases_total{0};      ///< Phases per cycle from the payload.
    std::uint64_t scheduled_time_ns{0}; ///< Grid deadline of this phase.
    std::uint64_t cycle_period_ns{0};   ///< Cycle period from the payload.
    std::uint64_t publish_time_ns{0};   ///< Header publication timestamp.
    std::uint64_t receive_time_ns{0};   ///< Raw-monotonic time at delivery.
    std::uint64_t sequence_id{0};       ///< Transport sequence of the event.
    std::uint64_t producer_epoch{0};    ///< Producer lifecycle epoch.
    std::int64_t ipc_latency_ns{0};     ///< Receive time minus publication time.
    std::int64_t e2e_latency_ns{0};     ///< Receive time minus scheduled time.
    bool is_late{false};                ///< End-to-end latency exceeded the budget.

    /// @brief Render the cold-path diagnostic string.
    /// @return Human-readable context summary.
    /// @note Spec `synchro/consumer`, requirement "Late-event classification";
    ///       mandate 9.
    [[nodiscard]] std::string to_string() const;
};

static_assert(sizeof(SyncContext) == 88U, "SyncContext must stay 88 bytes");
static_assert(alignof(SyncContext) == 8U, "SyncContext must stay 8-byte aligned");

/// @brief Serialize a context to diagnostic JSON.
/// @param j Destination JSON object.
/// @param context Context to serialize.
/// @note Mandate 10; cold path only.
void to_json(nlohmann::json& j, const SyncContext& context);

/// @brief Parse a context from diagnostic JSON, strictly non-throwing.
/// @param j Source JSON object.
/// @param context Destination, untouched on a non-object input.
/// @note Mandate 10; cold path only.
void from_json(const nlohmann::json& j, SyncContext& context) noexcept;

} // namespace nexigen::synchro
