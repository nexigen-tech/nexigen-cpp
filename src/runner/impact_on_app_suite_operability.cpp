// nexigen::runner — ImpactOnAppSuiteOperability JSON serialization
// (change refactor-runner-structure; spec runner/app-suite).
#include <nlohmann/json.hpp>

#include <nexigen/core/enum_traits.hpp>
#include <nexigen/runner/impact_on_app_suite_operability.hpp>

namespace nexigen::runner {

void to_json(nlohmann::json& j, ImpactOnAppSuiteOperability value) {
    core::mandate::enum_to_json(j, value);
}

void from_json(const nlohmann::json& j, ImpactOnAppSuiteOperability& value) noexcept {
    core::mandate::enum_from_json(j, value);
}

} // namespace nexigen::runner
