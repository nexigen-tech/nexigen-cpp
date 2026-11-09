// nexigen::hub — StreamKind enum.
#pragma once
#include <array>
#include <cstdint>
#include <string>
#include <string_view>

#include <nexigen/core/enum_traits.hpp>
namespace nexigen::hub {

enum class StreamKind : std::uint8_t {
    App,
    Metrics,
    Traces,
    Chrono,
    Stdout,
    Stderr,
    Runner,
};

} // namespace nexigen::hub

// Vocabulary table for core::enum_name/enum_value (change refactor-enum-traits-helper).
namespace nexigen::core {

template <> struct EnumTraits<nexigen::hub::StreamKind> {
    struct Entry {
        nexigen::hub::StreamKind value;
        std::string_view name;
    };
    static constexpr std::array<Entry, 7> entries{{
        {nexigen::hub::StreamKind::App, "app"},
        {nexigen::hub::StreamKind::Metrics, "metrics"},
        {nexigen::hub::StreamKind::Traces, "traces"},
        {nexigen::hub::StreamKind::Chrono, "chrono"},
        {nexigen::hub::StreamKind::Stdout, "stdout"},
        {nexigen::hub::StreamKind::Stderr, "stderr"},
        {nexigen::hub::StreamKind::Runner, "runner"},
    }};
    static constexpr std::string_view fallback = "unknown";
};

} // namespace nexigen::core

namespace nexigen::hub {

[[nodiscard]] constexpr std::string_view to_string(StreamKind kind) noexcept {
    return core::enum_name(kind);
}

[[nodiscard]] constexpr bool from_string(std::string_view name, StreamKind& out) noexcept {
    return core::enum_value(name, out);
}

} // namespace nexigen::hub
