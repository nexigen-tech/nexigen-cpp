// nexigen::phm — checkpoint emission client (change add-phm;
// spec phm/health-channel "Checkpoint emission"; design D2).
//
// The client-side API a process calls at each step of its control flow: each
// checkpoint carries an id and is delivered to the daemon through the
// iceoryx2 event pattern.
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

namespace nexigen::phm {

/// Emits checkpoints for one supervised process.
class CheckpointEmitter {
public:
    /// Open (or create) the checkpoint channel service on the node.
    [[nodiscard]] static com::Result<CheckpointEmitter>
    create(com::Node& node, const com::ServiceName& name) noexcept {
        auto notifier = com::EventNotifier::create(node, name);
        if (!notifier) {
            return std::unexpected(notifier.error());
        }
        return CheckpointEmitter{name, std::move(*notifier)};
    }

    CheckpointEmitter(const CheckpointEmitter&) = delete;
    CheckpointEmitter& operator=(const CheckpointEmitter&) = delete;
    CheckpointEmitter(CheckpointEmitter&&) noexcept = default;
    CheckpointEmitter& operator=(CheckpointEmitter&&) noexcept = default;

    /// Emit one checkpoint; the daemon's listener receives an activation
    /// identifying `checkpoint_id`.
    [[nodiscard]] com::Status emit(std::uint32_t checkpoint_id) noexcept {
        return notifier_.notify_with(com::EventId{static_cast<std::uint64_t>(checkpoint_id)});
    }

    /// Cold-path diagnostic (mandate 9): names the channel.
    [[nodiscard]] std::string to_string() const {
        return std::format("CheckpointEmitter(service={})", service_name_.to_string());
    }

    /// The underlying notifier (module-internal surface).
    [[nodiscard]] const com::EventNotifier& notifier() const noexcept { return notifier_; }

private:
    CheckpointEmitter(com::ServiceName name, com::EventNotifier notifier) noexcept
        : service_name_(std::move(name)), notifier_(std::move(notifier)) {}

    com::ServiceName service_name_;
    com::EventNotifier notifier_;
};

/// Free helper (mandate 9): mirrors the member.
[[nodiscard]] inline std::string to_string(const CheckpointEmitter& emitter) {
    return emitter.to_string();
}

} // namespace nexigen::phm

/// The emitter formats as its diagnostic string (mandate 11).
template <>
struct std::formatter<nexigen::phm::CheckpointEmitter> : std::formatter<std::string_view> {
    template <class Ctx>
    auto format(const nexigen::phm::CheckpointEmitter& emitter, Ctx& ctx) const {
        const std::string text = emitter.to_string();
        return std::formatter<std::string_view>::format(text, ctx);
    }
};
