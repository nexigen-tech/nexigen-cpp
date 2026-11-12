// nexigen::metrics — a current named state value (design D12; spec
// metrics/metric-provider).
#pragma once

#include <format>
#include <string>
#include <string_view>
#include <type_traits>

#include <nlohmann/json.hpp>

#include <nexigen/metrics/metric_identity.hpp>

namespace nexigen::metrics {

/// @brief A current named state value: a bounded NUL-terminated string.
///
/// Last-writer-wins: the most recent `set` overwrites the whole buffer. The
/// payload is inline so the record stays fixed-size and trivially copyable and
/// can travel inside a `MetricFrame`.
///
/// @note Spec `metrics/state-gauge`, requirement "Multi-threaded last-writer-wins".
struct StateValue {
    char value[max_state_length + 1]{}; ///< NUL-terminated state text.

    /// @brief Render the state text as a cold-path diagnostic string.
    ///
    /// @return Human-readable text carrying the current state.
    /// @note Spec `metrics/policy-enums`, requirement "Public type diagnostic
    ///       strings"; diagnostic only (project mandate 9).
    [[nodiscard]] std::string to_string() const;
};

static_assert(std::is_trivially_copyable_v<StateValue>);

/// @brief Free diagnostic helper mirroring `StateValue::to_string()`.
///
/// @param value State payload to render.
/// @return The same text the member returns.
/// @note Spec `metrics/policy-enums`, requirement "Public type diagnostic
///       strings"; diagnostic only (project mandate 9).
[[nodiscard]] std::string to_string(const StateValue& value);

/// @brief Write a state payload as JSON (cold path).
///
/// @param j Output JSON value, carrying the state text.
/// @param value Payload to serialize.
/// @note Spec `metrics/state-gauge`, requirement "State serialization
///       contract"; JSON only (project mandate 10).
void to_json(nlohmann::json& j, const StateValue& value);
/// @brief Parse a state payload from JSON without throwing.
///
/// Accepts only a non-empty `"value"` string of at most `max_state_length`
/// bytes; a missing, non-string, empty, or over-long value leaves the field
/// untouched.
///
/// @param j JSON object to read.
/// @param out Destination, NUL-terminated on success.
/// @note Spec `metrics/state-gauge`, requirement "State serialization
///       contract".
/// @note Cold-path JSON only (project mandate 10); safe under
///       `JSON_NOEXCEPTION=1`.
void from_json(const nlohmann::json& j, StateValue& out) noexcept;

} // namespace nexigen::metrics

/// @brief `std::formatter` for `StateValue`, rendering its diagnostic string.
///
/// @note Project mandate 11; formatting is cold path only.
template <> struct std::formatter<nexigen::metrics::StateValue> : std::formatter<std::string_view> {
    /// @brief Format the state payload through its `to_string()` text.
    ///
    /// @tparam Ctx Format context type supplied by `std::format`.
    /// @param value Payload to render.
    /// @param ctx Output context receiving the text.
    /// @return Iterator past the last written character.
    template <class Ctx> auto format(const nexigen::metrics::StateValue& value, Ctx& ctx) const {
        const std::string text = value.to_string();
        return std::formatter<std::string_view>::format(text, ctx);
    }
};
