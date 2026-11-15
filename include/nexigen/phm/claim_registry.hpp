// nexigen::phm — checkpoint identity gate (change checkpoint-identity).
//
// Transport-free bookkeeping: which supervised process has proven its identity,
// which claims are rejected, and whether checkpoints from a process may reach
// the supervisor. The daemon owns one registry per loaded manifest and feeds it
// the claims it drains; unit tests drive it directly.
#pragma once

#include <algorithm>
#include <cstddef>
#include <format>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include <nexigen/phm/checkpoint_claim.hpp>
#include <nexigen/phm/checkpoint_identity.hpp>
#include <nexigen/phm/phm_manifest.hpp>
#include <nexigen/phm/security_event_kind.hpp>

namespace nexigen::phm {

/// @brief Per-process identity gate for checkpoint supervision.
///
/// A process whose manifest entry declares `expected_uid` must present a claim
/// that verifies against `/proc`; until then its checkpoints are dropped. An
/// entry without `expected_uid` keeps the legacy path and is always accepted.
/// The verified flag is per process incarnation: the daemon resets it when the
/// EM reports the process stopping, so a restarted process must claim again.
///
/// The registry owns no transport and allocates only its flag vector at
/// construction, which keeps the daemon's claim-drain and checkpoint paths free
/// of per-sample allocation. Re-claims for an already verified process
/// short-circuit without another `/proc` read.
///
/// @note Spec `phm/identity`, requirements "Claim verification through /proc",
///       "Checkpoint gating", and "Security event channel".
class ClaimRegistry {
public:
    /// @brief Build a registry in which nothing is verified yet.
    ///
    /// @param manifest Manifest whose entries define the expectations; must
    ///        outlive the registry (a pointer is borrowed).
    /// @note Spec `phm/identity`, requirement "Checkpoint gating".
    explicit ClaimRegistry(const PhmManifest& manifest) noexcept
        : manifest_(&manifest), verified_(manifest.processes.size(), 0) {}

    /// @brief Whether checkpoints from this process may reach the supervisor.
    ///
    /// @param index Manifest index of the supervised process.
    /// @return True for a legacy entry, or for an entry with a verified claim;
    ///         false for an out-of-range index.
    /// @note Spec `phm/identity`, requirement "Checkpoint gating", scenarios
    ///       "Unverified checkpoints are ignored" and "Legacy entries are
    ///       unaffected".
    [[nodiscard]] bool accepts(std::size_t index) const noexcept {
        if (index >= verified_.size()) {
            return false;
        }
        return !manifest_->processes[index].expected_uid.has_value() || verified_[index] != 0;
    }

    /// @brief Whether the process must present a claim at all.
    ///
    /// @param index Manifest index of the supervised process.
    /// @return True when the entry declares `expected_uid`; false for a legacy
    ///         entry or an out-of-range index.
    /// @note Spec `phm/manifest`, requirement "Expected process identity".
    [[nodiscard]] bool requires_claim(std::size_t index) const noexcept {
        return index < verified_.size() && manifest_->processes[index].expected_uid.has_value();
    }

    /// @brief Apply one drained claim.
    ///
    /// A claim for a legacy entry is ignored, a claim for an unknown name is
    /// rejected, and a claim that fails verification is rejected without
    /// changing the gate. The name is compared through `bounded_name`, so a
    /// full, unterminated buffer cannot read past the sample. Applying a claim
    /// for an already verified process is idempotent and costs no `/proc` read.
    ///
    /// @param claim Claim sample received from the app-side publisher.
    /// @return The rejection kind to publish, or `std::nullopt` when the claim
    ///         was accepted or is not required.
    /// @note Spec `phm/identity`, requirement "Claim verification through
    ///       /proc"; scenarios "An unknown name raises a security event" and
    ///       "A lost claim is retried".
    [[nodiscard]] std::optional<SecurityEventKind> apply(const CheckpointClaim& claim) noexcept {
        const std::string_view name = bounded_name(claim.name, sizeof(claim.name));
        for (std::size_t index = 0; index < manifest_->processes.size(); ++index) {
            const SupervisedProcess& process = manifest_->processes[index];
            if (process.name != name) {
                continue;
            }
            if (!process.expected_uid.has_value()) {
                return std::nullopt;
            }
            if (verified_[index] != 0) {
                // Already proven for this incarnation: a re-claim must not cost
                // another /proc read (and cannot demote the gate).
                return std::nullopt;
            }
            if (checkpoint_claim_verdict(claim, *process.expected_uid).verified) {
                verified_[index] = 1;
                return std::nullopt;
            }
            return SecurityEventKind::ClaimRejected;
        }
        return SecurityEventKind::ClaimRejected;
    }

    /// @brief Drop the verification of one process incarnation.
    ///
    /// Called by the daemon when the EM reports the process stopping, so a new
    /// incarnation must claim again before its checkpoints are trusted.
    ///
    /// @param index Manifest index of the supervised process; out-of-range
    ///        values are ignored.
    /// @note Spec `phm/identity`, requirement "Checkpoint gating", scenario "A
    ///       restarted process claims again".
    void reset(std::size_t index) noexcept {
        if (index < verified_.size()) {
            verified_[index] = 0;
        }
    }

    /// @brief Drop every verification (daemon restart or test setup).
    /// @note Spec `phm/identity`, requirement "Checkpoint gating".
    void reset_all() noexcept { std::fill(verified_.begin(), verified_.end(), 0); }

    /// @brief Number of processes with a verified claim.
    ///
    /// @return Count of manifest entries whose verified flag is set.
    /// @note Diagnostics and tests only; the supervision path uses `accepts`.
    [[nodiscard]] std::size_t verified_count() const noexcept {
        return static_cast<std::size_t>(std::count(verified_.begin(), verified_.end(), 1));
    }

    /// @brief Cold-path diagnostic rendering.
    ///
    /// @return Text of the form `ClaimRegistry(processes=<count>, verified=<count>)`.
    /// @note Mandate 9 (universal `to_string`); allocates, so cold path only.
    [[nodiscard]] std::string to_string() const {
        return std::format("ClaimRegistry(processes={}, verified={})", verified_.size(),
                           verified_count());
    }

private:
    const PhmManifest* manifest_{nullptr}; ///< Borrowed manifest; must outlive the registry.
    std::vector<char> verified_{};         ///< One flag per manifest process.
};

/// @brief Free helper mirroring `ClaimRegistry::to_string()` (mandate 9).
///
/// @param registry Registry to render.
/// @return The member diagnostic text.
/// @note Mandate 9; allocates, so cold path only.
[[nodiscard]] inline std::string to_string(const ClaimRegistry& registry) {
    return registry.to_string();
}

} // namespace nexigen::phm

/// @brief `std::formatter` for a claim registry (project mandate 11).
///
/// Makes `std::format("{}", registry)` equal `to_string(registry)`.
///
/// @note Mandate 11 (universal `std::formatter`); cold path only.
template <> struct std::formatter<nexigen::phm::ClaimRegistry> : std::formatter<std::string_view> {
    /// @brief Write the registry diagnostic into the format context.
    ///
    /// @tparam Ctx `std::format` context type, for example
    ///         `std::format_context`.
    /// @param registry Registry to format.
    /// @param ctx Output context supplied by `std::format`.
    /// @return Iterator one past the last written character.
    /// @note Mandate 11; cold path only.
    template <class Ctx> auto format(const nexigen::phm::ClaimRegistry& registry, Ctx& ctx) const {
        return std::formatter<std::string_view>::format(registry.to_string(), ctx);
    }
};
