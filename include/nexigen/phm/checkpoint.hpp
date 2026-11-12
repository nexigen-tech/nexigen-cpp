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

/// @brief Client-side emitter for one process's checkpoint channel.
///
/// Each `emit` publishes an iceoryx2 event whose event id is the checkpoint id;
/// the daemon's listener receives it as an activation. The handle owns exactly
/// one `com::EventNotifier` created on the caller's `com::Node`, is movable but
/// not copyable, and must not outlive that node. `emit` is the hot path (one
/// non-blocking notification, no allocation); `to_string` is a cold-path
/// diagnostic.
///
/// @note Spec `phm/health-channel`, requirement "Checkpoint emission": every
///       checkpoint carries an id and reaches the daemon through the event
///       pattern, with no socket and no serialization step.
class CheckpointEmitter {
public:
    /// @brief Open (or create) the checkpoint channel service on the node.
    ///
    /// @param node Node that owns this process's transport membership; must
    ///        outlive the returned emitter.
    /// @param name Valid service name shared with the daemon's listener.
    /// @return The constructed emitter, or the mapped `com::Error` reported by
    ///         `com::EventNotifier::create` when the service or the notifier
    ///         cannot be created.
    /// @note Spec `phm/health-channel`, requirement "Checkpoint emission".
    [[nodiscard]] static com::Result<CheckpointEmitter>
    create(com::Node& node, const com::ServiceName& name) noexcept {
        auto notifier = com::EventNotifier::create(node, name);
        if (!notifier) {
            return std::unexpected(notifier.error());
        }
        return CheckpointEmitter{name, std::move(*notifier)};
    }

    /// @brief Copy construction is deleted: the emitter owns the transport
    ///        handle.
    CheckpointEmitter(const CheckpointEmitter&) = delete;

    /// @brief Copy assignment is deleted: the emitter owns the transport
    ///        handle.
    CheckpointEmitter& operator=(const CheckpointEmitter&) = delete;

    /// @brief Move-construct an emitter, transferring the notifier handle.
    ///
    /// The moved-from emitter no longer owns a channel and must not emit.
    CheckpointEmitter(CheckpointEmitter&&) noexcept = default;

    /// @brief Move-assign an emitter, transferring the notifier handle.
    ///
    /// @return `*this`.
    CheckpointEmitter& operator=(CheckpointEmitter&&) noexcept = default;

    /// @brief Emit one checkpoint.
    ///
    /// @param checkpoint_id Checkpoint identifier from the manifest, encoded
    ///        as the 64-bit event id.
    /// @return Empty `com::Status` when the notification was delivered, or the
    ///         mapped `com::Error` from the notifier (for example
    ///         `ConnectionFailure`).
    /// @note Spec `phm/health-channel`, requirement "Checkpoint emission": the
    ///       daemon's listener receives an activation identifying
    ///       `checkpoint_id`. The notification is non-blocking, so a slow
    ///       listener never stalls the emitting thread.
    [[nodiscard]] com::Status emit(std::uint32_t checkpoint_id) noexcept {
        return notifier_.notify_with(com::EventId{static_cast<std::uint64_t>(checkpoint_id)});
    }

    /// @brief Cold-path diagnostic naming the channel.
    ///
    /// @return Text of the form `CheckpointEmitter(service=<name>)`.
    /// @note Mandate 9 (universal `to_string`); allocates, so never call it on
    ///       the checkpoint hot path. Spec `phm/health-channel`, requirement
    ///       "Checkpoint emission".
    [[nodiscard]] std::string to_string() const {
        return std::format("CheckpointEmitter(service={})", service_name_.to_string());
    }

    /// @brief The underlying notifier (module-internal surface).
    ///
    /// @return Reference to the owned notifier, valid for the emitter's
    ///         lifetime.
    /// @note Spec `phm/health-channel`, requirement "Checkpoint emission".
    [[nodiscard]] const com::EventNotifier& notifier() const noexcept { return notifier_; }

private:
    /// @brief Adopt an opened service name and notifier.
    ///
    /// @param name Service name retained for `to_string()`.
    /// @param notifier Notifier handle transferred into the emitter.
    /// @note Private by design: the only supported construction path is
    ///       `create()`.
    CheckpointEmitter(com::ServiceName name, com::EventNotifier notifier) noexcept
        : service_name_(std::move(name)), notifier_(std::move(notifier)) {}

    com::ServiceName service_name_; ///< Service identity rendered by `to_string()`.
    com::EventNotifier notifier_;   ///< Owned notification handle.
};

/// @brief Free helper mirroring `CheckpointEmitter::to_string()` (mandate 9).
///
/// @param emitter Emitter to render.
/// @return The emitter's diagnostic text.
/// @note Mandate 9; allocates, so cold path only.
[[nodiscard]] inline std::string to_string(const CheckpointEmitter& emitter) {
    return emitter.to_string();
}

} // namespace nexigen::phm

/// @brief Formats a `CheckpointEmitter` as its diagnostic string.
///
/// Delegates to the member `to_string`, so `std::format("{}", emitter)` equals
/// the member output.
///
/// @note Mandate 11 (universal `std::formatter`); cold path only.
template <>
struct std::formatter<nexigen::phm::CheckpointEmitter> : std::formatter<std::string_view> {
    /// @brief Write the emitter's diagnostic text into the format context.
    ///
    /// @tparam Ctx `std::format` context type, for example
    ///         `std::format_context`.
    /// @param emitter Emitter to format.
    /// @param ctx Output context supplied by `std::format`.
    /// @return Iterator one past the last written character.
    template <class Ctx>
    auto format(const nexigen::phm::CheckpointEmitter& emitter, Ctx& ctx) const {
        const std::string text = emitter.to_string();
        return std::formatter<std::string_view>::format(text, ctx);
    }
};
