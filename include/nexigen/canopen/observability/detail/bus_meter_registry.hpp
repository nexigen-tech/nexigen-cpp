// nexigen::canopen::observability::detail — bus name reservation (change
// add-canopen-observability; design D2).
#pragma once

#include <array>
#include <cstddef>
#include <string>
#include <string_view>

#include <nexigen/canopen/observability/detail/bus_meter_identity.hpp>
#include <nexigen/core/result.hpp>

namespace nexigen::canopen::observability::detail {

/// @brief Fixed-capacity registry of reserved bus names.
///
/// A runtime reserves its bus name before creating the meter and opening the
/// transport. `MetricProvider::get_meter(name, version)` is idempotent for the
/// same name/version, so two runtimes sharing a bus name would share metric
/// names and either fail registration or double-count; the registry rejects the
/// second reservation instead.
///
/// The registry is cold path only (construction and startup), so a small
/// fixed-capacity name table with `std::string` entries is sufficient.
///
/// @note Spec `canopen/observability`, requirement "One runtime owns one logger
///       and one meter per bus"; design D2.
class BusMeterRegistry {
public:
    /// @brief Maximum accepted bus-name length in characters.
    ///
    /// The composed meter name `canopen.<bus>` stays below
    /// `metrics::max_name_length` (63) while the bus itself fits a diagnostic
    /// label.
    static constexpr std::size_t max_bus_name_length = 32U;

    /// @brief Maximum simultaneously reserved bus names.
    static constexpr std::size_t capacity = 8U;

    /// @brief Reserve one bus name and compose its meter identity.
    ///
    /// @param bus Bus name without the `canopen.` prefix; non-empty and at most
    ///        `max_bus_name_length` characters.
    /// @param version Meter version from the runtime configuration; non-empty
    ///        and at most `metrics::max_name_length` characters.
    /// @return The composed `canopen.<bus>` identity on success. The error arm
    ///         is `InvalidArgument` for an empty, over-long, or duplicate bus
    ///         name or version, and `ResourceExhausted` when all `capacity`
    ///         slots are taken.
    /// @note Cold path only; allocates the returned strings.
    [[nodiscard]] core::Result<BusMeterIdentity> reserve(std::string_view bus,
                                                         std::string_view version);

    /// @brief Release one previously reserved bus name.
    ///
    /// @param bus Bus name passed to `reserve()`.
    /// @return `true` when a matching reservation was removed; `false` when the
    ///         name was not reserved.
    /// @note Cold path only.
    bool release(std::string_view bus) noexcept;

    /// @brief Number of currently reserved bus names.
    ///
    /// @return Occupied slots in `[0, capacity]`.
    [[nodiscard]] std::size_t reserved_count() const noexcept;

    /// @brief Cold-path diagnostic string listing reserved meter names.
    ///
    /// @return `BusMeterRegistry(count=N, buses=[...])`; allocates.
    /// @note Mandate 9.
    [[nodiscard]] std::string to_string() const;

private:
    /// @brief Reserved bus names; empty entries are free slots.
    std::array<std::string, capacity> reserved_{};
};

} // namespace nexigen::canopen::observability::detail
