// nexigen::logging — shard lifecycle state observed by the consumer drain
// loop (spec logging/logger-options).
#pragma once

#include <array>
#include <cstdint>
#include <string>
#include <string_view>

#include <nlohmann/json.hpp>

#include <nexigen/core/enum_traits.hpp>
namespace nexigen::logging {

/// Shard lifecycle state observed by the consumer drain loop.
enum class ShardState : std::uint8_t {
    Active,   ///< Slot registered and accepting records.
    Draining, ///< Producer thread exited; consumer must drain then free.
    Free,     ///< Returned to the free-slot pool.
};

} // namespace nexigen::logging

namespace nexigen::logging {

[[nodiscard]] constexpr std::string_view to_string(ShardState s) noexcept {
    return core::enum_name(s);
}
[[nodiscard]] constexpr bool from_string(std::string_view name, ShardState& out) noexcept {
    return core::enum_value(name, out);
}
inline void to_json(nlohmann::json& j, const ShardState& s) {
    j = std::string(to_string(s));
}
inline void from_json(const nlohmann::json& j, ShardState& out) noexcept {
    core::mandate::enum_from_json(j, out);
}

} // namespace nexigen::logging

// Vocabulary table for core::enum_name/enum_value (change refactor-enum-traits-helper).
namespace nexigen::core {

template <> struct EnumTraits<nexigen::logging::ShardState> {
    struct Entry {
        nexigen::logging::ShardState value;
        std::string_view name;
    };
    static constexpr std::array<Entry, 3> entries{{
        {nexigen::logging::ShardState::Active, "active"},
        {nexigen::logging::ShardState::Draining, "draining"},
        {nexigen::logging::ShardState::Free, "free"},
    }};
};

} // namespace nexigen::core
