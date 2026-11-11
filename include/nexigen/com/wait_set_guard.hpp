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

class WaitSetAttachmentId;

/// RAII attachment guard: detaches its source when dropped.
class WaitSetGuard {
public:
    WaitSetGuard(const WaitSetGuard&) = delete;
    WaitSetGuard& operator=(const WaitSetGuard&) = delete;
    WaitSetGuard(WaitSetGuard&&) noexcept = default;
    WaitSetGuard& operator=(WaitSetGuard&&) noexcept = default;

    /// Cold-path diagnostic (mandate 9).
    [[nodiscard]] std::string to_string() const { return "WaitSetGuard()"; }

    /// The underlying transport handle (module-internal surface only).
    [[nodiscard]] const iox2::WaitSetGuard<iox2::ServiceType::Ipc>& handle() const noexcept {
        return inner_;
    }

private:
    friend class WaitSet;
    friend class WaitSetAttachmentId;

    explicit WaitSetGuard(iox2::WaitSetGuard<iox2::ServiceType::Ipc> inner) noexcept
        : inner_(std::move(inner)) {}

    iox2::WaitSetGuard<iox2::ServiceType::Ipc> inner_;
};

/// Free helper (mandate 9): mirrors the member.
[[nodiscard]] inline std::string to_string(const WaitSetGuard& guard) {
    return guard.to_string();
}

} // namespace nexigen::com

/// The guard formats as its diagnostic string (mandate 11).
template <> struct std::formatter<nexigen::com::WaitSetGuard> : std::formatter<std::string_view> {
    template <class Ctx> auto format(const nexigen::com::WaitSetGuard& guard, Ctx& ctx) const {
        const std::string text = guard.to_string();
        return std::formatter<std::string_view>::format(text, ctx);
    }
};
