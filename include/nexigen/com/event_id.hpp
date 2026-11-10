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

/// Application-defined event tag.
class EventId {
public:
    explicit EventId(std::uint64_t value) noexcept : inner_(value) {}

    /// The numeric value of the event id.
    [[nodiscard]] std::uint64_t as_value() const noexcept {
        return inner_.as_value();
    }

    /// Cold-path diagnostic (mandate 9).
    [[nodiscard]] std::string to_string() const {
        return "EventId(" + std::to_string(as_value()) + ")";
    }

    /// The underlying transport handle (module-internal surface only).
    [[nodiscard]] const iox2::EventId& handle() const noexcept { return inner_; }

private:
    iox2::EventId inner_;
};

/// Free helper (mandate 9): mirrors the member.
[[nodiscard]] inline std::string to_string(const EventId& id) {
    return id.to_string();
}

} // namespace nexigen::com

/// The event id formats as its diagnostic string (mandate 11).
template <> struct std::formatter<nexigen::com::EventId> : std::formatter<std::string_view> {
    template <class Ctx>
    auto format(const nexigen::com::EventId& id, Ctx& ctx) const {
        const std::string text = id.to_string();
        return std::formatter<std::string_view>::format(text, ctx);
    }
};
