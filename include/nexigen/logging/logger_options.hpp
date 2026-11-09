// nexigen::logging — unified runtime configuration for the async hot-path
// engine (change rework-logging-hot-path-engine, design D1/D7/D8; spec
// logging/logger-options).
//
// `LoggerOptions` is a single aggregate applied at logger initialization,
// replacing the compile-time template parameters (ShardCapacity,
// MessageCapacity, MaxThreads) that previously fixed every async logger.
// Construction and validation are cold path; all runtime reconfiguration
// goes through wait-free relaxed-atomic setters.
//
// Hot-path impact: zero. `LoggerOptions` is read once at `initialize()`;
// thereafter producers consult the relaxed atomic level/spam config
// published by `set_log_level()` / `set_spam_protection_config()`.
#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>

#include <nlohmann/json.hpp>

#include <nexigen/core/result.hpp>
#include <nexigen/core/thread_management.hpp>
#include <nexigen/core/log_level.hpp>
#include <nexigen/logging/overflow_policy.hpp>
#include <nexigen/logging/shard_state.hpp>
#include <nexigen/logging/spam_protection_config.hpp>

namespace nexigen::logging {

/// Unified runtime configuration for the async logging engine.
struct LoggerOptions {
    std::string log_root_directory{"./logs"};
    core::LogLevel initial_log_level{core::LogLevel::Info};
    std::uint64_t max_file_size_bytes{104857600ULL}; ///< 0 = no limit.
    std::uint32_t spsc_ring_buffer_capacity{65536U}; ///< rounded up via bit_ceil.
    std::uint32_t message_capacity{128U}; ///< inline argument blob capacity (max bytes per record).
    std::uint32_t max_producer_threads{64U};
    OverflowPolicy overflow_policy{OverflowPolicy::DropNewest};
    core::ThreadSpec worker_thread{};             ///< canonical file-writer attributes.
    std::uint32_t worker_poll_interval_us{1000U}; ///< 1 ms.
    bool sync_flush_on_critical{true};
    SpamProtectionConfig spam_protection{};

    [[nodiscard]] std::string to_string() const;
};
[[nodiscard]] inline std::string to_string(const LoggerOptions& opts) {
    return opts.to_string();
}

// --- ADL JSON for LoggerOptions (cold path; definition in
//     src/logging/logger_options.cpp) ---
void to_json(nlohmann::json& j, const LoggerOptions& opts);
void from_json(const nlohmann::json& j, LoggerOptions& out) noexcept;

} // namespace nexigen::logging
