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
///
/// Fixed so `CheckpointClaim` stays trivially copyable and travels through
/// shared memory without serialization. A gated manifest entry must therefore
/// have a name of at most `k_claim_name_length - 1` bytes; the manifest
/// validator enforces that bound.
///
/// @note Spec `phm/identity`, requirement "Checkpoint claim channel"; spec
///       `phm/manifest`, scenario "A name that cannot fit a claim is rejected".
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
/// @note Spec `phm/identity`, requirement "Checkpoint claim channel".
[[nodiscard]] inline std::string_view bounded_name(const char* buffer,
                                                   std::size_t length) noexcept {
    return std::string_view(buffer, ::strnlen(buffer, length));
}

/// @brief One process identity claim: name, pid, and uid.
///
/// Published by a supervised process on the claim channel and drained by the
/// PHM daemon, which matches `name` to a manifest entry and verifies that the
/// claimed uid and the real uid of `/proc/<pid>` equal the entry's
/// `expected_uid`. The struct is a `com::concepts::Message`: fixed-size,
/// trivially copyable, no serialization step.
///
/// The fields describe what the process *claims*, not what the kernel attests;
/// a same-UID impostor can still claim another pid, which is the documented
/// limitation of `RS_PHM_00118`.
///
/// @note Spec `phm/identity`, requirement "Checkpoint claim channel".
struct CheckpointClaim {
    char
        name[k_claim_name_length]{}; ///< NUL-terminated process name; may be full and unterminated.
    std::uint32_t pid{0};            ///< Claimed process id.
    std::uint32_t uid{0};            ///< Claimed real user id.

    /// @brief Cold-path diagnostic rendering.
    ///
    /// @return Text of the form
    ///         `CheckpointClaim(name=<bounded name>, pid=<pid>, uid=<uid>)`.
    /// @note Mandate 9 (universal `to_string`); allocates, so cold path only.
    ///       The name is read through `bounded_name`, so a full buffer cannot
    ///       overrun the sample.
    [[nodiscard]] std::string to_string() const {
        return std::format("CheckpointClaim(name={}, pid={}, uid={})",
                           bounded_name(name, sizeof(name)), pid, uid);
    }
};

static_assert(com::concepts::Message<CheckpointClaim>);

/// @brief Free helper mirroring `CheckpointClaim::to_string()` (mandate 9).
///
/// @param claim Claim to render.
/// @return The member diagnostic text.
/// @note Mandate 9; allocates, so cold path only. Spec `phm/identity`,
///       requirement "Checkpoint claim channel".
[[nodiscard]] inline std::string to_string(const CheckpointClaim& claim) {
    return claim.to_string();
}

} // namespace nexigen::phm

/// @brief `std::formatter` for a checkpoint claim (project mandate 11).
///
/// Makes `std::format("{}", claim)` equal `to_string(claim)`.
///
/// @note Mandate 11 (universal `std::formatter`); spec `phm/identity`,
///       requirement "Checkpoint claim channel".
template <>
struct std::formatter<nexigen::phm::CheckpointClaim> : std::formatter<std::string_view> {
    /// @brief Write the claim diagnostic into the format context.
    ///
    /// @tparam Ctx `std::format` context type, for example
    ///         `std::format_context`.
    /// @param claim Claim to format.
    /// @param ctx Output context supplied by `std::format`.
    /// @return Iterator one past the last written character.
    /// @note Mandate 11; spec `phm/identity`, requirement "Checkpoint claim
    ///       channel".
    template <class Ctx> auto format(const nexigen::phm::CheckpointClaim& claim, Ctx& ctx) const {
        return std::formatter<std::string_view>::format(claim.to_string(), ctx);
    }
};
