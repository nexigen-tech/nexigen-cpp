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

/// A current named state value: a bounded NUL-terminated string, last-writer-
/// wins. The payload is inline so the record stays fixed-size and trivially
/// copyable.
struct StateValue {
    char value[max_state_length + 1]{};

    [[nodiscard]] std::string to_string() const;
};

static_assert(std::is_trivially_copyable_v<StateValue>);

[[nodiscard]] std::string to_string(const StateValue& value);

void to_json(nlohmann::json& j, const StateValue& value);
void from_json(const nlohmann::json& j, StateValue& out) noexcept;

} // namespace nexigen::metrics

/// StateValue formats as its diagnostic string (mandate 11).
template <> struct std::formatter<nexigen::metrics::StateValue> : std::formatter<std::string_view> {
    template <class Ctx> auto format(const nexigen::metrics::StateValue& value, Ctx& ctx) const {
        const std::string text = value.to_string();
        return std::formatter<std::string_view>::format(text, ctx);
    }
};
