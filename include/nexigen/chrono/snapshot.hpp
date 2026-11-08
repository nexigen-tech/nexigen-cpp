// nexigen::chrono — persisted UTC-offset snapshot (change
// add-chrono-clock-service; spec chrono/chrono-snapshot).
//
// Snapshot is a 16-byte, trivially copyable value: the monotonic-raw
// stamp (the series key) plus the signed UTC offset (realtime − monotonic_raw)
// sampled at that instant. Post-hoc readers re-bind a monotonic event to the
// wall clock as stamp + realtime_offset_ns.
//
// COLD PATH ONLY: to_string / to_json / from_json allocate and are
// diagnostics; the bodies live in src/chrono/snapshot.cpp so consumer
// TUs stay lean.
#pragma once

#include <cstdint>
#include <format>
#include <string>
#include <type_traits>

#include <nlohmann/json.hpp>

#include <nexigen/chrono/timestamp.hpp>

namespace nexigen::chrono {

/// One UTC-offset snapshot: the monotonic-raw series key and its wall-clock
/// offset at that instant.
struct Snapshot {
    chrono::Timestamp stamp{};          ///< monotonic-raw nanoseconds (series key)
    std::int64_t realtime_offset_ns{0}; ///< realtime − monotonic_raw (signed)

    [[nodiscard]] std::string to_string() const;
};

/// Free helper (mandate 9): mirrors the member.
[[nodiscard]] std::string to_string(const Snapshot& snapshot);

// --- nlohmann::json (cold path). Readers are strictly non-throwing under
// JSON_NOEXCEPTION=1. Definitions live in src/chrono/snapshot.cpp.
void to_json(nlohmann::json& j, const Snapshot& snapshot);
void from_json(const nlohmann::json& j, Snapshot& out) noexcept;

static_assert(sizeof(Snapshot) == 16, "snapshot must stay two register-sized fields");
static_assert(std::is_trivially_copyable_v<Snapshot>, "snapshot must be trivially copyable");

} // namespace nexigen::chrono

/// Snapshot formats as its diagnostic string (mandate 11).
template <> struct std::formatter<nexigen::chrono::Snapshot> : std::formatter<std::string_view> {
    template <class Ctx> auto format(const nexigen::chrono::Snapshot& snapshot, Ctx& ctx) const {
        const std::string text = snapshot.to_string();
        return std::formatter<std::string_view>::format(text, ctx);
    }
};
