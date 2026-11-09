// nexigen::tracing — DroppedStats (change refactor-hub-model-config-headers;
// extracted verbatim from tracer_provider.hpp).
#pragma once

#include <cstdint>
#include <format>
#include <string>
#include <string_view>

#include <nlohmann/json.hpp>

namespace nexigen::tracing {

/// Snapshot of dropped-span accounting (spec "Drop visibility"): the three
/// drop causes counted separately; readable from any thread at any time as
/// a relaxed aggregate observation, never as synchronization. Sampling-
/// filtered spans are NOT counted (a decision is not a failure).
struct DroppedStats {
    std::uint64_t rate_limited{};   ///< Rejected by the span-rate budget.
    std::uint64_t buffer_full{};    ///< Rejected by a full shard ring.
    std::uint64_t slot_exhausted{}; ///< Dropped: no free shard slot.

    [[nodiscard]] constexpr std::uint64_t total() const noexcept {
        return rate_limited + buffer_full + slot_exhausted;
    }

    /// Cold-path diagnostic (mandate 9).
    [[nodiscard]] std::string to_string() const;
};

/// Free helper (mandate 9): mirrors the member.
[[nodiscard]] std::string to_string(const DroppedStats& stats);

/// ADL JSON (cold path, full round-trip — the snapshot is a value type).
void to_json(nlohmann::json& j, const DroppedStats& stats);
void from_json(const nlohmann::json& j, DroppedStats& out) noexcept;

} // namespace nexigen::tracing

/// The drop snapshot formats as its diagnostic string (mandate 11).
template <>
struct std::formatter<nexigen::tracing::DroppedStats> : std::formatter<std::string_view> {
    template <class Ctx> auto format(const nexigen::tracing::DroppedStats& stats, Ctx& ctx) const {
        const std::string text = stats.to_string();
        return std::formatter<std::string_view>::format(text, ctx);
    }
};
