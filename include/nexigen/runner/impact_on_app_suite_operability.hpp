// nexigen::runner — ImpactOnAppSuiteOperability enum.
#pragma once
#include <cstdint>
#include <string_view>

#include <nlohmann/json.hpp>

namespace nexigen::runner {

enum class ImpactOnAppSuiteOperability : std::uint8_t {
    Failure,    ///< Any abnormal termination restarts the whole suite.
    Degrade,    ///< Only this application is retried; suite becomes degraded.
    Not_impact, ///< Only this application is retried; suite state is unchanged.
};

[[nodiscard]] constexpr std::string_view to_string(ImpactOnAppSuiteOperability value) noexcept {
    switch (value) {
        case ImpactOnAppSuiteOperability::Failure:    return "failure";
        case ImpactOnAppSuiteOperability::Degrade:    return "degrade";
        case ImpactOnAppSuiteOperability::Not_impact: return "not_impact";
        default:                                      return "unknown";
    }
}

[[nodiscard]] constexpr bool from_string(std::string_view name,
                                         ImpactOnAppSuiteOperability& out) noexcept {
    if (name == "failure") {
        out = ImpactOnAppSuiteOperability::Failure;
        return true;
    }
    if (name == "degrade") {
        out = ImpactOnAppSuiteOperability::Degrade;
        return true;
    }
    if (name == "not_impact") {
        out = ImpactOnAppSuiteOperability::Not_impact;
        return true;
    }
    return false;
}

void to_json(nlohmann::json& j, ImpactOnAppSuiteOperability value);
void from_json(const nlohmann::json& j, ImpactOnAppSuiteOperability& value) noexcept;

} // namespace nexigen::runner
