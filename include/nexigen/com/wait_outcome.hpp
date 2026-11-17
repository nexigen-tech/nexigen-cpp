// nexigen::com — result of a consumer-set wait.
#pragma once

#include <array>
#include <cstdint>
#include <format>
#include <string_view>

#include <nlohmann/json.hpp>

#include <nexigen/core/enum_traits.hpp>
#include <nexigen/core/serialization_mandate.hpp>

namespace nexigen::com {

/// @brief Why an `EventConsumerSet` wait returned.
///
/// @note Spec `com/event-system`, requirement "Consumer waiting and
///       multiplexing".
enum class WaitOutcome : std::uint8_t {
    Events,  ///< At least one consumer was drained.
    Timeout, ///< The requested timeout elapsed without an event.
    WokenUp, ///< `wake_up()` interrupted the wait.
};

} // namespace nexigen::com

namespace nexigen::core {

/// @brief Vocabulary table binding `WaitOutcome` to canonical names.
template <> struct EnumTraits<nexigen::com::WaitOutcome> {
    /// @brief One value/name pair.
    struct Entry {
        nexigen::com::WaitOutcome value; ///< Enumerator.
        std::string_view name;           ///< Canonical name.
    };
    /// @brief Canonical table.
    static constexpr std::array<Entry, 3> entries{{
        {nexigen::com::WaitOutcome::Events, "Events"},
        {nexigen::com::WaitOutcome::Timeout, "Timeout"},
        {nexigen::com::WaitOutcome::WokenUp, "WokenUp"},
    }};
};

} // namespace nexigen::core

namespace nexigen::com {

/// @brief Canonical name of a wait outcome.
/// @param value Outcome to render.
/// @return Canonical name.
[[nodiscard]] constexpr std::string_view to_string(WaitOutcome value) noexcept {
    return core::enum_name(value);
}

/// @brief Parse a canonical wait-outcome name.
/// @param name Candidate name.
/// @param out Destination, unchanged on failure.
/// @return `true` when the name matched.
[[nodiscard]] constexpr bool from_string(std::string_view name, WaitOutcome& out) noexcept {
    return core::enum_value(name, out);
}

/// @brief ADL JSON writer for a wait outcome.
/// @param j Destination JSON value.
/// @param value Outcome to serialize.
inline void to_json(nlohmann::json& j, const WaitOutcome value) {
    core::mandate::enum_to_json(j, value);
}

/// @brief ADL JSON reader for a wait outcome, strictly non-throwing.
/// @param j Source JSON value.
/// @param value Destination, unchanged on failure.
inline void from_json(const nlohmann::json& j, WaitOutcome& value) noexcept {
    core::mandate::enum_from_json(j, value);
}

} // namespace nexigen::com

/// @brief Formats a wait outcome as its canonical name.
template <> struct std::formatter<nexigen::com::WaitOutcome> : std::formatter<std::string_view> {
    /// @brief Write the outcome name into the format context.
    /// @tparam Ctx Format context type.
    /// @param value Outcome to format.
    /// @param ctx Output context.
    /// @return Iterator past the last written character.
    template <class Ctx> auto format(const nexigen::com::WaitOutcome value, Ctx& ctx) const {
        return std::formatter<std::string_view>::format(nexigen::com::to_string(value), ctx);
    }
};
