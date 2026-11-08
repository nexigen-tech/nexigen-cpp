// nexigen::metrics — ConcurrencyPolicy enum.
#pragma once

#include <cstdint>
#include <string>
#include <string_view>

#include <nlohmann/json.hpp>

namespace nexigen::metrics {

/// How an instrument is written: by one thread, or sharded across threads.
enum class ConcurrencyPolicy : std::uint8_t {
    SingleThreaded,
    MultiThreaded,
};

[[nodiscard]] constexpr std::string_view to_string(ConcurrencyPolicy policy) noexcept {
    switch (policy) {
        case ConcurrencyPolicy::SingleThreaded: return "SingleThreaded";
        case ConcurrencyPolicy::MultiThreaded:  return "MultiThreaded";
        default:                                return "Unknown";
    }
}

[[nodiscard]] constexpr bool from_string(std::string_view name, ConcurrencyPolicy& out) noexcept {
    if (name == "SingleThreaded") {
        out = ConcurrencyPolicy::SingleThreaded;
        return true;
    }
    if (name == "MultiThreaded") {
        out = ConcurrencyPolicy::MultiThreaded;
        return true;
    }
    return false;
}

inline void from_json(const nlohmann::json& j, ConcurrencyPolicy& out) noexcept {
    const auto* const name = j.get_ptr<const nlohmann::json::string_t*>();
    if (name != nullptr) {
        static_cast<void>(from_string(std::string_view(*name), out));
    }
}


inline void to_json(nlohmann::json& j, const ConcurrencyPolicy concurrencyPolicy) {
    j = std::string(to_string(concurrencyPolicy));
}

} // namespace nexigen::metrics
