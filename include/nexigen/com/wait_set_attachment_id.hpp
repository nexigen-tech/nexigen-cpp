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

/// Identifies one WaitSet attachment inside the callback.
class WaitSetAttachmentId {
public:
    WaitSetAttachmentId(const WaitSetAttachmentId&) = delete;
    WaitSetAttachmentId& operator=(const WaitSetAttachmentId&) = delete;
    WaitSetAttachmentId(WaitSetAttachmentId&&) noexcept = default;
    WaitSetAttachmentId& operator=(WaitSetAttachmentId&&) noexcept = default;

    /// Build the id that corresponds to a live attachment guard.
    [[nodiscard]] static WaitSetAttachmentId from_guard(const WaitSetGuard& guard) noexcept {
        return WaitSetAttachmentId{
            iox2::WaitSetAttachmentId<iox2::ServiceType::Ipc>::from_guard(guard.handle())};
    }

    /// True when the source fired because a notification/event arrived.
    [[nodiscard]] bool has_event_from(const WaitSetGuard& guard) const noexcept {
        return inner_.has_event_from(guard.handle());
    }

    /// True when the source fired because its deadline elapsed (deadline
    /// attachment only).
    [[nodiscard]] bool has_missed_deadline(const WaitSetGuard& guard) const noexcept {
        return inner_.has_missed_deadline(guard.handle());
    }

    /// Stable hash for use as a map key (dispatch tables).
    [[nodiscard]] std::size_t hash() const noexcept { return inner_.hash(); }

    /// Cold-path diagnostic (mandate 9).
    [[nodiscard]] std::string to_string() const { return "WaitSetAttachmentId()"; }

private:
    friend class WaitSet;

    explicit WaitSetAttachmentId(iox2::WaitSetAttachmentId<iox2::ServiceType::Ipc> inner) noexcept
        : inner_(std::move(inner)) {}

    iox2::WaitSetAttachmentId<iox2::ServiceType::Ipc> inner_;
};

/// Free helper (mandate 9): mirrors the member.
[[nodiscard]] inline std::string to_string(const WaitSetAttachmentId& id) {
    return id.to_string();
}

} // namespace nexigen::com

/// The attachment id formats as its diagnostic string (mandate 11).
template <> struct std::formatter<nexigen::com::WaitSetAttachmentId> : std::formatter<std::string_view> {
    template <class Ctx>
    auto format(const nexigen::com::WaitSetAttachmentId& id, Ctx& ctx) const {
        const std::string text = id.to_string();
        return std::formatter<std::string_view>::format(text, ctx);
    }
};
