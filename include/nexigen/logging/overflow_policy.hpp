// nexigen::logging — overflow policy for a full SPSC shard ring (spec
// logging/logger-options).
#pragma once

#include <array>
#include <cstdint>
#include <string>
#include <string_view>

#include <nlohmann/json.hpp>

#include <nexigen/core/enum_traits.hpp>
namespace nexigen::logging {

/// Overflow policy for a full SPSC shard ring.
enum class OverflowPolicy : std::uint8_t {
    DropNewest,      ///< Incoming record is dropped; `buffer_full` counted.
    OverwriteOldest, ///< Oldest unread record is overwritten atomically;
                     ///< `dropped_count_` incremented.
};

} // namespace nexigen::logging

// Vocabulary table for core::enum_name/enum_value (change refactor-enum-traits-helper).
namespace nexigen::core {

template <> struct EnumTraits<nexigen::logging::OverflowPolicy> {
    struct Entry {
        nexigen::logging::OverflowPolicy value;
        std::string_view name;
    };
    static constexpr std::array<Entry, 2> entries{{
        {nexigen::logging::OverflowPolicy::DropNewest, "drop_newest"},
        {nexigen::logging::OverflowPolicy::OverwriteOldest, "overwrite_oldest"},
    }};
};

} // namespace nexigen::core

namespace nexigen::logging {

[[nodiscard]] constexpr std::string_view to_string(OverflowPolicy p) noexcept {
    return core::enum_name(p);
}
[[nodiscard]] constexpr bool from_string(std::string_view name, OverflowPolicy& out) noexcept {
    return core::enum_value(name, out);
}
inline void to_json(nlohmann::json& j, const OverflowPolicy& p) {
    j = std::string(to_string(p));
}
inline void from_json(const nlohmann::json& j, OverflowPolicy& out) noexcept {
    core::mandate::enum_from_json(j, out);
}

} // namespace nexigen::logging
