// nexigen::com — service identity (change add-com-module;
// spec com/service-management "Service naming").
//
// Thin wrapper over iox2::ServiceName: a value type carrying the service
// identity; validation (non-empty, no invalid content, length limit) comes
// from iox2 and is surfaced through com::Error::InvalidArgument.
#pragma once

#include <format>
#include <string>
#include <utility>

#include <iox2/iceoryx2.hpp>

#include <nexigen/com/detail/error_mapping.hpp>
#include <nexigen/com/error.hpp>
#include <nexigen/core/serialization_mandate.hpp>

namespace nexigen::com {

/// Value type holding one service identity. Copies compare equal by name.
class ServiceName {
public:
    /// Create a service name; a non-empty, syntactically valid name is
    /// required (iox2 validation surfaces as InvalidArgument).
    [[nodiscard]] static Result<ServiceName> create(const char* name) noexcept {
        auto created = iox2::ServiceName::create(name);
        if (!created) {
            return std::unexpected(detail::map_error(created.error()));
        }
        return ServiceName{std::move(*created)};
    }

    ServiceName(const ServiceName&) = default;
    ServiceName& operator=(const ServiceName&) = default;
    ServiceName(ServiceName&&) noexcept = default;
    ServiceName& operator=(ServiceName&&) noexcept = default;

    /// Cold-path diagnostic (mandate 9).
    [[nodiscard]] std::string to_string() const {
        return std::string(inner_.to_string().unchecked_access().c_str());
    }

    /// Name equality.
    [[nodiscard]] bool operator==(const ServiceName& other) const noexcept {
        return to_string() == other.to_string();
    }

    /// The underlying transport handle (module-internal surface only).
    [[nodiscard]] const iox2::ServiceName& handle() const noexcept { return inner_; }

private:
    explicit ServiceName(iox2::ServiceName inner) noexcept : inner_(std::move(inner)) {}

    iox2::ServiceName inner_;
};

/// Free helper (mandate 9): mirrors the member.
[[nodiscard]] inline std::string to_string(const ServiceName& name) {
    return name.to_string();
}

} // namespace nexigen::com

/// The service name formats as its diagnostic string (mandate 11).
template <> struct std::formatter<nexigen::com::ServiceName> : std::formatter<std::string_view> {
    template <class Ctx> auto format(const nexigen::com::ServiceName& name, Ctx& ctx) const {
        const std::string text = name.to_string();
        return std::formatter<std::string_view>::format(text, ctx);
    }
};
