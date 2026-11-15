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
#include <nexigen/phm/checkpoint_claim.hpp>

namespace nexigen::phm {

/// @brief Publishes one process's identity claim and wakes the daemon.
///
/// @note Spec `phm/identity`, requirement "Checkpoint claim channel".
class ClaimPublisher {
public:
    /// @brief Open the claim channel and its wake event.
    [[nodiscard]] static com::Result<ClaimPublisher>
    create(com::Node& node, const com::ServiceName& channel_name,
           const com::ServiceName& event_name) noexcept {
        auto publisher = com::Publisher<CheckpointClaim>::create(node, channel_name);
        if (!publisher) {
            return std::unexpected(publisher.error());
        }
        auto event = com::EventNotifier::create(node, event_name);
        if (!event) {
            return std::unexpected(event.error());
        }
        return ClaimPublisher{channel_name, std::move(*publisher), std::move(*event)};
    }

    ClaimPublisher(const ClaimPublisher&) = delete;
    ClaimPublisher& operator=(const ClaimPublisher&) = delete;
    ClaimPublisher(ClaimPublisher&&) noexcept = default;
    ClaimPublisher& operator=(ClaimPublisher&&) noexcept = default;

    /// @brief Publish the claim and wake the daemon.
    [[nodiscard]] com::Status claim(std::string_view name, std::uint32_t pid,
                                    std::uint32_t uid) noexcept {
        CheckpointClaim payload{};
        std::snprintf(payload.name, sizeof(payload.name), "%.*s", static_cast<int>(name.size()),
                      name.data());
        payload.pid = pid;
        payload.uid = uid;
        const com::Status sent = publisher_.send_copy(payload);
        if (!sent) {
            return sent;
        }
        return event_.notify();
    }

    /// @brief Cold-path diagnostic rendering.
    [[nodiscard]] std::string to_string() const {
        return std::format("ClaimPublisher(channel={})", channel_.to_string());
    }

private:
    ClaimPublisher(com::ServiceName channel, com::Publisher<CheckpointClaim> publisher,
                   com::EventNotifier event) noexcept
        : channel_(std::move(channel)), publisher_(std::move(publisher)), event_(std::move(event)) {
    }

    com::ServiceName channel_;
    com::Publisher<CheckpointClaim> publisher_;
    com::EventNotifier event_;
};

/// @brief Free helper mirroring `ClaimPublisher::to_string()` (mandate 9).
[[nodiscard]] inline std::string to_string(const ClaimPublisher& publisher) {
    return publisher.to_string();
}

} // namespace nexigen::phm
