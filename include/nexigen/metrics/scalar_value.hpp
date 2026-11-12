// nexigen::metrics — scalar payload of a counter or a gauge (design D12;
// spec metrics/metric-provider).
#pragma once

#include <cstdint>
#include <format>
#include <string>
#include <string_view>
#include <type_traits>

#include <nlohmann/json.hpp>

namespace nexigen::metrics {

/// @brief Scalar payload of a counter or a gauge.
///
/// One member of the `MetricRecord` payload union, so it is fixed-size and
/// trivially copyable: counters use `counter`, integer gauges use `gauge_int`,
/// and double gauges use `gauge_double` with `is_double` selecting the active
/// interpretation. The value type is decided at harvest time by the record's
/// `MetricKind`.
///
/// @note Spec `metrics/snapshot-format`, requirement "Identity and scalar
///       snapshots round-trip safely".
struct ScalarValue {
    std::uint64_t counter{0};  ///< Counter total / window delta.
    std::int64_t gauge_int{0}; ///< Integer gauge value (`Gauge<std::int64_t>`).
    double gauge_double{0.0};  ///< Double gauge value (`Gauge<double>`).
    bool is_double{false};     ///< `true` when the gauge carries a double.

    /// @brief Render the payload as a cold-path diagnostic string.
    ///
    /// @return Human-readable text naming the active scalar members.
    /// @note Spec `metrics/policy-enums`, requirement "Public type diagnostic
    ///       strings"; diagnostic only (project mandate 9).
    [[nodiscard]] std::string to_string() const;
};

static_assert(std::is_trivially_copyable_v<ScalarValue>);

/// @brief Free diagnostic helper mirroring `ScalarValue::to_string()`.
///
/// @param value Payload to render.
/// @return The same text the member returns.
/// @note Spec `metrics/policy-enums`, requirement "Public type diagnostic
///       strings"; diagnostic only (project mandate 9).
[[nodiscard]] std::string to_string(const ScalarValue& value);

/// @brief Write a scalar payload as JSON (cold path).
///
/// @param j Output JSON object, filled with the scalar fields.
/// @param value Payload to serialize.
/// @note Spec `metrics/policy-enums`, requirement "Public type JSON
///       serialization"; JSON only (project mandate 10).
void to_json(nlohmann::json& j, const ScalarValue& value);
/// @brief Parse a scalar payload from JSON without throwing.
///
/// Accepts signed and unsigned integers and doubles, updating `is_double` only
/// from a boolean field; missing or wrong-typed fields are ignored.
///
/// @param j JSON object to read.
/// @param out Destination, left at its prior values for ignored fields.
/// @note Spec `metrics/snapshot-format`, requirement "Identity and scalar
///       snapshots round-trip safely".
/// @note Cold-path JSON only (project mandate 10); safe under
///       `JSON_NOEXCEPTION=1`.
void from_json(const nlohmann::json& j, ScalarValue& out) noexcept;

} // namespace nexigen::metrics

/// @brief `std::formatter` for `ScalarValue`, rendering its diagnostic string.
///
/// @note Project mandate 11; formatting is cold path only.
template <>
struct std::formatter<nexigen::metrics::ScalarValue> : std::formatter<std::string_view> {
    /// @brief Format the payload through its `to_string()` text.
    ///
    /// @tparam Ctx Format context type supplied by `std::format`.
    /// @param value Payload to render.
    /// @param ctx Output context receiving the text.
    /// @return Iterator past the last written character.
    template <class Ctx> auto format(const nexigen::metrics::ScalarValue& value, Ctx& ctx) const {
        const std::string text = value.to_string();
        return std::formatter<std::string_view>::format(text, ctx);
    }
};
