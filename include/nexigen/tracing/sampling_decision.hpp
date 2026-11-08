// nexigen::tracing — SamplingDecision enum.
#pragma once

#include <cstdint>
#include <string>
#include <string_view>

#include <nlohmann/json.hpp>

namespace nexigen::tracing {

/// The whole sampling vocabulary (design non-goal: no policy engines).
enum class SamplingDecision : std::uint8_t {
    Drop,            ///< Do not record: start_span short-circuits.
    RecordAndSample, ///< Record the span (default).
};

[[nodiscard]] constexpr std::string_view to_string(SamplingDecision decision) noexcept {
    switch (decision) {
        case SamplingDecision::Drop:            return "Drop";
        case SamplingDecision::RecordAndSample: return "RecordAndSample";
        default:                                return "Unknown";
    }
}

[[nodiscard]] constexpr bool from_string(std::string_view name, SamplingDecision& out) noexcept {
    if (name == "Drop") {
        out = SamplingDecision::Drop;
        return true;
    }
    if (name == "RecordAndSample") {
        out = SamplingDecision::RecordAndSample;
        return true;
    }
    return false;
}

inline void from_json(const nlohmann::json& j, SamplingDecision& out) noexcept {
    const auto* const name = j.get_ptr<const nlohmann::json::string_t*>();
    if (name != nullptr) {
        static_cast<void>(from_string(std::string_view(*name), out));
    }
}


inline void to_json(nlohmann::json& j, const SamplingDecision samplingDecision) {
    j = std::string(to_string(samplingDecision));
}

} // namespace nexigen::tracing
