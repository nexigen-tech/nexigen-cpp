// nexigen::exec — cgroup v2 resource-group budgets (change cpu-time-budgets).
#include <nexigen/exec/cgroup_budget.hpp>

#include <cstdint>
#include <filesystem>
#include <fstream>
#include <system_error>

namespace nexigen::exec {

namespace {

constexpr std::uint64_t k_bytes_per_mib = 1024ULL * 1024ULL;

/// @brief Write one cgroup control file, truncating it.
[[nodiscard]] core::Status write_control(const std::filesystem::path& path,
                                         const std::string& value) noexcept {
    std::ofstream out(path, std::ios::binary | std::ios::trunc);
    if (!out.is_open()) {
        return std::unexpected(core::ErrorCode::SystemError);
    }
    out << value;
    if (!out.good()) {
        return std::unexpected(core::ErrorCode::SystemError);
    }
    return {};
}

} // namespace

std::string resource_group_cgroup_path(const SuiteManifest& manifest,
                                       const ResourceGroupConfig& group) {
    std::filesystem::path path(cgroup_root_of(manifest));
    path /= "nexigen";
    path /= manifest.suite_id;
    path /= group.name;
    return path.string();
}

core::Status prepare_resource_groups(const SuiteManifest& manifest) noexcept {
    for (const ResourceGroupConfig& group : manifest.resource_groups) {
        if (!group_has_cgroup_budget(group)) {
            continue;
        }
        const std::filesystem::path directory(resource_group_cgroup_path(manifest, group));
        std::error_code error;
        std::filesystem::create_directories(directory, error);
        if (error) {
            return std::unexpected(core::ErrorCode::SystemError);
        }
        if (group.cpu_quota_us > 0U) {
            const std::string cpu_max =
                std::to_string(group.cpu_quota_us) + " " + std::to_string(group.cpu_period_us);
            if (const core::Status written = write_control(directory / "cpu.max", cpu_max);
                !written) {
                return written;
            }
        }
        if (group.memory_max_mb > 0U) {
            const std::string memory_max =
                std::to_string(group.memory_max_mb * k_bytes_per_mib);
            if (const core::Status written = write_control(directory / "memory.max", memory_max);
                !written) {
                return written;
            }
        }
    }
    return {};
}

} // namespace nexigen::exec
