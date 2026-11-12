// nexigen::com — event identifier (change add-com-module;
// spec com/service-management / com/publish-subscribe "Event notification").
//
// Thin wrapper over iox2::EventId: the application-defined tag of an event
// notification (for example a checkpoint id or a trigger code).
#pragma once

#include <cstdint>
#include <format>
#include <string>

#include <iox2/iceoryx2.hpp>

namespace nexigen::com {

/// @brief Application-defined tag of an event notification.
///
/// Thin value wrapper over `iox2::EventId` so business code never names the
/// transport type. The numeric value carries application meaning, for example
/// a checkpoint id or a trigger code; notifying with it lets a listener
/// dispatch without a second lookup.
///
/// Copies are cheap; the wrapper holds no resources.
///
/// @note Spec `com/publish-subscribe`, requirement "Event notification on
///       publish"; `com/service-management`, requirement
///       "Protocol-independent boundary".
class EventId {
public:
    /// @brief Construct an event id from its numeric tag.
    ///
    /// @param value Application-defined tag.
    explicit EventId(std::uint64_t value) noexcept : inner_(value) {}

    /// @brief The numeric value of the event id.
    ///
    /// @return The tag passed at construction.
    [[nodiscard]] std::uint64_t as_value() const noexcept { return inner_.as_value(); }

    /// @brief Render the cold-path diagnostic string.
    ///
    /// @return `EventId(<value>)`, matching the free helper and the formatter.
    /// @note Mandate 9 (universal `to_string`); allocates, so cold path only.
    [[nodiscard]] std::string to_string() const {
        return "EventId(" + std::to_string(as_value()) + ")";
    }

    /// @brief The underlying transport handle.
    ///
    /// @return Const reference to the wrapped `iox2::EventId`.
    /// @note Module-internal surface only: business code must stay
    ///       transport-agnostic (spec `com/service-management`, requirement
    ///       "Protocol-independent boundary").
    [[nodiscard]] const iox2::EventId& handle() const noexcept { return inner_; }

private:
    iox2::EventId inner_;
};

/// @brief Free helper mirroring `EventId::to_string()`.
///
/// @param id Event id to render.
/// @return The diagnostic string.
/// @note Mandate 9 (universal `to_string`); allocates, so cold path only.
[[nodiscard]] inline std::string to_string(const EventId& id) {
    return id.to_string();
}

} // namespace nexigen::com

/// @brief Formats an `EventId` as its diagnostic string.
///
/// @note Mandate 11 (universal `std::formatter`); delegates to
///       `EventId::to_string()`, which allocates, so cold path only.
template <> struct std::formatter<nexigen::com::EventId> : std::formatter<std::string_view> {
    /// @brief Write the event id's diagnostic text into the format context.
    ///
    /// @tparam Ctx `std::format` context type.
    /// @param id Event id to format.
    /// @param ctx Output context supplied by `std::format`.
    /// @return Iterator one past the last written character.
    template <class Ctx> auto format(const nexigen::com::EventId& id, Ctx& ctx) const {
        const std::string text = id.to_string();
        return std::formatter<std::string_view>::format(text, ctx);
    }
};
