// nexigen::sm — declarative state-management manifest (change add-sm;
// spec sm/manifest; design D8).
//
// The JSON configuration the SM reads at startup: Function Groups with their
// states and allowed transitions, transition/preparation timeouts, requester
// priorities, and the metrics flush interval. Parsed once into immutable
// structures; JSON only (nlohmann, mandate 12).
#pragma once

#include <cstdint>
#include <format>
#include <string>
#include <string_view>
#include <vector>

#include <nlohmann/json.hpp>

#include <nexigen/core/thread_management.hpp>

namespace nexigen::sm {

/// One allowed transition of a Function Group graph.
struct AllowedTransition {
    std::string from{};
    std::string to{};

    [[nodiscard]] std::string to_string() const;
};

/// One Function Group: its states, transition graph, and timeouts.
struct FunctionGroup {
    std::string name{};
    std::vector<std::string> states{};
    std::vector<AllowedTransition> allowed_transitions{};
    std::uint64_t transition_max_time_ms{1000};
    std::uint64_t preparation_timeout_ms{500};
    bool application_group{true}; ///< Moved to Off during shutdown sequencing.

    [[nodiscard]] std::string to_string() const;
};

/// One requester-priority entry (higher value = higher priority).
struct RequesterPriority {
    std::string requester_id{};
    std::int32_t priority{0};

    [[nodiscard]] std::string to_string() const;
};

/// The state-management manifest the SM loads at startup.
struct SmManifest {
    std::vector<FunctionGroup> function_groups{};
    std::vector<RequesterPriority> requester_priorities{};
    std::uint64_t metrics_flush_interval_ms{1000};
    std::uint64_t startup_timeout_ms{5000};
    core::ThreadSpec bg_thread{};    ///< fallback for infrastructure threads
    core::ThreadSpec audit_thread{}; ///< transition-audit writer spec

    /// Cold-path diagnostic (mandate 9): lists the Function Groups.
    [[nodiscard]] std::string to_string() const;

    /// Cold-path lookup: priority of `requester_id`; 0 when unknown.
    [[nodiscard]] std::int32_t priority_of(std::string_view requester_id) const noexcept;
};

/// Free helper (mandate 9): mirrors the member.
[[nodiscard]] inline std::string to_string(const SmManifest& manifest) {
    return manifest.to_string();
}

/// Cold-path ADL serialization (mandate 10), non-throwing readers under
/// JSON_NOEXCEPTION=1.
void to_json(nlohmann::json& j, const AllowedTransition& transition);
void to_json(nlohmann::json& j, const FunctionGroup& group);
void to_json(nlohmann::json& j, const RequesterPriority& priority);
void to_json(nlohmann::json& j, const SmManifest& manifest);
void from_json(const nlohmann::json& j, AllowedTransition& transition) noexcept;
void from_json(const nlohmann::json& j, FunctionGroup& group) noexcept;
void from_json(const nlohmann::json& j, RequesterPriority& priority) noexcept;
void from_json(const nlohmann::json& j, SmManifest& manifest) noexcept;

} // namespace nexigen::sm

/// The manifest formats as its diagnostic string (mandate 11).
template <> struct std::formatter<nexigen::sm::SmManifest> : std::formatter<std::string_view> {
    template <class Ctx> auto format(const nexigen::sm::SmManifest& manifest, Ctx& ctx) const {
        const std::string text = manifest.to_string();
        return std::formatter<std::string_view>::format(text, ctx);
    }
};
