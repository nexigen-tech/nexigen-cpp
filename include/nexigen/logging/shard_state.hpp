// nexigen::logging — shard lifecycle state observed by the consumer drain
// loop (spec logging/logger-options).
#pragma once

#include <cstdint>
#include <string>
#include <string_view>

#include <nlohmann/json.hpp>

namespace nexigen::logging {

/// Shard lifecycle state observed by the consumer drain loop.
enum class ShardState : std::uint8_t {
    Active,   ///< Slot registered and accepting records.
    Draining, ///< Producer thread exited; consumer must drain then free.
    Free,     ///< Returned to the free-slot pool.
};

[[nodiscard]] constexpr std::string_view to_string(ShardState s) noexcept {
    switch (s) {
        case ShardState::Active:   return "active";
        case ShardState::Draining: return "draining";
        case ShardState::Free:     return "free";
        default:                   return "Unknown";
    }
}
[[nodiscard]] constexpr bool from_string(std::string_view name, ShardState& out) noexcept {
    if (name == "active") {
        out = ShardState::Active;
        return true;
    }
    if (name == "draining") {
        out = ShardState::Draining;
        return true;
    }
    if (name == "free") {
        out = ShardState::Free;
        return true;
    }
    return false;
}
inline void to_json(nlohmann::json& j, const ShardState& s) {
    j = std::string(to_string(s));
}
inline void from_json(const nlohmann::json& j, ShardState& out) noexcept {
    const auto* name = j.get_ptr<const nlohmann::json::string_t*>();
    if (name != nullptr) {
        static_cast<void>(from_string(std::string_view(*name), out));
    }
}

} // namespace nexigen::logging
