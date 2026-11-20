// nexigen::chrono — one bidirectional time-domain coefficient row.
//
// A row is the whole truth about one domain for one validity interval: its
// class, epoch, name, forward and reverse anchored maps, uncertainty, lock
// state, and validity in native and hub units. The row is 128 bytes so it can
// live in the shared-memory block, history, and recorder payload without
// conversion.
//
// HOT PATH: forward/reverse conversion is pure integer arithmetic through
// `detail::int128`; no clock read, lock, or allocation.
#pragma once

#include <cstddef>
#include <cstdint>
#include <limits>
#include <string>
#include <string_view>

#include <nlohmann/json.hpp>

#include <nexigen/chrono/detail/domain_map.hpp>
#include <nexigen/chrono/lock_state.hpp>
#include <nexigen/chrono/time_domain_class.hpp>

namespace nexigen::chrono {

/// @brief Affine maps and validity interval of one time domain.
///
/// `native_anchor` and `canonical_anchor_ns` are one measured pair. The forward
/// map converts native units to hub nanoseconds; the reverse map converts hub
/// nanoseconds back to native units. Both scales are Q32.
///
/// "canonical" in these field names is the design's wire term for hub units
/// (`CLOCK_MONOTONIC_RAW`); it is not a domain or class name.
///
/// @note Spec `chrono/time-base`, requirement "Domain coefficient row".
struct alignas(64) DomainCoefficient {
    static constexpr std::uint16_t has_reverse_bit = 1U << 0U; ///< Reverse map is valid.
    static constexpr std::uint16_t latched_bit = 1U << 1U;     ///< Pair came from a latch.
    static constexpr std::uint16_t ptm_capable_bit = 1U << 2U; ///< Device supports PCIe PTM.
    static constexpr std::uint16_t manual_pair_bit = 1U << 3U; ///< Software-read pair.

    std::uint16_t domain_id{0};                                 ///< Stable domain identity.
    TimeDomainClass domain_class{TimeDomainClass::DeviceClock}; ///< Domain role.
    LockState lock{LockState::Unsupported};                     ///< Measurement state.
    std::uint16_t flags{0};                                     ///< Domain flags.
    std::uint16_t reserved0{0};                                 ///< Reserved; zero in this version.
    std::uint64_t epoch{0};                                     ///< Domain lifecycle epoch.
    std::uint64_t native_anchor{0};           ///< Native value of the anchor pair.
    std::int64_t canonical_anchor_ns{0};      ///< Hub instant of the anchor pair.
    std::uint64_t scale_fwd_q32{1ULL << 32U}; ///< Hub ns per native unit, Q32.
    std::uint64_t scale_rev_q32{1ULL << 32U}; ///< Native units per hub ns, Q32.
    std::uint64_t unc_ns{0};                  ///< Map uncertainty in nanoseconds.
    std::uint64_t valid_from_native{0};       ///< First native value covered.
    std::uint64_t valid_to_native{
        std::numeric_limits<std::uint64_t>::max()}; ///< Last native value covered.
    std::uint64_t valid_from_canonical{0};          ///< First hub instant covered.
    std::uint64_t valid_to_canonical{std::numeric_limits<std::uint64_t>::max()}; ///< Last instant.
    char name[16]{};             ///< Stable domain name (NUL-terminated).
    std::uint8_t reserved[24]{}; ///< Reserved; zero in this version.

    /// @brief Whether the reverse map is valid.
    /// @return `true` when `scale_rev_q32` may be applied.
    /// @note Spec `chrono/time-base`, requirement "Domain coefficient row".
    [[nodiscard]] constexpr bool has_reverse() const noexcept {
        return (flags & has_reverse_bit) != 0U;
    }

    /// @brief Whether the pair came from a hardware latch.
    /// @return `true` for a latched register pair or cross-timestamp.
    /// @note Spec `chrono/time-base`, requirement "Hardware domain access modes".
    [[nodiscard]] constexpr bool latched() const noexcept { return (flags & latched_bit) != 0U; }

    /// @brief Whether the device supports PCIe PTM.
    /// @return `true` when the firmware advertised the PTM capability.
    /// @note Spec `chrono/time-base`, requirement "Hardware domain access modes".
    [[nodiscard]] constexpr bool ptm_capable() const noexcept {
        return (flags & ptm_capable_bit) != 0U;
    }

    /// @brief Whether the pair is a software-read manual pair.
    /// @return `true` when the counter and hub clock were read in software.
    /// @note Spec `chrono/time-base`, requirement "Hardware domain access modes".
    [[nodiscard]] constexpr bool manual_pair() const noexcept {
        return (flags & manual_pair_bit) != 0U;
    }

    /// @brief The stored domain name as a view.
    /// @return Text up to the NUL terminator of `name`.
    /// @note Spec `chrono/time-base`, requirement "Domain coefficient row".
    [[nodiscard]] std::string_view name_view() const noexcept;

    /// @brief Render the row as a diagnostic string.
    /// @return Text naming the id, class, epoch, maps, validity and flags.
    /// @note Spec `chrono/time-base`, requirement "Time-base serialization";
    ///       cold path only (allocates).
    [[nodiscard]] std::string to_string() const;
};

/// @brief Free-helper mirror of `DomainCoefficient::to_string`.
[[nodiscard]] std::string to_string(const DomainCoefficient& coefficient);

/// @brief Forward map: native value to hub nanoseconds.
///
/// @param coefficient Row.
/// @param native Value inside the row's native validity.
/// @return Hub nanoseconds.
/// @note Hot path safe.
[[nodiscard]] inline std::int64_t canonical_from_native(const DomainCoefficient& coefficient,
                                                        std::uint64_t native) noexcept {
    return detail::map_forward(native, coefficient.native_anchor, coefficient.canonical_anchor_ns,
                               coefficient.scale_fwd_q32);
}

/// @brief Reverse map: hub nanoseconds to the native domain.
///
/// @param coefficient Row.
/// @param canonical Hub value inside the row's canonical validity.
/// @return Native value; zero or saturated when the row has no reverse map.
/// @note Hot path safe.
[[nodiscard]] inline std::uint64_t native_from_canonical(const DomainCoefficient& coefficient,
                                                         std::int64_t canonical) noexcept {
    if (!coefficient.has_reverse()) {
        return 0U;
    }
    return detail::map_reverse(canonical, coefficient.canonical_anchor_ns,
                               coefficient.native_anchor, coefficient.scale_rev_q32);
}

/// @brief Whether a native value is inside the row's native validity.
[[nodiscard]] inline bool contains_native(const DomainCoefficient& coefficient,
                                          std::uint64_t native) noexcept {
    return native >= coefficient.valid_from_native && native <= coefficient.valid_to_native;
}

/// @brief Whether a hub instant is inside the row's canonical validity.
[[nodiscard]] inline bool contains_canonical(const DomainCoefficient& coefficient,
                                             std::uint64_t canonical_ns) noexcept {
    return canonical_ns >= coefficient.valid_from_canonical &&
           canonical_ns <= coefficient.valid_to_canonical;
}

/// @brief Serialize a row as JSON (cold path only).
void to_json(nlohmann::json& j, const DomainCoefficient& coefficient);

/// @brief Parse a row from JSON, non-throwing.
void from_json(const nlohmann::json& j, DomainCoefficient& coefficient) noexcept;

static_assert(sizeof(DomainCoefficient) == 128U, "a coefficient row is two cache lines");
static_assert(alignof(DomainCoefficient) == 64U, "a coefficient row starts on a cache line");
static_assert(offsetof(DomainCoefficient, name) == 88U, "row name offset is a wire constant");

} // namespace nexigen::chrono
