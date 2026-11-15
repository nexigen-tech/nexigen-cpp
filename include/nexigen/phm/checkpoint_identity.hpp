// nexigen::phm — claim verification through /proc (change checkpoint-identity).
#pragma once

#include <cstdint>
#include <format>
#include <optional>
#include <string>
#include <string_view>

#include <nexigen/phm/checkpoint_claim.hpp>

namespace nexigen::phm {

/// @brief Outcome of verifying one claim.
///
/// `verified` is the only value the gate consults. `reason` is a cold-path
/// diagnostic kept for logs and tests: it is empty on success and names the
/// first failed check otherwise. The struct is not a transport message; it
/// exists on the daemon's claim-drain path.
///
/// @note Spec `phm/identity`, requirement "Claim verification through /proc".
struct ClaimVerdict {
    bool verified{false}; ///< True only when every check passed.
    std::string reason{}; ///< Cold-path rejection reason; empty on success.

    /// @brief Cold-path diagnostic rendering.
    ///
    /// @return Text of the form `ClaimVerdict(verified=<bool>, reason=<reason>)`.
    /// @note Mandate 9 (universal `to_string`); allocates, so cold path only.
    [[nodiscard]] std::string to_string() const {
        return std::format("ClaimVerdict(verified={}, reason={})", verified, reason);
    }
};

/// @brief Parse the real uid from `/proc/<pid>/status` text.
///
/// Extracts the first field of the first `Uid:` line (the real uid). The parser
/// is separated from the file read so malformed and hostile status text can be
/// tested without a live process.
///
/// @param status_text Contents of a `/proc/<pid>/status` file.
/// @return The real uid, or `std::nullopt` when the line is missing, has no
///         digits, or does not fit `std::uint32_t`.
/// @note Spec `phm/identity`, requirement "Claim verification through /proc".
[[nodiscard]] std::optional<std::uint32_t>
parse_status_real_uid(std::string_view status_text) noexcept;

/// @brief Verify a claim against an expected UID and the process owner.
///
/// Reads `/proc/<pid>/status` and requires the claimed uid and the process's
/// real uid to equal `expected_uid`. A same-uid impostor can still pass this
/// check; kernel-attested peer credentials are out of scope.
///
/// The checks run in cost order: empty name, zero pid, claimed uid, then the
/// single `/proc` read. A pid that cannot be read is rejected, so a vanished
/// process never verifies.
///
/// @param claim Claim to verify.
/// @param expected_uid Manifest expectation; nonzero for a gated entry.
/// @return The verdict and, on rejection, a reason.
/// @note Spec `phm/identity`, requirement "Claim verification through /proc",
///       scenarios "A matching UID is accepted", "A foreign UID is rejected",
///       and "A missing process is rejected".
[[nodiscard]] ClaimVerdict checkpoint_claim_verdict(const CheckpointClaim& claim,
                                                    std::uint32_t expected_uid) noexcept;

} // namespace nexigen::phm

/// @brief `std::formatter` for a claim verdict (project mandate 11).
///
/// Makes `std::format("{}", verdict)` equal `to_string(verdict)`.
///
/// @note Mandate 11 (universal `std::formatter`); spec `phm/identity`,
///       requirement "Claim verification through /proc".
template <> struct std::formatter<nexigen::phm::ClaimVerdict> : std::formatter<std::string_view> {
    /// @brief Write the verdict diagnostic into the format context.
    ///
    /// @tparam Ctx `std::format` context type, for example
    ///         `std::format_context`.
    /// @param verdict Verdict to format.
    /// @param ctx Output context supplied by `std::format`.
    /// @return Iterator one past the last written character.
    /// @note Mandate 11; spec `phm/identity`, requirement "Claim verification
    ///       through /proc".
    template <class Ctx> auto format(const nexigen::phm::ClaimVerdict& verdict, Ctx& ctx) const {
        return std::formatter<std::string_view>::format(verdict.to_string(), ctx);
    }
};
