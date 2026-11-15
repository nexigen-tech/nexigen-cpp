// nexigen::phm — checkpoint identity claim (change checkpoint-identity).
//
// A process publishes this sample on Nexigen/PHM/Claim and wakes the paired
// event; the daemon matches it to the manifest entry and checks the pid's owner.
#pragma once

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <format>
#include <string>
#include <string_view>

#include <nexigen/com/concepts/message_concept.hpp>

namespace nexigen::phm {

/// @brief Length of the process-name buffer in a claim.
inline constexpr std::size_t k_claim_name_length = 32;

/// @brief View of a fixed-size name buffer that stops at the first NUL.
///
/// Samples travel through shared memory, so a buggy or hostile producer can
/// fill the buffer without a terminator. Every diagnostic path must bound the
/// read instead of assuming NUL termination.
///
/// @param buffer Name buffer.
/// @param length Buffer capacity in bytes.
/// @return View of at most `length` bytes, ending at the first NUL.
[[nodiscard]] inline std::string_view bounded_name(const char* buffer,
                                                   std::size_t length) noexcept {
    return std::string_view(buffer, ::strnlen(buffer, length));
}

/// @brief One process identity claim: name, pid, and uid.
///
/// @note Spec `phm/identity`, requirement "Checkpoint claim channel".
struct CheckpointClaim {
    char name[k_claim_name_length]{}; ///< NUL-terminated process name.
    std::uint32_t pid{0};             ///< Claimed process id.
    std::uint32_t uid{0};             ///< Claimed real user id.

    /// @brief Cold-path diagnostic rendering.
    [[nodiscard]] std::string to_string() const {
        return std::format("CheckpointClaim(name={}, pid={}, uid={})",
                           bounded_name(name, sizeof(name)), pid, uid);
    }
};

static_assert(com::concepts::Message<CheckpointClaim>);

/// @brief Free helper mirroring `CheckpointClaim::to_string()` (mandate 9).
[[nodiscard]] inline std::string to_string(const CheckpointClaim& claim) {
    return claim.to_string();
}

} // namespace nexigen::phm

/// @brief `std::formatter` for a checkpoint claim (project mandate 11).
template <>
struct std::formatter<nexigen::phm::CheckpointClaim> : std::formatter<std::string_view> {
    template <class Ctx> auto format(const nexigen::phm::CheckpointClaim& claim, Ctx& ctx) const {
        return std::formatter<std::string_view>::format(claim.to_string(), ctx);
    }
};
