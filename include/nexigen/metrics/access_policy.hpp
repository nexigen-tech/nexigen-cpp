// nexigen::metrics — AccessPolicy enum.
#pragma once

#include <array>
#include <cstdint>
#include <string>
#include <string_view>

#include <nlohmann/json.hpp>

#include <nexigen/core/enum_traits.hpp>
namespace nexigen::metrics {

/// Whether the read side (.read() / .snapshot()) is part of the public API.
enum class AccessPolicy : std::uint8_t {
    Readable,
    WriteOnly,
};

} // namespace nexigen::metrics

// Vocabulary table for core::enum_name/enum_value (change refactor-enum-traits-helper).
namespace nexigen::core {

template <> struct EnumTraits<nexigen::metrics::AccessPolicy> {
    struct Entry {
        nexigen::metrics::AccessPolicy value;
        std::string_view name;
    };
    static constexpr std::array<Entry, 2> entries{{
        {nexigen::metrics::AccessPolicy::Readable, "Readable"},
        {nexigen::metrics::AccessPolicy::WriteOnly, "WriteOnly"},
    }};
};

} // namespace nexigen::core

namespace nexigen::metrics {

[[nodiscard]] constexpr std::string_view to_string(AccessPolicy policy) noexcept {
    return core::enum_name(policy);
}

[[nodiscard]] constexpr bool from_string(std::string_view name, AccessPolicy& out) noexcept {
    return core::enum_value(name, out);
}

inline void from_json(const nlohmann::json& j, AccessPolicy& out) noexcept {
    core::mandate::enum_from_json(j, out);
}

inline void to_json(nlohmann::json& j, const AccessPolicy accessPolicy) {
    core::mandate::enum_to_json(j, accessPolicy);
}

} // namespace nexigen::metrics
