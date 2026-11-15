// nexigen::phm — claim verification through /proc (change checkpoint-identity).
#pragma once

#include <cstdint>
#include <format>
#include <string>
#include <string_view>

#include <nexigen/phm/checkpoint_claim.hpp>

namespace nexigen::phm {

/// @brief Outcome of verifying one claim.
struct ClaimVerdict {
    bool verified{false}; ///< True only when every check passed.
    std::string reason{}; ///< Cold-path rejection reason; empty on success.

    /// @brief Cold-path diagnostic rendering.
    [[nodiscard]] std::string to_string() const {
        return std::format("ClaimVerdict(verified={}, reason={})", verified, reason);
    }
};

/// @brief Verify a claim against an expected UID and the process owner.
///
/// Reads `/proc/<pid>/status` and requires the claimed uid and the process's
/// real uid to equal `expected_uid`. A same-uid impostor can still pass this
/// check; kernel-attested peer credentials are out of scope.
///
/// @param claim Claim to verify.
/// @param expected_uid Manifest expectation.
/// @return The verdict and, on rejection, a reason.
/// @note Spec `phm/identity`, requirement "Claim verification through /proc".
[[nodiscard]] ClaimVerdict checkpoint_claim_verdict(const CheckpointClaim& claim,
                                                    std::uint32_t expected_uid) noexcept;

} // namespace nexigen::phm

/// @brief `std::formatter` for a claim verdict (project mandate 11).
template <> struct std::formatter<nexigen::phm::ClaimVerdict> : std::formatter<std::string_view> {
    template <class Ctx> auto format(const nexigen::phm::ClaimVerdict& verdict, Ctx& ctx) const {
        return std::formatter<std::string_view>::format(verdict.to_string(), ctx);
    }
};
