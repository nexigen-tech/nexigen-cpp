// nexigen::com — result of a successful event publication.
#pragma once

#include <array>
#include <cstdint>
#include <format>
#include <string_view>

#include <nlohmann/json.hpp>

#include <nexigen/core/enum_traits.hpp>
#include <nexigen/core/serialization_mandate.hpp>

namespace nexigen::com {

/// @brief Whether the sample was published and its wake notification delivered.
///
/// The error arm of `EventProducer::publish` is reserved for send failure; this
/// enum distinguishes a successful send with and without a delivered wake.
///
/// @note Spec `com/event-system`, requirement "Event production and publish
///       outcome".
enum class PublishOutcome : std::uint8_t {
    Notified,     ///< Sample published and wake delivered.
    NotifyFailed, ///< Sample published, wake not delivered.
};

} // namespace nexigen::com

namespace nexigen::core {

/// @brief Vocabulary table binding `PublishOutcome` to canonical names.
template <> struct EnumTraits<nexigen::com::PublishOutcome> {
    /// @brief One value/name pair.
    struct Entry {
        nexigen::com::PublishOutcome value; ///< Enumerator.
        std::string_view name;              ///< Canonical name.
    };
    /// @brief Canonical table.
    static constexpr std::array<Entry, 2> entries{{
        {nexigen::com::PublishOutcome::Notified, "Notified"},
        {nexigen::com::PublishOutcome::NotifyFailed, "NotifyFailed"},
    }};
};

} // namespace nexigen::core

namespace nexigen::com {

/// @brief Canonical name of a publish outcome.
/// @param value Outcome to render.
/// @return Canonical name.
[[nodiscard]] constexpr std::string_view to_string(PublishOutcome value) noexcept {
    return core::enum_name(value);
}

/// @brief Parse a canonical publish-outcome name.
/// @param name Candidate name.
/// @param out Destination, unchanged on failure.
/// @return `true` when the name matched.
[[nodiscard]] constexpr bool from_string(std::string_view name, PublishOutcome& out) noexcept {
    return core::enum_value(name, out);
}

/// @brief ADL JSON writer for a publish outcome.
/// @param j Destination JSON value.
/// @param value Outcome to serialize.
inline void to_json(nlohmann::json& j, const PublishOutcome value) {
    core::mandate::enum_to_json(j, value);
}

/// @brief ADL JSON reader for a publish outcome, strictly non-throwing.
/// @param j Source JSON value.
/// @param value Destination, unchanged on failure.
inline void from_json(const nlohmann::json& j, PublishOutcome& value) noexcept {
    core::mandate::enum_from_json(j, value);
}

} // namespace nexigen::com

/// @brief Formats a publish outcome as its canonical name.
template <> struct std::formatter<nexigen::com::PublishOutcome> : std::formatter<std::string_view> {
    /// @brief Write the outcome name into the format context.
    /// @tparam Ctx Format context type.
    /// @param value Outcome to format.
    /// @param ctx Output context.
    /// @return Iterator past the last written character.
    template <class Ctx> auto format(const nexigen::com::PublishOutcome value, Ctx& ctx) const {
        return std::formatter<std::string_view>::format(nexigen::com::to_string(value), ctx);
    }
};
