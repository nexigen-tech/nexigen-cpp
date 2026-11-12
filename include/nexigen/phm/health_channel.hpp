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

/// @brief Client side of one health channel.
///
/// Publishes a `HealthState` transition by notifying an iceoryx2 event whose
/// event id is the state value, so the daemon's listener observes the change
/// without polling, sockets, or serialization. The handle owns exactly one
/// `com::EventNotifier` created on the caller's `com::Node`, is movable but not
/// copyable, and must not outlive that node. `set_state` is the hot path (one
/// non-blocking notification); `to_string` is a cold-path diagnostic.
///
/// @note Spec `phm/health-channel`, requirement "Health status notification".
class HealthChannel {
public:
    /// @brief Open (or create) the channel service on the node.
    ///
    /// @param node Node that owns this process's transport membership; must
    ///        outlive the returned channel.
    /// @param name Valid service name shared with the daemon's listener.
    /// @return The constructed channel, or the mapped `com::Error` reported by
    ///         `com::EventNotifier::create` when the service or the notifier
    ///         cannot be created.
    /// @note Spec `phm/health-channel`, requirement "Health status
    ///       notification".
    [[nodiscard]] static com::Result<HealthChannel> create(com::Node& node,
                                                           const com::ServiceName& name) noexcept {
        auto notifier = com::EventNotifier::create(node, name);
        if (!notifier) {
            return std::unexpected(notifier.error());
        }
        return HealthChannel{name, std::move(*notifier)};
    }

    /// @brief Copy construction is deleted: the channel owns the transport
    ///        handle.
    HealthChannel(const HealthChannel&) = delete;

    /// @brief Copy assignment is deleted: the channel owns the transport
    ///        handle.
    HealthChannel& operator=(const HealthChannel&) = delete;

    /// @brief Move-construct a channel, transferring the notifier handle.
    ///
    /// The moved-from channel no longer owns a channel and must not publish.
    HealthChannel(HealthChannel&&) noexcept = default;

    /// @brief Move-assign a channel, transferring the notifier handle.
    ///
    /// @return `*this`.
    HealthChannel& operator=(HealthChannel&&) noexcept = default;

    /// @brief Publish a state transition.
    ///
    /// The cached last state advances only after the notification is accepted,
    /// so `state()` never reports a state the daemon did not receive.
    ///
    /// @param state New health state, encoded as the event id.
    /// @return Empty `com::Status` when the transition was delivered, or the
    ///         mapped `com::Error` from the notifier.
    /// @note Spec `phm/health-channel`, requirement "Health status
    ///       notification": the daemon's listener observes the new state
    ///       promptly without polling.
    [[nodiscard]] com::Status set_state(HealthState state) noexcept {
        const auto notified =
            notifier_.notify_with(com::EventId{static_cast<std::uint64_t>(state)});
        if (notified.has_value()) {
            state_ = state;
        }
        return notified;
    }

    /// @brief The last state this channel published.
    ///
    /// @return Cached state; `HealthState::Ok` until the first successful
    ///         `set_state`.
    /// @note Spec `phm/health-channel`, requirement "Health status
    ///       notification".
    [[nodiscard]] HealthState state() const noexcept { return state_; }

    /// @brief Cold-path diagnostic naming the channel and its cached state.
    ///
    /// @return Text of the form `HealthChannel(service=<name>, state=<state>)`.
    /// @note Mandate 9 (universal `to_string`); allocates, so cold path only.
    ///       Spec `phm/health-channel`, requirement "Health channel
    ///       diagnostics".
    [[nodiscard]] std::string to_string() const {
        return std::format("HealthChannel(service={}, state={})", service_name_.to_string(),
                           nexigen::phm::to_string(state_));
    }

    /// @brief The underlying notifier (module-internal surface).
    ///
    /// The daemon tests pair channels with listeners through the service name.
    ///
    /// @return Reference to the owned notifier, valid for the channel's
    ///         lifetime.
    /// @note Spec `phm/health-channel`, requirement "Health status
    ///       notification".
    [[nodiscard]] const com::EventNotifier& notifier() const noexcept { return notifier_; }

private:
    /// @brief Adopt an opened service name and notifier.
    ///
    /// @param name Service name retained for `to_string()`.
    /// @param notifier Notifier handle transferred into the channel.
    /// @note Private by design: the only supported construction path is
    ///       `create()`.
    HealthChannel(com::ServiceName name, com::EventNotifier notifier) noexcept
        : service_name_(std::move(name)), notifier_(std::move(notifier)) {}

    com::ServiceName service_name_;      ///< Service identity rendered by `to_string()`.
    com::EventNotifier notifier_;        ///< Owned notification handle.
    HealthState state_{HealthState::Ok}; ///< Last state accepted by the notifier.
};

/// @brief Free helper mirroring `HealthChannel::to_string()` (mandate 9).
///
/// @param channel Channel to render.
/// @return The channel's diagnostic text.
/// @note Mandate 9; allocates, so cold path only.
[[nodiscard]] inline std::string to_string(const HealthChannel& channel) {
    return channel.to_string();
}

} // namespace nexigen::phm

/// @brief Formats a `HealthChannel` as its diagnostic string.
///
/// Delegates to the member `to_string`, so `std::format("{}", channel)` equals
/// the member output (naming the channel and its state).
///
/// @note Mandate 11 (universal `std::formatter`); cold path only. Spec
///       `phm/health-channel`, requirement "Health channel diagnostics".
template <> struct std::formatter<nexigen::phm::HealthChannel> : std::formatter<std::string_view> {
    /// @brief Write the channel's diagnostic text into the format context.
    ///
    /// @tparam Ctx `std::format` context type, for example
    ///         `std::format_context`.
    /// @param channel Channel to format.
    /// @param ctx Output context supplied by `std::format`.
    /// @return Iterator one past the last written character.
    template <class Ctx> auto format(const nexigen::phm::HealthChannel& channel, Ctx& ctx) const {
        const std::string text = channel.to_string();
        return std::formatter<std::string_view>::format(text, ctx);
    }
};
