// nexigen::com — identity of a fired WaitSet source (change add-com-waitset;
// spec com/waitset "Callback dispatch and source identity").
//
// The value a WaitSet callback receives: it identifies which attachment fired
// and, for a deadline attachment, whether the wake-up was the event or the
// elapsed deadline. Move-only, mirroring the underlying iceoryx2 id.
#pragma once

#include <cstddef>
#include <format>
#include <string>
#include <utility>

#include <iox2/iceoryx2.hpp>

#include <nexigen/com/wait_set_guard.hpp>

namespace nexigen::com {

/// @brief Identifies one WaitSet attachment inside the callback.
///
/// The value a `WaitSet` callback receives: it names which attachment fired
/// and, for a deadline attachment, whether the wake-up was the event or the
/// elapsed deadline. It never exposes an `iox2::` type to business code.
///
/// The id is move-only and mirrors the transport id. It is valid during the
/// callback invocation and refers to the guard that produced it.
///
/// @note Spec `com/waitset`, requirements "Callback dispatch and source
///       identity" and "Deadline attachment".
class WaitSetAttachmentId {
public:
    /// @brief Copying is disabled: the id mirrors a move-only transport value.
    WaitSetAttachmentId(const WaitSetAttachmentId&) = delete;

    /// @brief Copy assignment is disabled: the id is move-only.
    WaitSetAttachmentId& operator=(const WaitSetAttachmentId&) = delete;

    /// @brief Move-construct an id, transferring the transport value.
    ///
    /// The moved-from id must not be queried.
    WaitSetAttachmentId(WaitSetAttachmentId&&) noexcept = default;

    /// @brief Move-assign an id, transferring the transport value.
    ///
    /// @return `*this`.
    WaitSetAttachmentId& operator=(WaitSetAttachmentId&&) noexcept = default;

    /// @brief Build the id that corresponds to a live attachment guard.
    ///
    /// @param guard Attachment guard whose source should be queried; must
    ///        outlive the returned id's use.
    /// @return The attachment id for that guard.
    /// @note Spec `com/waitset`, requirement "Callback dispatch and source
    ///       identity".
    [[nodiscard]] static WaitSetAttachmentId from_guard(const WaitSetGuard& guard) noexcept {
        return WaitSetAttachmentId{
            iox2::WaitSetAttachmentId<iox2::ServiceType::Ipc>::from_guard(guard.handle())};
    }

    /// @brief True when the source fired because a notification/event arrived.
    ///
    /// @param guard Guard of the attachment to query.
    /// @return `true` when the event triggered this activation.
    /// @note Spec `com/waitset`, requirement "Event notification attachment".
    [[nodiscard]] bool has_event_from(const WaitSetGuard& guard) const noexcept {
        return inner_.has_event_from(guard.handle());
    }

    /// @brief True when the source fired because its deadline elapsed.
    ///
    /// @param guard Guard of the deadline attachment to query.
    /// @return `true` when the deadline triggered this activation.
    /// @note Spec `com/waitset`, requirement "Deadline attachment": the result
    ///       distinguishes a missed deadline from an arriving event.
    [[nodiscard]] bool has_missed_deadline(const WaitSetGuard& guard) const noexcept {
        return inner_.has_missed_deadline(guard.handle());
    }

    /// @brief Stable hash for use as a map key.
    ///
    /// @return Hash of the underlying transport id, suitable for dispatch
    ///         tables keyed by attachment.
    [[nodiscard]] std::size_t hash() const noexcept { return inner_.hash(); }

    /// @brief Render the cold-path diagnostic string.
    ///
    /// @return The constant marker `WaitSetAttachmentId()`; source identity is
    ///         queried through the guard predicates.
    /// @note Mandate 9 (universal `to_string`); `com/waitset`, requirement
    ///       "WaitSet diagnostics". Allocates, so cold path only.
    [[nodiscard]] std::string to_string() const { return "WaitSetAttachmentId()"; }

private:
    friend class WaitSet;

    /// @brief Adopt a transport attachment id from the wait entry point.
    ///
    /// @param inner Transport attachment id.
    /// @note Private by design: the id arrives through the callback adapter.
    explicit WaitSetAttachmentId(iox2::WaitSetAttachmentId<iox2::ServiceType::Ipc> inner) noexcept
        : inner_(std::move(inner)) {}

    iox2::WaitSetAttachmentId<iox2::ServiceType::Ipc> inner_; ///< Owned transport id.
};

/// @brief Free helper mirroring `WaitSetAttachmentId::to_string()`.
///
/// @param id Attachment id to render.
/// @return The id's diagnostic string.
/// @note Mandate 9 (universal `to_string`); allocates, so cold path only.
[[nodiscard]] inline std::string to_string(const WaitSetAttachmentId& id) {
    return id.to_string();
}

} // namespace nexigen::com

/// @brief Formats a `WaitSetAttachmentId` as its diagnostic string.
///
/// @note Mandate 11 (universal `std::formatter`); delegates to
///       `WaitSetAttachmentId::to_string()`, which allocates, so cold path
///       only.
template <>
struct std::formatter<nexigen::com::WaitSetAttachmentId> : std::formatter<std::string_view> {
    /// @brief Write the attachment id's diagnostic text into the format context.
    ///
    /// @tparam Ctx `std::format` context type.
    /// @param id Attachment id to format.
    /// @param ctx Output context supplied by `std::format`.
    /// @return Iterator one past the last written character.
    template <class Ctx> auto format(const nexigen::com::WaitSetAttachmentId& id, Ctx& ctx) const {
        const std::string text = id.to_string();
        return std::formatter<std::string_view>::format(text, ctx);
    }
};
