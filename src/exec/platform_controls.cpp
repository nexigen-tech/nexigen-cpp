// nexigen::exec — P2 platform control manifest types
// (change p2-exec-platform-controls).
#include <nexigen/exec/platform_controls.hpp>

#include <cctype>

#include <nexigen/core/detail/json_field.hpp>

namespace nexigen::exec {

namespace {

[[nodiscard]] bool safe_control_id(std::string_view text) noexcept {
    if (text.empty()) {
        return false;
    }
    for (const char ch : text) {
        const unsigned char byte = static_cast<unsigned char>(ch);
        if (!(std::isalnum(byte) != 0 || ch == '_' || ch == '-' || ch == '.')) {
            return false;
        }
    }
    return true;
}

} // namespace

std::string ResourceGroupConfig::to_string() const {
    return std::format("ResourceGroupConfig(name={}, memory_limit_mb={}, cpus={}, working_dir={})",
                       name, memory_limit_mb, cpus, working_dir);
}

std::string TrustedPlatformConfig::to_string() const {
    return std::format("TrustedPlatformConfig(require_executable_digest={}, require_non_root={})",
                       require_executable_digest, require_non_root);
}

std::string FunctionGroupPortMapping::to_string() const {
    return std::format("FunctionGroupPortMapping(requester_id={}, function_group={})", requester_id,
                       function_group);
}

void to_json(nlohmann::json& j, const ResourceGroupConfig& value) {
    j = nlohmann::json{{"name", value.name},
                       {"memory_limit_mb", value.memory_limit_mb},
                       {"cpus", value.cpus},
                       {"working_dir", value.working_dir},
                       {"cpu_quota_us", value.cpu_quota_us},
                       {"cpu_period_us", value.cpu_period_us},
                       {"memory_max_mb", value.memory_max_mb}};
}

void from_json(const nlohmann::json& j, ResourceGroupConfig& value) noexcept {
    static_cast<void>(core::detail::read_string(j, "name", value.name));
    static_cast<void>(core::detail::read_unsigned(j, "memory_limit_mb", value.memory_limit_mb));
    if (const auto it = j.find("cpus"); it != j.end()) {
        core::CpuSet parsed;
        core::from_json(*it, parsed);
        value.cpus = parsed;
    }
    static_cast<void>(core::detail::read_string(j, "working_dir", value.working_dir));
    static_cast<void>(core::detail::read_unsigned(j, "cpu_quota_us", value.cpu_quota_us));
    static_cast<void>(core::detail::read_unsigned(j, "cpu_period_us", value.cpu_period_us));
    static_cast<void>(core::detail::read_unsigned(j, "memory_max_mb", value.memory_max_mb));
}

void to_json(nlohmann::json& j, const TrustedPlatformConfig& value) {
    j = nlohmann::json{{"require_executable_digest", value.require_executable_digest},
                       {"require_non_root", value.require_non_root}};
}

void from_json(const nlohmann::json& j, TrustedPlatformConfig& value) noexcept {
    static_cast<void>(
        core::detail::read_bool(j, "require_executable_digest", value.require_executable_digest));
    static_cast<void>(core::detail::read_bool(j, "require_non_root", value.require_non_root));
}

void to_json(nlohmann::json& j, const FunctionGroupPortMapping& value) {
    j = nlohmann::json{{"requester_id", value.requester_id},
                       {"function_group", value.function_group}};
}

void from_json(const nlohmann::json& j, FunctionGroupPortMapping& value) noexcept {
    static_cast<void>(core::detail::read_string(j, "requester_id", value.requester_id));
    static_cast<void>(core::detail::read_string(j, "function_group", value.function_group));
}

bool is_sha256_hex(std::string_view text) noexcept {
    if (text.size() != 64U) {
        return false;
    }
    for (const char ch : text) {
        const unsigned char byte = static_cast<unsigned char>(ch);
        if (std::isxdigit(byte) == 0) {
            return false;
        }
    }
    return true;
}

/// @brief Internal helper used by manifest validation.
[[nodiscard]] bool valid_control_id(std::string_view text) noexcept {
    return safe_control_id(text);
}

} // namespace nexigen::exec
