// nexigen::logging — overflow policy for a full SPSC shard ring (spec
// logging/logger-options).
#pragma once

#include <cstdint>
#include <string>
#include <string_view>

#include <nlohmann/json.hpp>

namespace nexigen::logging {

/// Overflow policy for a full SPSC shard ring.
enum class OverflowPolicy : std::uint8_t {
    DropNewest,      ///< Incoming record is dropped; `buffer_full` counted.
    OverwriteOldest, ///< Oldest unread record is overwritten atomically;
                     ///< `dropped_count_` incremented.
};

[[nodiscard]] constexpr std::string_view to_string(OverflowPolicy p) noexcept {
    switch (p) {
        case OverflowPolicy::DropNewest:      return "drop_newest";
        case OverflowPolicy::OverwriteOldest: return "overwrite_oldest";
        default:                              return "Unknown";
    }
}
[[nodiscard]] constexpr bool from_string(std::string_view name, OverflowPolicy& out) noexcept {
    if (name == "drop_newest") {
        out = OverflowPolicy::DropNewest;
        return true;
    }
    if (name == "overwrite_oldest") {
        out = OverflowPolicy::OverwriteOldest;
        return true;
    }
    return false;
}
inline void to_json(nlohmann::json& j, const OverflowPolicy& p) {
    j = std::string(to_string(p));
}
inline void from_json(const nlohmann::json& j, OverflowPolicy& out) noexcept {
    const auto* name = j.get_ptr<const nlohmann::json::string_t*>();
    if (name != nullptr) {
        static_cast<void>(from_string(std::string_view(*name), out));
    }
}

} // namespace nexigen::logging
