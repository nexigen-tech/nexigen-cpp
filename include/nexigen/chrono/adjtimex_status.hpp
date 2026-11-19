// nexigen::chrono — kernel clock-discipline status in `chrono` vocabulary.
//
// `adjtimex(2)` reports how the kernel is steering `CLOCK_REALTIME`: the current
// offset, the frequency correction, and the PLL/FLL status bits. The daemon uses
// this as observed PTP/TAI state (design D-E); it never tries to detect an
// external process.
#pragma once

#include <cstdint>
#include <format>
#include <string>
#include <string_view>

#include <nlohmann/json.hpp>

#include <nexigen/core/result.hpp>

namespace nexigen::chrono {

/// @brief Observed kernel clock discipline.
///
/// All durations are nanoseconds and the frequency is parts per billion.
struct AdjtimexStatus {
    std::uint32_t kernel_status{0};     ///< Raw `timex.status` bits.
    std::int64_t offset_ns{0};          ///< Current `timex.offset`.
    std::int64_t freq_ppb{0};           ///< Current frequency correction.
    std::int64_t max_error_ns{0};       ///< `timex.maxerror`.
    std::int64_t estimated_error_ns{0}; ///< `timex.esterror`.
    bool pll_engaged{false};            ///< `STA_PLL` observed.
    bool unsynchronized{false};         ///< `STA_UNSYNC` observed.

    /// @brief Whether the kernel reports a locked PLL.
    ///
    /// @return `true` when `STA_PLL` is set and `STA_UNSYNC` is clear.
    [[nodiscard]] bool locked() const noexcept { return pll_engaged && !unsynchronized; }

    /// @brief Render the status as a diagnostic string.
    [[nodiscard]] std::string to_string() const;
};

/// @brief Free-helper mirror of `AdjtimexStatus::to_string`.
[[nodiscard]] std::string to_string(const AdjtimexStatus& status);

/// @brief Serialize the status as JSON (cold path only).
void to_json(nlohmann::json& j, const AdjtimexStatus& status);

/// @brief Parse the status from JSON, non-throwing.
void from_json(const nlohmann::json& j, AdjtimexStatus& status) noexcept;

/// @brief Read the current kernel clock discipline.
///
/// @return The status, or `SystemError` when `adjtimex` fails and
///         `UnsupportedFormat` on a platform without the call.
/// @note Cold path safe: one syscall, no allocation.
[[nodiscard]] core::Result<AdjtimexStatus> read_adjtimex() noexcept;

} // namespace nexigen::chrono

/// @brief `std::formatter` for `AdjtimexStatus`.
template <>
struct std::formatter<nexigen::chrono::AdjtimexStatus> : std::formatter<std::string_view> {
    /// @brief Format the status through `to_string()`.
    template <class Ctx> auto format(const nexigen::chrono::AdjtimexStatus& value, Ctx& ctx) const {
        const std::string text = value.to_string();
        return std::formatter<std::string_view>::format(text, ctx);
    }
};
