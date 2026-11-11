// nexigen::phm — declarative health manifest (change add-phm; spec
// phm/manifest; design D7).
//
// The JSON configuration the daemon reads at startup: supervised processes
// with their iceoryx2 service identifiers, Alive parameters, Logical
// transition graphs, Deadline pairs, and Recovery rules. Parsed once into an
// immutable config; JSON only (nlohmann, mandate 12) — YAML input is a future
// converter.
#pragma once

#include <cstdint>
#include <format>
#include <string>
#include <string_view>
#include <vector>

#include <nlohmann/json.hpp>

#include <nexigen/phm/recovery_action.hpp>

namespace nexigen::phm {

/// One allowed checkpoint transition in the logical-supervision graph.
struct CheckpointTransition {
    std::uint32_t from_id{0};
    std::uint32_t to_id{0};

    [[nodiscard]] std::string to_string() const;
};

/// One deadline-supervision pair: source -> target with min/max limits.
struct DeadlinePair {
    std::uint32_t source_id{0};
    std::uint32_t target_id{0};
    std::uint64_t min_ms{0};
    std::uint64_t max_ms{0};

    [[nodiscard]] std::string to_string() const;
};

/// One recovery rule attached to an Expired process.
struct RecoveryRule {
    RecoveryAction action{RecoveryAction::RestartProcess};
    std::string function_group{}; ///< Only used by SwitchFunctionGroupState.

    [[nodiscard]] std::string to_string() const;
};

/// One supervised process: identity plus every supervision parameter.
struct SupervisedProcess {
    std::string name{};
    std::string service_name{}; ///< iceoryx2 service identifier (checkpoints channel).
    std::uint64_t alive_reference_cycle_ms{0};
    std::uint64_t alive_min_margin_ms{0};
    std::uint64_t alive_max_margin_ms{0};
    std::uint64_t failed_supervision_cycles_tolerance{0};
    bool safety_critical{false};
    std::vector<std::uint32_t> checkpoints{};        ///< Expected checkpoint ids (alive).
    std::vector<CheckpointTransition> transitions{}; ///< Logical graph edges.
    std::vector<DeadlinePair> deadlines{};           ///< Deadline pairs with limits.
    std::vector<RecoveryRule> recovery_rules{};      ///< Actions on Expired.

    [[nodiscard]] std::string to_string() const;
};

/// The health-logic manifest the daemon loads at startup.
struct PhmManifest {
    std::vector<SupervisedProcess> processes{};

    /// Cold-path diagnostic (mandate 9): lists the supervised processes.
    [[nodiscard]] std::string to_string() const;
};

/// Free helper (mandate 9): mirrors the member.
[[nodiscard]] inline std::string to_string(const PhmManifest& manifest) {
    return manifest.to_string();
}

/// Cold-path ADL serialization (mandate 10), non-throwing readers under
/// JSON_NOEXCEPTION=1.
void to_json(nlohmann::json& j, const CheckpointTransition& transition);
void to_json(nlohmann::json& j, const DeadlinePair& pair);
void to_json(nlohmann::json& j, const RecoveryRule& rule);
void to_json(nlohmann::json& j, const SupervisedProcess& process);
void to_json(nlohmann::json& j, const PhmManifest& manifest);
void from_json(const nlohmann::json& j, CheckpointTransition& transition) noexcept;
void from_json(const nlohmann::json& j, DeadlinePair& pair) noexcept;
void from_json(const nlohmann::json& j, RecoveryRule& rule) noexcept;
void from_json(const nlohmann::json& j, SupervisedProcess& process) noexcept;
void from_json(const nlohmann::json& j, PhmManifest& manifest) noexcept;

} // namespace nexigen::phm

/// The manifest formats as its diagnostic string (mandate 11).
template <> struct std::formatter<nexigen::phm::PhmManifest> : std::formatter<std::string_view> {
    template <class Ctx> auto format(const nexigen::phm::PhmManifest& manifest, Ctx& ctx) const {
        const std::string text = manifest.to_string();
        return std::formatter<std::string_view>::format(text, ctx);
    }
};
