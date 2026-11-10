// nexigen::phm — health channel client (change add-phm;
// spec phm/health-channel; design D2).
//
// The per-process channel through which an application reports its health
// state. A state transition is an iceoryx2 event carrying the HealthState
// value as the event id — no sockets, no serialization.
#pragma once

#include <cstdint>
#include <format>
#include <string>
#include <utility>

#include <nexigen/com/error.hpp>
#include <nexigen/com/event.hpp>
#include <nexigen/com/event_id.hpp>
#include <nexigen/com/node.hpp>
#include <nexigen/com/service_name.hpp>
#include <nexigen/phm/health_state.hpp>

namespace nexigen::phm {

/// Client side of one health channel.
class HealthChannel {
public:
    /// Open (or create) the channel service on the node.
    [[nodiscard]] static com::Result<HealthChannel> create(
        com::Node& node, const com::ServiceName& name) noexcept {
        auto notifier = com::EventNotifier::create(node, name);
        if (!notifier) {
            return std::unexpected(notifier.error());
        }
        return HealthChannel{name, std::move(*notifier)};
    }

    HealthChannel(const HealthChannel&) = delete;
    HealthChannel& operator=(const HealthChannel&) = delete;
    HealthChannel(HealthChannel&&) noexcept = default;
    HealthChannel& operator=(HealthChannel&&) noexcept = default;

    /// Publish a state transition; the daemon's listener observes the new
    /// state promptly without polling.
    [[nodiscard]] com::Status set_state(HealthState state) noexcept {
        const auto notified =
            notifier_.notify_with(com::EventId{static_cast<std::uint64_t>(state)});
        if (notified.has_value()) {
            state_ = state;
        }
        return notified;
    }

    /// The last state this channel published.
    [[nodiscard]] HealthState state() const noexcept { return state_; }

    /// Cold-path diagnostic (mandate 9): names the channel and its state.
    [[nodiscard]] std::string to_string() const {
        return std::format("HealthChannel(service={}, state={})", service_name_.to_string(),
                           nexigen::phm::to_string(state_));
    }

    /// The underlying notifier (module-internal surface; the daemon tests
    /// pair channels with listeners through the service name).
    [[nodiscard]] const com::EventNotifier& notifier() const noexcept { return notifier_; }

private:
    HealthChannel(com::ServiceName name, com::EventNotifier notifier) noexcept
        : service_name_(std::move(name)), notifier_(std::move(notifier)) {}

    com::ServiceName service_name_;
    com::EventNotifier notifier_;
    HealthState state_{HealthState::Ok};
};

/// Free helper (mandate 9): mirrors the member.
[[nodiscard]] inline std::string to_string(const HealthChannel& channel) {
    return channel.to_string();
}

} // namespace nexigen::phm

/// The channel formats as its diagnostic string (mandate 11).
template <> struct std::formatter<nexigen::phm::HealthChannel> : std::formatter<std::string_view> {
    template <class Ctx>
    auto format(const nexigen::phm::HealthChannel& channel, Ctx& ctx) const {
        const std::string text = channel.to_string();
        return std::formatter<std::string_view>::format(text, ctx);
    }
};
