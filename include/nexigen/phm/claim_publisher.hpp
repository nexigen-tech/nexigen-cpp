// nexigen::phm — app-side identity claim publisher (change
// checkpoint-identity).
#pragma once

#include <cstdint>
#include <cstdio>
#include <format>
#include <string>
#include <string_view>
#include <utility>

#include <nexigen/com/error.hpp>
#include <nexigen/com/event.hpp>
#include <nexigen/com/node.hpp>
#include <nexigen/com/publisher.hpp>
#include <nexigen/com/service_name.hpp>
#include <nexigen/core/detail/fault_injection.hpp>
#include <nexigen/phm/checkpoint_claim.hpp>

namespace nexigen::phm {

/// @brief Publishes one process's identity claim and wakes the daemon.
///
/// Owns a sample publisher on the claim channel plus the paired wake event.
/// `claim()` writes the current name/pid/uid and notifies the daemon, which
/// drains the sample and verifies it against the manifest. The handle is
/// movable but not copyable and must not outlive the node it was created on.
///
/// Names longer than `k_claim_name_length - 1` bytes are truncated; the
/// manifest validator rejects gated entries whose name would not fit, so a
/// conforming process always sends its full name.
///
/// @note Spec `phm/identity`, requirement "Checkpoint claim channel".
class ClaimPublisher {
public:
    /// @brief Open the claim channel and its wake event.
    ///
    /// @param node Node that owns this process's transport membership; must
    ///        outlive the returned publisher.
    /// @param channel_name Claim sample service shared with the daemon's
    ///        subscriber (for example `Nexigen/PHM/Claim`).
    /// @param event_name Wake event paired with the sample service.
    /// @return The constructed publisher, or the first mapped `com::Error` from
    ///         `com::Publisher::create` or `com::EventNotifier::create`.
    /// @note Spec `phm/identity`, requirement "Checkpoint claim channel".
    [[nodiscard]] static com::Result<ClaimPublisher>
    create(com::Node& node, const com::ServiceName& channel_name,
           const com::ServiceName& event_name) noexcept {
        using Point = core::fault_injection::FaultPoint;
        if (core::fault_injection::fault(Point::Phm_claim_channel)) {
            return std::unexpected(com::Error::CreationFailed);
        }
        auto publisher = com::Publisher<CheckpointClaim>::create(node, channel_name);
        if (!publisher) {
            return std::unexpected(publisher.error());
        }
        com::Result<com::EventNotifier> event = com::EventNotifier::create(node, event_name);
        const bool event_fault = core::fault_injection::fault(Point::Phm_claim_event_channel);
        if (event_fault) {
            event = std::unexpected(com::Error::CreationFailed);
        }
        if (!event) {
            return std::unexpected(event.error());
        }
        return ClaimPublisher{channel_name, std::move(*publisher), std::move(*event)};
    }

    /// @brief Copy construction is deleted: the publisher owns both handles.
    ClaimPublisher(const ClaimPublisher&) = delete;

    /// @brief Copy assignment is deleted: the publisher owns both handles.
    ClaimPublisher& operator=(const ClaimPublisher&) = delete;

    /// @brief Move-construct a publisher, transferring both handles.
    ///
    /// The moved-from publisher must not claim.
    ClaimPublisher(ClaimPublisher&&) noexcept = default;

    /// @brief Move-assign a publisher, transferring both handles.
    ///
    /// @return `*this`; the moved-from publisher must not claim.
    ClaimPublisher& operator=(ClaimPublisher&&) noexcept = default;

    /// @brief Publish the claim and wake the daemon.
    ///
    /// Copies the name into the fixed-size sample (NUL-terminated, truncated to
    /// the buffer), stores pid and uid, sends the sample, and only then fires
    /// the wake event, so a woken daemon always finds the sample in its queue.
    ///
    /// @param name Process name; must match the manifest entry to verify.
    /// @param pid Claimed process id.
    /// @param uid Claimed real user id.
    /// @return Empty `com::Status` when the sample and the notification were
    ///         delivered, otherwise the mapped `com::Error` from the sample
    ///         send or the event notify.
    /// @note Spec `phm/identity`, requirement "Checkpoint claim channel".
    [[nodiscard]] com::Status claim(std::string_view name, std::uint32_t pid,
                                    std::uint32_t uid) noexcept {
        CheckpointClaim payload{};
        std::snprintf(payload.name, sizeof(payload.name), "%.*s", static_cast<int>(name.size()),
                      name.data());
        payload.pid = pid;
        payload.uid = uid;
        com::Status sent = publisher_.send_copy(payload);
        if (core::fault_injection::fault(core::fault_injection::FaultPoint::Phm_claim_send)) {
            sent = std::unexpected(com::Error::UnableToDeliver);
        }
        if (!sent) {
            return sent;
        }
        if (core::fault_injection::fault(core::fault_injection::FaultPoint::Phm_claim_event)) {
            return std::unexpected(com::Error::ConnectionFailure);
        }
        return event_.notify();
    }

    /// @brief Cold-path diagnostic rendering.
    ///
    /// @return Text of the form `ClaimPublisher(channel=<service name>)`.
    /// @note Mandate 9 (universal `to_string`); allocates, so cold path only.
    [[nodiscard]] std::string to_string() const {
        return std::format("ClaimPublisher(channel={})", channel_.to_string());
    }

private:
    /// @brief Adopt the opened handles.
    ///
    /// @param channel Claim service name kept for diagnostics.
    /// @param publisher Opened claim sample publisher.
    /// @param event Opened wake event.
    /// @note Private by design: the only supported construction path is
    ///       `create()`.
    ClaimPublisher(com::ServiceName channel, com::Publisher<CheckpointClaim> publisher,
                   com::EventNotifier event) noexcept
        : channel_(std::move(channel)), publisher_(std::move(publisher)), event_(std::move(event)) {
    }

    com::ServiceName channel_;                  ///< Claim service name for diagnostics.
    com::Publisher<CheckpointClaim> publisher_; ///< Claim sample channel.
    com::EventNotifier event_;                  ///< Wake event paired with the channel.
};

/// @brief Free helper mirroring `ClaimPublisher::to_string()` (mandate 9).
///
/// @param publisher Publisher to render.
/// @return The member diagnostic text.
/// @note Mandate 9; allocates, so cold path only. Spec `phm/identity`,
///       requirement "Checkpoint claim channel".
[[nodiscard]] inline std::string to_string(const ClaimPublisher& publisher) {
    return publisher.to_string();
}

} // namespace nexigen::phm
