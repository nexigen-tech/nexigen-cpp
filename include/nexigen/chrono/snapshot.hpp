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

/// @brief One UTC-offset snapshot: the monotonic-raw series key and its offset.
///
/// A 16-byte, trivially copyable value of two register-sized fields. The stamp
/// orders the series in the monotonic-raw domain, and `realtime_offset_ns`
/// re-binds any event stamp in that domain to the wall clock as
/// `stamp + realtime_offset_ns`. Copying the struct copies no ownership; the
/// type is immutable in practice and safe to pass by value across threads.
///
/// @note Spec `chrono/chrono-snapshot`, requirement "Snapshot value type";
///       requirement "Monotonic series key"; requirement "Signed UTC offset
///       semantics".
struct Snapshot {
    chrono::Timestamp stamp{};          ///< Monotonic-raw nanoseconds (series key).
    std::int64_t realtime_offset_ns{0}; ///< `realtime` minus `monotonic_raw` (signed).

    /// @brief Render this snapshot as a diagnostic string.
    ///
    /// Allocates a `std::string`; it belongs to diagnostics and post-mortem
    /// tooling, never to the snapshot delivery path.
    ///
    /// @return The rendered text naming the stamp and the offset.
    /// @note Spec `chrono/chrono-snapshot`, requirement "Snapshot
    ///       serialization"; project mandate 9.
    [[nodiscard]] std::string to_string() const;
};

/// @brief Free-helper mirror of `Snapshot::to_string`.
///
/// @param snapshot Snapshot to render.
/// @return The same text as `snapshot.to_string()`.
/// @note Spec `chrono/chrono-snapshot`, requirement "Snapshot serialization";
///       project mandate 9.
[[nodiscard]] std::string to_string(const Snapshot& snapshot);

/// @brief Serialize a snapshot as JSON (cold path only).
///
/// Emits the monotonic-raw stamp and the signed realtime offset; `boottime` is
/// deliberately absent. The body lives in `src/chrono/snapshot.cpp`.
///
/// @param j Destination JSON value, replaced by the serialized snapshot.
/// @param snapshot Snapshot to serialize.
/// @note Spec `chrono/chrono-snapshot`, requirement "Snapshot serialization";
///       project mandate 10.
void to_json(nlohmann::json& j, const Snapshot& snapshot);

/// @brief Parse a snapshot from JSON, strictly non-throwing.
///
/// Under `JSON_NOEXCEPTION=1` a non-object value leaves `out` unchanged, and a
/// missing or mistyped field is skipped so that field keeps its previous value.
/// The body lives in `src/chrono/snapshot.cpp`.
///
/// @param j Source JSON value.
/// @param out Destination snapshot; each recognized field is assigned in place.
/// @note Spec `chrono/chrono-snapshot`, requirement "Snapshot serialization";
///       project mandate 10.
void from_json(const nlohmann::json& j, Snapshot& out) noexcept;

static_assert(sizeof(Snapshot) == 16, "snapshot must stay two register-sized fields");
static_assert(std::is_trivially_copyable_v<Snapshot>, "snapshot must be trivially copyable");

} // namespace nexigen::chrono

/// @brief `std::formatter` specialization rendering the snapshot diagnostic string.
///
/// @note Spec `chrono/chrono-snapshot`, requirement "Snapshot serialization";
///       project mandate 11.
template <> struct std::formatter<nexigen::chrono::Snapshot> : std::formatter<std::string_view> {
    /// @brief Format a snapshot as its diagnostic text.
    ///
    /// @tparam Ctx Format context type; the inherited string specifiers apply.
    /// @param snapshot Snapshot to format.
    /// @param ctx Format context receiving the output.
    /// @return Iterator past the last written character.
    /// @note Project mandate 11 (`std::formatter`).
    template <class Ctx> auto format(const nexigen::chrono::Snapshot& snapshot, Ctx& ctx) const {
        const std::string text = snapshot.to_string();
        return std::formatter<std::string_view>::format(text, ctx);
    }
};
