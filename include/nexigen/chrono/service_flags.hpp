// nexigen::chrono — time-service availability and health flags (change
// add-chrono-service-contract; spec chrono/time-sync).
//
// Flags describe observed state, not configured process presence. A missing
// external service is visible as `ptp_stack_ready=false` while its `*_required`
// bit remains set.
#pragma once

#include <array>
#include <cstdint>
#include <format>
#include <string>
#include <string_view>

#include <nlohmann/json.hpp>

#include <nexigen/core/enum_traits.hpp>
#include <nexigen/core/serialization_mandate.hpp>

namespace nexigen::chrono {

/// @brief One time-service flag.
enum class ServiceFlag : std::uint32_t {
    PhcPresent = 1U << 0U,
    TaiAvailable = 1U << 1U,
    HostCounterAvailable = 1U << 2U,
    HostCounterSynchronized = 1U << 3U,
    Ptp4lRequired = 1U << 4U,
    Phc2sysRequired = 1U << 5U,
    PtpStackReady = 1U << 6U,
    TaiSynchronized = 1U << 7U,
    StepDetected = 1U << 8U,
    SlewActive = 1U << 9U,
    RtSchedApplied = 1U << 10U,
    MlockApplied = 1U << 11U,
    DeclaredDomainsComplete = 1U << 12U,
};

/// @brief A set of `ServiceFlag` bits.
///
/// Trivially copyable and 4 bytes, so it lives directly in the shared block.
struct ServiceFlags {
    std::uint32_t bits{0}; ///< Active flags.

    /// @brief Whether one flag is set.
    [[nodiscard]] constexpr bool has(ServiceFlag flag) const noexcept {
        return (bits & static_cast<std::uint32_t>(flag)) != 0U;
    }

    /// @brief Set one flag.
    constexpr void set(ServiceFlag flag) noexcept { bits |= static_cast<std::uint32_t>(flag); }

    /// @brief Clear one flag.
    constexpr void clear(ServiceFlag flag) noexcept { bits &= ~static_cast<std::uint32_t>(flag); }

    /// @brief Whether two flag sets are equal.
    [[nodiscard]] constexpr bool operator==(const ServiceFlags&) const noexcept = default;
};

/// @brief Combine two single flags.
[[nodiscard]] constexpr ServiceFlags operator|(ServiceFlag lhs, ServiceFlag rhs) noexcept {
    return ServiceFlags{static_cast<std::uint32_t>(lhs) | static_cast<std::uint32_t>(rhs)};
}

/// @brief Add one flag to a set.
[[nodiscard]] constexpr ServiceFlags operator|(ServiceFlags lhs, ServiceFlag rhs) noexcept {
    return ServiceFlags{lhs.bits | static_cast<std::uint32_t>(rhs)};
}

} // namespace nexigen::chrono

namespace nexigen::core {

/// @brief Vocabulary table for `ServiceFlag`.
template <> struct EnumTraits<nexigen::chrono::ServiceFlag> {
    /// @brief One flag-to-name row.
    struct Entry {
        nexigen::chrono::ServiceFlag value; ///< Enumerator.
        std::string_view name;              ///< Canonical spelling.
    };
    /// @brief Append-only rows.
    static constexpr std::array<Entry, 13> entries{{
        {nexigen::chrono::ServiceFlag::PhcPresent, "phc_present"},
        {nexigen::chrono::ServiceFlag::TaiAvailable, "tai_available"},
        {nexigen::chrono::ServiceFlag::HostCounterAvailable, "host_counter_available"},
        {nexigen::chrono::ServiceFlag::HostCounterSynchronized, "host_counter_synchronized"},
        {nexigen::chrono::ServiceFlag::Ptp4lRequired, "ptp4l_required"},
        {nexigen::chrono::ServiceFlag::Phc2sysRequired, "phc2sys_required"},
        {nexigen::chrono::ServiceFlag::PtpStackReady, "ptp_stack_ready"},
        {nexigen::chrono::ServiceFlag::TaiSynchronized, "tai_synchronized"},
        {nexigen::chrono::ServiceFlag::StepDetected, "step_detected"},
        {nexigen::chrono::ServiceFlag::SlewActive, "slew_active"},
        {nexigen::chrono::ServiceFlag::RtSchedApplied, "rt_sched_applied"},
        {nexigen::chrono::ServiceFlag::MlockApplied, "mlock_applied"},
        {nexigen::chrono::ServiceFlag::DeclaredDomainsComplete, "declared_domains_complete"},
    }};
};

} // namespace nexigen::core

namespace nexigen::chrono {

/// @brief Canonical name of one service flag.
[[nodiscard]] constexpr std::string_view to_string(ServiceFlag value) noexcept {
    return core::enum_name(value);
}

/// @brief Parse one service-flag name.
[[nodiscard]] constexpr bool from_string(std::string_view name, ServiceFlag& out) noexcept {
    return core::enum_value(name, out);
}

/// @brief ADL JSON writer for one service flag.
inline void to_json(nlohmann::json& j, const ServiceFlag value) {
    core::mandate::enum_to_json(j, value);
}

/// @brief ADL JSON reader for one service flag, non-throwing.
inline void from_json(const nlohmann::json& j, ServiceFlag& out) noexcept {
    core::mandate::enum_from_json(j, out);
}

/// @brief Render every active flag as `name|name` text.
///
/// @param flags Flags to render.
/// @return Text naming active flags, or `"none"`.
[[nodiscard]] std::string to_string(const ServiceFlags& flags);

/// @brief Serialize a flag set as an array of active names.
void to_json(nlohmann::json& j, const ServiceFlags& flags);

/// @brief Parse a flag set from an array of names, a string, or a number.
void from_json(const nlohmann::json& j, ServiceFlags& flags) noexcept;

} // namespace nexigen::chrono

/// @brief `std::formatter` for one service flag.
template <> struct std::formatter<nexigen::chrono::ServiceFlag> : std::formatter<std::string_view> {
    /// @brief Format one flag through its canonical name.
    template <class Ctx> auto format(const nexigen::chrono::ServiceFlag value, Ctx& ctx) const {
        return std::formatter<std::string_view>::format(nexigen::chrono::to_string(value), ctx);
    }
};

/// @brief `std::formatter` for a flag set.
template <>
struct std::formatter<nexigen::chrono::ServiceFlags> : std::formatter<std::string_view> {
    /// @brief Format the set through `to_string(ServiceFlags)`.
    template <class Ctx> auto format(const nexigen::chrono::ServiceFlags& value, Ctx& ctx) const {
        const std::string text = nexigen::chrono::to_string(value);
        return std::formatter<std::string_view>::format(text, ctx);
    }
};
