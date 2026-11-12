// nexigen::exec — shared suite/manifest JSON field helpers
// (change strengthen-json-field-vocabulary; spec execution_manager/app-suite).
//
// These helpers parse the suite manifest and app-spec vocabulary. They were
// duplicated verbatim between src/exec/app_suite.cpp and
// src/exec/suite_manifest.cpp; this header is their single definition site.
//
// COLD PATH ONLY. Manifest parsing runs at startup, never on a supervision or
// worker hot path. nlohmann::json and std::string are allowed here by design.
#pragma once

#include <cstdint>
#include <limits>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include <nlohmann/json.hpp>

#include <nexigen/core/log_level.hpp>
#include <nexigen/core/result.hpp>
#include <nexigen/core/thread_management.hpp>

namespace nexigen::exec::detail {

/// Read an optional log-level field: `null` clears `target`, a string parses
/// through `core::from_string`, anything else leaves `target` untouched.
[[nodiscard]] inline bool read_log_level(const nlohmann::json& document, const char* key,
                                         std::optional<core::LogLevel>& target) noexcept {
    if (!document.is_object()) {
        return false;
    }
    const auto it = document.find(key);
    if (it == document.end()) {
        return false;
    }
    if (it->is_null()) {
        target = std::nullopt;
        return true;
    }
    if (!it->is_string()) {
        return false;
    }
    const auto* const name = it->get_ptr<const nlohmann::json::string_t*>();
    if (name == nullptr) {
        return false;
    }
    core::LogLevel level = core::LogLevel::Info;
    if (!core::from_string(std::string_view(*name), level)) {
        return false;
    }
    target = level;
    return true;
}

/// Serialize an optional log level as a JSON string or `null`.
[[nodiscard]] inline nlohmann::json log_level_json(const std::optional<core::LogLevel>& level) {
    if (!level.has_value()) {
        return nullptr;
    }
    return std::string(core::to_string(*level));
}

/// Read an array of strings; a non-array field or a non-string element rejects
/// (returns false and leaves `target` untouched).
[[nodiscard]] inline bool read_string_array(const nlohmann::json& document, const char* key,
                                            std::vector<std::string>& target) noexcept {
    if (!document.is_object()) {
        return false;
    }
    const auto it = document.find(key);
    if (it == document.end() || !it->is_array()) {
        return false;
    }
    std::vector<std::string> values;
    values.reserve(it->size());
    for (const auto& item : *it) {
        if (!item.is_string()) {
            return false;
        }
        values.emplace_back(item.get<std::string>());
    }
    target = std::move(values);
    return true;
}

/// Read an array of CPU ids bounded by `uint32_t`; a non-array field, a
/// non-unsigned element, or an out-of-range value rejects.
[[nodiscard]] inline bool read_cpu_array(const nlohmann::json& document, const char* key,
                                         std::vector<std::uint32_t>& target) noexcept {
    if (!document.is_object()) {
        return false;
    }
    const auto it = document.find(key);
    if (it == document.end() || !it->is_array()) {
        return false;
    }
    std::vector<std::uint32_t> values;
    values.reserve(it->size());
    for (const auto& item : *it) {
        if (!item.is_number_unsigned()) {
            return false;
        }
        const std::uint64_t value = item.get<std::uint64_t>();
        if (value > UINT32_MAX) {
            return false;
        }
        values.push_back(static_cast<std::uint32_t>(value));
    }
    target = std::move(values);
    return true;
}

/// Read a CPU set through `core::parse_cpu_set_json`.
[[nodiscard]] inline bool read_cpu_set(const nlohmann::json& document, const char* key,
                                       core::CpuSet& target) noexcept {
    if (!document.is_object()) {
        return false;
    }
    const auto it = document.find(key);
    if (it == document.end()) {
        return false;
    }
    return core::parse_cpu_set_json(*it, target);
}

/// Read the legacy execution-manager CPU list into a ThreadSpec.
inline void read_legacy_execution_manager_cpus(const nlohmann::json& document, const char* key,
                                               core::ThreadSpec& target) noexcept {
    std::vector<std::uint32_t> values;
    if (!read_cpu_array(document, key, values)) {
        return;
    }
    const core::Result<core::CpuSet> converted = core::CpuSet::from_cpus(values);
    if (converted) {
        target.set_cpus(*converted);
    }
}

/// Read the canonical `runner_control_thread`/`runner_logger_thread` object.
inline void read_canonical_thread(const nlohmann::json& document, const char* key,
                                  core::ThreadSpec& target) noexcept {
    const auto it = document.find(key);
    if (it != document.end()) {
        static_cast<void>(core::parse_thread_spec_json(*it, target));
    }
}

/// Read an object of string pairs (environment map) into a vector.
[[nodiscard]] inline bool
read_env_object(const nlohmann::json& document, const char* key,
                std::vector<std::pair<std::string, std::string>>& target) noexcept {
    if (!document.is_object()) {
        return false;
    }
    const auto it = document.find(key);
    if (it == document.end() || !it->is_object()) {
        return false;
    }
    std::vector<std::pair<std::string, std::string>> values;
    values.reserve(it->size());
    for (auto entry = it->begin(); entry != it->end(); ++entry) {
        if (!entry.value().is_string()) {
            return false;
        }
        values.emplace_back(entry.key(), entry.value().get<std::string>());
    }
    target = std::move(values);
    return true;
}

} // namespace nexigen::exec::detail
