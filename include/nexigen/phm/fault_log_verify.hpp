// nexigen::phm — fault-log verification (change add-integrity-primitives).
//
// After-the-fact scan of the append-only fault log. Every line is classified as
// intact, corrupted, truncated, or legacy (pre-change four-field records).
#pragma once

#include <cstddef>
#include <format>
#include <string>
#include <string_view>

#include <nexigen/core/result.hpp>

namespace nexigen::phm {

/// @brief Line classification produced by `verify_fault_log`.
struct FaultLogReport {
    std::size_t intact{0};    ///< Records with a matching checksum and increasing sequence.
    std::size_t corrupted{0}; ///< Records with a checksum, sequence, or field error.
    std::size_t truncated{0}; ///< Lines that end before the checksum field.
    std::size_t legacy{0};    ///< Pre-change four-field records, accepted unverified.

    /// @brief Cold-path diagnostic rendering.
    ///
    /// @return Text naming every counter.
    /// @note Mandate 9; allocates, so cold path only.
    [[nodiscard]] std::string to_string() const {
        return std::format("FaultLogReport(intact={}, corrupted={}, truncated={}, legacy={})",
                           intact, corrupted, truncated, legacy);
    }
};

/// @brief Free helper mirroring `FaultLogReport::to_string()` (mandate 9).
[[nodiscard]] inline std::string to_string(const FaultLogReport& report) {
    return report.to_string();
}

/// @brief Scan a fault log and classify every line.
///
/// The writer appends `<kind> <process> <checkpoint> <timestamp> <seq> <crc>`,
/// where the CRC-32C covers the exact bytes before the checksum field,
/// including the separating space. Four-field lines are legacy records and are
/// counted but not verified. Malformed input never aborts the scan.
///
/// @param path Log path.
/// @param out Receives the counters.
/// @return Success, `InvalidArgument` when the file is absent, or
///         `SystemError` when it cannot be read.
/// @note Spec `phm/recovery`, requirement "Fault log record integrity".
[[nodiscard]] core::Status verify_fault_log(std::string_view path, FaultLogReport& out) noexcept;

} // namespace nexigen::phm

/// @brief `std::formatter` for the fault-log report (project mandate 11).
template <> struct std::formatter<nexigen::phm::FaultLogReport> : std::formatter<std::string_view> {
    template <class Ctx> auto format(const nexigen::phm::FaultLogReport& report, Ctx& ctx) const {
        return std::formatter<std::string_view>::format(report.to_string(), ctx);
    }
};
