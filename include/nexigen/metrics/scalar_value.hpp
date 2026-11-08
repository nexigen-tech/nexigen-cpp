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

/// Scalar payload of a counter or a gauge.
struct ScalarValue {
    std::uint64_t counter{0};  ///< Counter total / window delta
    std::int64_t gauge_int{0}; ///< Gauge<std::int64_t>
    double gauge_double{0.0};  ///< Gauge<double>
    bool is_double{false};     ///< true when the gauge carries a double

    [[nodiscard]] std::string to_string() const;
};

static_assert(std::is_trivially_copyable_v<ScalarValue>);

[[nodiscard]] std::string to_string(const ScalarValue& value);

void to_json(nlohmann::json& j, const ScalarValue& value);
void from_json(const nlohmann::json& j, ScalarValue& out) noexcept;

} // namespace nexigen::metrics

/// ScalarValue formats as its diagnostic string (mandate 11).
template <>
struct std::formatter<nexigen::metrics::ScalarValue> : std::formatter<std::string_view> {
    template <class Ctx> auto format(const nexigen::metrics::ScalarValue& value, Ctx& ctx) const {
        const std::string text = value.to_string();
        return std::formatter<std::string_view>::format(text, ctx);
    }
};
