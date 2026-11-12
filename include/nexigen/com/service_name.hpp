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

/// @brief Value type holding one service identity.
///
/// Thin wrapper over `iox2::ServiceName` so business code never names the
/// transport type. Two copies compare equal by name, and every port requires a
/// valid `ServiceName` for creation. Validation (non-empty, no invalid
/// content, length limit) is performed by the transport and surfaced through
/// `com::Error::InvalidArgument`.
///
/// The value owns only the name, so copies are independent and cheap.
///
/// @note Spec `com/service-management`, requirement "Service naming".
class ServiceName {
public:
    /// @brief Create a service name from a string.
    ///
    /// @param name NUL-terminated candidate name; must be non-empty and
    ///        syntactically valid for the transport.
    /// @return The constructed name, or `com::Error::InvalidArgument` when the
    ///         transport rejects it, or another mapped `com::Error`.
    /// @note Spec `com/service-management`, requirement "Service naming";
    ///       port creation requires the returned value.
    [[nodiscard]] static Result<ServiceName> create(const char* name) noexcept {
        auto created = iox2::ServiceName::create(name);
        if (!created) {
            return std::unexpected(detail::map_error(created.error()));
        }
        return ServiceName{std::move(*created)};
    }

    /// @brief Copy-construct a name; the copy compares equal to the original.
    ServiceName(const ServiceName&) = default;

    /// @brief Copy-assign a name.
    /// @return `*this`.
    ServiceName& operator=(const ServiceName&) = default;

    /// @brief Move-construct a name, transferring the transport value.
    ///
    /// The moved-from object is left in an unspecified state.
    ServiceName(ServiceName&&) noexcept = default;

    /// @brief Move-assign a name, transferring the transport value.
    ///
    /// The moved-from object is left in an unspecified state.
    /// @return `*this`.
    ServiceName& operator=(ServiceName&&) noexcept = default;

    /// @brief Render the cold-path diagnostic string.
    ///
    /// @return The bare service name, matching the free helper and formatter.
    /// @note Mandate 9 (universal `to_string`); allocates, so cold path only.
    [[nodiscard]] std::string to_string() const {
        return std::string(inner_.to_string().unchecked_access().c_str());
    }

    /// @brief Compare two service names by their textual identity.
    ///
    /// @param other Name to compare against.
    /// @return `true` when both render the same name.
    /// @note Spec `com/service-management`, requirement "Service naming":
    ///       copies compare equal to the original. The comparison allocates,
    ///       so it is a cold-path operation.
    [[nodiscard]] bool operator==(const ServiceName& other) const noexcept {
        return to_string() == other.to_string();
    }

    /// @brief The underlying transport handle.
    ///
    /// @return Const reference to the wrapped `iox2::ServiceName`.
    /// @note Module-internal surface only: port creation passes it to the
    ///       service builder; business code must stay transport-agnostic
    ///       (spec `com/service-management`, requirement
    ///       "Protocol-independent boundary").
    [[nodiscard]] const iox2::ServiceName& handle() const noexcept { return inner_; }

private:
    /// @brief Adopt an already-validated transport name.
    ///
    /// @param inner Transport name returned by the iceoryx2 builder.
    /// @note Private by design: the only supported construction path is
    ///       `create()`, which performs validation.
    explicit ServiceName(iox2::ServiceName inner) noexcept : inner_(std::move(inner)) {}

    iox2::ServiceName inner_; ///< Owned transport name.
};

/// @brief Free helper mirroring `ServiceName::to_string()`.
///
/// @param name Name to render.
/// @return The service name text.
/// @note Mandate 9 (universal `to_string`); allocates, so cold path only.
[[nodiscard]] inline std::string to_string(const ServiceName& name) {
    return name.to_string();
}

} // namespace nexigen::com

/// @brief Formats a `ServiceName` as its diagnostic string.
///
/// @note Mandate 11 (universal `std::formatter`); delegates to
///       `ServiceName::to_string()`, which allocates, so cold path only.
template <> struct std::formatter<nexigen::com::ServiceName> : std::formatter<std::string_view> {
    /// @brief Write the service name's text into the format context.
    ///
    /// @tparam Ctx `std::format` context type.
    /// @param name Service name to format.
    /// @param ctx Output context supplied by `std::format`.
    /// @return Iterator one past the last written character.
    template <class Ctx> auto format(const nexigen::com::ServiceName& name, Ctx& ctx) const {
        const std::string text = name.to_string();
        return std::formatter<std::string_view>::format(text, ctx);
    }
};
