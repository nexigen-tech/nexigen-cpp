// nexigen::runner — ImpactOnAppSuiteOperability enum.
#pragma once
#include <array>
#include <cstdint>
#include <string_view>

#include <nlohmann/json.hpp>

#include <nexigen/core/enum_traits.hpp>
namespace nexigen::runner {

enum class ImpactOnAppSuiteOperability : std::uint8_t {
    Failure,    ///< Any abnormal termination restarts the whole suite.
    Degrade,    ///< Only this application is retried; suite becomes degraded.
    Not_impact, ///< Only this application is retried; suite state is unchanged.
};

} // namespace nexigen::runner

namespace nexigen::runner {

[[nodiscard]] constexpr std::string_view to_string(ImpactOnAppSuiteOperability value) noexcept {
    return core::enum_name(value);
}

[[nodiscard]] constexpr bool from_string(std::string_view name,
                                         ImpactOnAppSuiteOperability& out) noexcept {
    return core::enum_value(name, out);
}

void to_json(nlohmann::json& j, ImpactOnAppSuiteOperability value);
void from_json(const nlohmann::json& j, ImpactOnAppSuiteOperability& value) noexcept;

} // namespace nexigen::runner

// Vocabulary table for core::enum_name/enum_value (change refactor-enum-traits-helper).
namespace nexigen::core {

template <> struct EnumTraits<nexigen::runner::ImpactOnAppSuiteOperability> {
    struct Entry {
        nexigen::runner::ImpactOnAppSuiteOperability value;
        std::string_view name;
    };
    static constexpr std::array<Entry, 3> entries{{
        {nexigen::runner::ImpactOnAppSuiteOperability::Failure, "failure"},
        {nexigen::runner::ImpactOnAppSuiteOperability::Degrade, "degrade"},
        {nexigen::runner::ImpactOnAppSuiteOperability::Not_impact, "not_impact"},
    }};
    static constexpr std::string_view fallback = "unknown";
};

} // namespace nexigen::core
