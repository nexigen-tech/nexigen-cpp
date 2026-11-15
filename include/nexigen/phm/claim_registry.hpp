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
/// @note Spec `phm/identity`, requirements "Claim verification through /proc",
///       "Checkpoint gating", and "Security event channel".
class ClaimRegistry {
public:
    /// @brief Build a registry in which nothing is verified yet.
    ///
    /// @param manifest Manifest whose entries define the expectations.
    explicit ClaimRegistry(const PhmManifest& manifest) noexcept
        : manifest_(&manifest), verified_(manifest.processes.size(), 0) {}

    /// @brief Whether checkpoints from this process may reach the supervisor.
    ///
    /// @param index Manifest index of the supervised process.
    /// @return True for a legacy entry, or for an entry with a verified claim.
    /// @note Spec `phm/identity`, requirement "Checkpoint gating".
    [[nodiscard]] bool accepts(std::size_t index) const noexcept {
        if (index >= verified_.size()) {
            return false;
        }
        return !manifest_->processes[index].expected_uid.has_value() || verified_[index] != 0;
    }

    /// @brief Whether the process must present a claim at all.
    ///
    /// @param index Manifest index of the supervised process.
    /// @return True when the entry declares `expected_uid`.
    [[nodiscard]] bool requires_claim(std::size_t index) const noexcept {
        return index < verified_.size() && manifest_->processes[index].expected_uid.has_value();
    }

    /// @brief Apply one drained claim.
    ///
    /// A claim for a legacy entry is ignored, a claim for an unknown name is
    /// rejected, and a claim that fails verification is rejected without
    /// changing the gate.
    ///
    /// @param claim Claim sample received from the app-side publisher.
    /// @return The rejection kind to publish, or `std::nullopt` when the claim
    ///         was accepted or is not required.
    /// @note Spec `phm/identity`, requirement "Claim verification through /proc".
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
    /// @param index Manifest index of the supervised process.
    void reset(std::size_t index) noexcept {
        if (index < verified_.size()) {
            verified_[index] = 0;
        }
    }

    /// @brief Drop every verification (daemon restart or test setup).
    void reset_all() noexcept { std::fill(verified_.begin(), verified_.end(), 0); }

    /// @brief Number of processes with a verified claim.
    [[nodiscard]] std::size_t verified_count() const noexcept {
        return static_cast<std::size_t>(std::count(verified_.begin(), verified_.end(), 1));
    }

    /// @brief Cold-path diagnostic rendering.
    [[nodiscard]] std::string to_string() const {
        return std::format("ClaimRegistry(processes={}, verified={})", verified_.size(),
                           verified_count());
    }

private:
    const PhmManifest* manifest_{nullptr}; ///< Borrowed manifest; must outlive the registry.
    std::vector<char> verified_{};         ///< One flag per manifest process.
};

/// @brief Free helper mirroring `ClaimRegistry::to_string()` (mandate 9).
[[nodiscard]] inline std::string to_string(const ClaimRegistry& registry) {
    return registry.to_string();
}

} // namespace nexigen::phm

/// @brief `std::formatter` for a claim registry (project mandate 11).
template <> struct std::formatter<nexigen::phm::ClaimRegistry> : std::formatter<std::string_view> {
    template <class Ctx> auto format(const nexigen::phm::ClaimRegistry& registry, Ctx& ctx) const {
        return std::formatter<std::string_view>::format(registry.to_string(), ctx);
    }
};
