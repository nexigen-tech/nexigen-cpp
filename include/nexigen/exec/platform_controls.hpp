// nexigen::exec — P2 platform controls in the suite manifest
// (change p2-exec-platform-controls; specs exec/resource-groups,
// exec/trusted-executable, exec/state-client-access-control).
#pragma once

#include <cstdint>
#include <format>
#include <string>
#include <string_view>

#include <nlohmann/json.hpp>

#include <nexigen/core/thread_management.hpp>

namespace nexigen::exec {

/// @brief One named resource group.
struct ResourceGroupConfig {
    std::string name{};               ///< Unique group name.
    std::uint64_t memory_limit_mb{0}; ///< RLIMIT_AS budget; 0 = unlimited.
    core::CpuSet cpus{};              ///< Default CPU set; empty = unrestricted.
    std::string working_dir{};        ///< Child working folder; empty = inherit.
    std::uint64_t cpu_quota_us{0};    ///< cgroup cpu.max quota; 0 = unlimited.
    std::uint64_t cpu_period_us{100000}; ///< cgroup cpu.max period (1000..1000000).
    std::uint64_t memory_max_mb{0};   ///< cgroup memory.max budget; 0 = unlimited.

    /// @brief Cold-path diagnostic rendering.
    [[nodiscard]] std::string to_string() const;
};

/// @brief Suite-wide trusted executable policy.
struct TrustedPlatformConfig {
    bool require_executable_digest{false}; ///< Require a digest for every app.
    bool require_non_root{false};          ///< Refuse root launches without an explicit uid.

    /// @brief Cold-path diagnostic rendering.
    [[nodiscard]] std::string to_string() const;
};

/// @brief One authorized `(requester, Function Group)` pair.
struct FunctionGroupPortMapping {
    std::string requester_id{};   ///< Authorized requester identity.
    std::string function_group{}; ///< Function Group it may switch.

    /// @brief Cold-path diagnostic rendering.
    [[nodiscard]] std::string to_string() const;
};

/// @brief Free helper (project mandate 9).
[[nodiscard]] inline std::string to_string(const ResourceGroupConfig& value) {
    return value.to_string();
}
/// @brief Free helper (project mandate 9).
[[nodiscard]] inline std::string to_string(const TrustedPlatformConfig& value) {
    return value.to_string();
}
/// @brief Free helper (project mandate 9).
[[nodiscard]] inline std::string to_string(const FunctionGroupPortMapping& value) {
    return value.to_string();
}

void to_json(nlohmann::json& j, const ResourceGroupConfig& value);
void from_json(const nlohmann::json& j, ResourceGroupConfig& value) noexcept;
void to_json(nlohmann::json& j, const TrustedPlatformConfig& value);
void from_json(const nlohmann::json& j, TrustedPlatformConfig& value) noexcept;
void to_json(nlohmann::json& j, const FunctionGroupPortMapping& value);
void from_json(const nlohmann::json& j, FunctionGroupPortMapping& value) noexcept;

/// @brief Whether a string is a 64-character hex SHA-256 digest.
[[nodiscard]] bool is_sha256_hex(std::string_view text) noexcept;

/// @brief Whether a manifest control identifier is non-empty and safe.
[[nodiscard]] bool valid_control_id(std::string_view text) noexcept;

} // namespace nexigen::exec

template <>
struct std::formatter<nexigen::exec::ResourceGroupConfig> : std::formatter<std::string_view> {
    template <class Ctx>
    auto format(const nexigen::exec::ResourceGroupConfig& value, Ctx& ctx) const {
        return std::formatter<std::string_view>::format(value.to_string(), ctx);
    }
};

template <>
struct std::formatter<nexigen::exec::TrustedPlatformConfig> : std::formatter<std::string_view> {
    template <class Ctx>
    auto format(const nexigen::exec::TrustedPlatformConfig& value, Ctx& ctx) const {
        return std::formatter<std::string_view>::format(value.to_string(), ctx);
    }
};

template <>
struct std::formatter<nexigen::exec::FunctionGroupPortMapping> : std::formatter<std::string_view> {
    template <class Ctx>
    auto format(const nexigen::exec::FunctionGroupPortMapping& value, Ctx& ctx) const {
        return std::formatter<std::string_view>::format(value.to_string(), ctx);
    }
};
