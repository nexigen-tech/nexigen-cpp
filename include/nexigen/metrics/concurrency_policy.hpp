// nexigen::metrics — ConcurrencyPolicy enum.
#pragma once

#include <array>
#include <cstdint>
#include <string>
#include <string_view>

#include <nlohmann/json.hpp>

#include <nexigen/core/enum_traits.hpp>
namespace nexigen::metrics {

/// How an instrument is written: by one thread, or sharded across threads.
enum class ConcurrencyPolicy : std::uint8_t {
    SingleThreaded,
    MultiThreaded,
};

} // namespace nexigen::metrics

// Vocabulary table for core::enum_name/enum_value (change refactor-enum-traits-helper).
namespace nexigen::core {

template <> struct EnumTraits<nexigen::metrics::ConcurrencyPolicy> {
    struct Entry {
        nexigen::metrics::ConcurrencyPolicy value;
        std::string_view name;
    };
    static constexpr std::array<Entry, 2> entries{{
        {nexigen::metrics::ConcurrencyPolicy::SingleThreaded, "SingleThreaded"},
        {nexigen::metrics::ConcurrencyPolicy::MultiThreaded, "MultiThreaded"},
    }};
};

} // namespace nexigen::core

namespace nexigen::metrics {

[[nodiscard]] constexpr std::string_view to_string(ConcurrencyPolicy policy) noexcept {
    return core::enum_name(policy);
}

[[nodiscard]] constexpr bool from_string(std::string_view name, ConcurrencyPolicy& out) noexcept {
    return core::enum_value(name, out);
}

inline void from_json(const nlohmann::json& j, ConcurrencyPolicy& out) noexcept {
    core::mandate::enum_from_json(j, out);
}

inline void to_json(nlohmann::json& j, const ConcurrencyPolicy concurrencyPolicy) {
    core::mandate::enum_to_json(j, concurrencyPolicy);
}

} // namespace nexigen::metrics
