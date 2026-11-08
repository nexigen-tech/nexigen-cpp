// nexigen::logging — cold-path JSON serialization for SpamProtectionConfig
// (change rework-logging-hot-path-engine, design D7; spec logging/logger-options).
//
// Strictly non-throwing under `JSON_NOEXCEPTION=1`: every access is guarded by
// `is_*` checks, and a wrong shape leaves the target untouched.
// nlohmann/json headers trigger -Wnull-dereference under -O3 (pre-existing
// upstream issue). Suppress for this TU only — the JSON code is cold-path.
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wnull-dereference"
#include <nlohmann/json.hpp>
#pragma GCC diagnostic pop

#include <cstdint>
#include <string>
#include <string_view>

#include <nexigen/logging/log_level.hpp>
#include <nexigen/logging/spam_protection_config.hpp>

namespace nexigen::logging {

namespace {

void read_u32_field(const nlohmann::json& j, const char* key, std::uint32_t& out) noexcept {
    const auto it = j.find(key);
    if (it == j.end()) {
        return;
    }
    if (!it->is_number_unsigned()) {
        return;
    }
    out = it->get<std::uint32_t>();
}

void read_bool_field(const nlohmann::json& j, const char* key, bool& out) noexcept {
    const auto it = j.find(key);
    if (it == j.end()) {
        return;
    }
    if (!it->is_boolean()) {
        return;
    }
    out = it->get<bool>();
}

void read_log_level_field(const nlohmann::json& j, const char* key, LogLevel& out) noexcept {
    const auto it = j.find(key);
    if (it == j.end()) {
        return;
    }
    const auto* const text = it->get_ptr<const nlohmann::json::string_t*>();
    if (text == nullptr) {
        return;
    }
    static_cast<void>(from_string(std::string_view(*text), out));
}

} // namespace

void to_json(nlohmann::json& j, const SpamProtectionConfig& cfg) {
    j = nlohmann::json{
        {"enable_repeat_suppression", cfg.enable_repeat_suppression},
        {"repeat_suppression_window_ms", cfg.repeat_suppression_window_ms},
        {"max_messages_per_sec_per_shard", cfg.max_messages_per_sec_per_shard},
        {"bypass_min_level", std::string(to_string(cfg.bypass_min_level))},
    };
}

void from_json(const nlohmann::json& j, SpamProtectionConfig& out) noexcept {
    if (!j.is_object()) {
        return;
    }
    read_bool_field(j, "enable_repeat_suppression", out.enable_repeat_suppression);
    read_u32_field(j, "repeat_suppression_window_ms", out.repeat_suppression_window_ms);
    read_u32_field(j, "max_messages_per_sec_per_shard", out.max_messages_per_sec_per_shard);
    read_log_level_field(j, "bypass_min_level", out.bypass_min_level);
}

std::string SpamProtectionConfig::to_string() const {
    return "SpamProtectionConfig(enable_repeat_suppression=" +
           std::string(enable_repeat_suppression ? "true" : "false") +
           ", repeat_suppression_window_ms=" + std::to_string(repeat_suppression_window_ms) +
           ", max_messages_per_sec_per_shard=" + std::to_string(max_messages_per_sec_per_shard) +
           ", bypass_min_level=" + std::string(nexigen::logging::to_string(bypass_min_level)) + ")";
}

} // namespace nexigen::logging
