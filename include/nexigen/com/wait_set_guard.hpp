// nexigen::com — RAII attachment handle for WaitSet (change add-com-waitset;
// spec com/waitset "Attachment lifecycle").
//
// The handle returned by WaitSet::attach_*: it keeps one source attached for
// as long as it lives and detaches it on destruction. Move-only, mirroring the
// underlying iceoryx2 guard.
#pragma once

#include <format>
#include <string>
#include <utility>

#include <iox2/iceoryx2.hpp>

namespace nexigen::com {

/// @brief Forward declaration of the id a WaitSet callback receives.
class WaitSetAttachmentId;

/// @brief RAII attachment guard: detaches its source when dropped.
///
/// Returned by `WaitSet::attach_notification()`, `attach_deadline()`, and
/// `attach_interval()`. The source stays attached exactly as long as the guard
/// lives, so dropping or moving it updates the wait set's attachment count and
/// never leaks a source.
///
/// The guard is move-only and mirrors the transport guard. It must not outlive
/// the `WaitSet` it came from.
///
/// @note Spec `com/waitset`, requirement "Attachment lifecycle".
class WaitSetGuard {
public:
    /// @brief Copying is disabled: one guard owns one attachment.
    WaitSetGuard(const WaitSetGuard&) = delete;

    /// @brief Copy assignment is disabled: one guard owns one attachment.
    WaitSetGuard& operator=(const WaitSetGuard&) = delete;

    /// @brief Move-construct a guard, transferring the attachment.
    ///
    /// The moved-from guard no longer detaches anything on destruction.
    WaitSetGuard(WaitSetGuard&&) noexcept = default;

    /// @brief Move-assign a guard, transferring the attachment.
    ///
    /// The destination's previous attachment is detached first.
    /// @return `*this`.
    WaitSetGuard& operator=(WaitSetGuard&&) noexcept = default;

    /// @brief Render the cold-path diagnostic string.
    ///
    /// @return The constant marker `WaitSetGuard()`; the attachment identity is
    ///         reported by `WaitSet` instead.
    /// @note Mandate 9 (universal `to_string`); `com/waitset`, requirement
    ///       "WaitSet diagnostics". Allocates, so cold path only.
    [[nodiscard]] std::string to_string() const { return "WaitSetGuard()"; }

    /// @brief The underlying transport handle.
    ///
    /// @return Const reference to the wrapped `iox2::WaitSetGuard`.
    /// @note Module-internal surface only: `WaitSetAttachmentId` compares
    ///       against it; business code must stay transport-agnostic (spec
    ///       `com/service-management`, requirement "Protocol-independent
    ///       boundary").
    [[nodiscard]] const iox2::WaitSetGuard<iox2::ServiceType::Ipc>& handle() const noexcept {
        return inner_;
    }

private:
    friend class WaitSet;
    friend class WaitSetAttachmentId;

    /// @brief Adopt a transport guard returned by an attach call.
    ///
    /// @param inner Transport guard.
    /// @note Private by design: only `WaitSet::attach_*` produces guards.
    explicit WaitSetGuard(iox2::WaitSetGuard<iox2::ServiceType::Ipc> inner) noexcept
        : inner_(std::move(inner)) {}

    iox2::WaitSetGuard<iox2::ServiceType::Ipc> inner_; ///< Owned attachment guard.
};

/// @brief Free helper mirroring `WaitSetGuard::to_string()`.
///
/// @param guard Guard to render.
/// @return The guard's diagnostic string.
/// @note Mandate 9 (universal `to_string`); allocates, so cold path only.
[[nodiscard]] inline std::string to_string(const WaitSetGuard& guard) {
    return guard.to_string();
}

} // namespace nexigen::com

/// @brief Formats a `WaitSetGuard` as its diagnostic string.
///
/// @note Mandate 11 (universal `std::formatter`); delegates to
///       `WaitSetGuard::to_string()`, which allocates, so cold path only.
template <> struct std::formatter<nexigen::com::WaitSetGuard> : std::formatter<std::string_view> {
    /// @brief Write the guard's diagnostic text into the format context.
    ///
    /// @tparam Ctx `std::format` context type.
    /// @param guard Guard to format.
    /// @param ctx Output context supplied by `std::format`.
    /// @return Iterator one past the last written character.
    template <class Ctx> auto format(const nexigen::com::WaitSetGuard& guard, Ctx& ctx) const {
        const std::string text = guard.to_string();
        return std::formatter<std::string_view>::format(text, ctx);
    }
};
