// nexigen::logging — unified runtime configuration for the async hot-path
// engine (change rework-logging-hot-path-engine, design D1/D7/D8; specs
// logging/logger-options).
//
// `LoggerOptions` is a single aggregate applied at logger initialization,
// replacing the compile-time template parameters (ShardCapacity,
// MessageCapacity, MaxThreads) that previously fixed every async logger.
// Construction and validation are cold path; all runtime reconfiguration
// goes through wait-free relaxed-atomic setters.
//
// Every public type here implements the full serialization mandate:
// member + free `to_string()`, `from_string()` for enums, non-throwing ADL
// `to_json`/`from_json` under `JSON_NOEXCEPTION=1`, and a `std::formatter`
// specialization. Factory-bound types (none in this header — all are plain
// configuration values) provide `to_json` only.
//
// Hot-path impact: zero. `LoggerOptions` is read once at `initialize()`;
// thereafter producers consult the relaxed atomic level/spam config
// published by `set_log_level()` / `set_spam_protection_config()`.
#pragma once

#include <bit>
#include <cstddef>
#include <cstdint>
#include <format>
#include <string>
#include <string_view>
#include <vector>

#include <nlohmann/json.hpp>

#include <nexigen/core/result.hpp>
#include <nexigen/logging/log_level.hpp>

namespace nexigen::logging {

/// Overflow policy for a full SPSC shard ring.
enum class OverflowPolicy : std::uint8_t {
    DropNewest,    ///< Incoming record is dropped; `buffer_full` counted.
    OverwriteOldest, ///< Oldest unread record is overwritten atomically;
                     ///< `dropped_count_` incremented.
};

[[nodiscard]] constexpr std::string_view to_string(OverflowPolicy p) noexcept {
    switch (p) {
        case OverflowPolicy::DropNewest:    return "drop_newest";
        case OverflowPolicy::OverwriteOldest: return "overwrite_oldest";
        default:                            return "Unknown";
    }
}
[[nodiscard]] constexpr bool from_string(std::string_view name,
                                         OverflowPolicy& out) noexcept {
    if (name == "drop_newest") { out = OverflowPolicy::DropNewest; return true; }
    if (name == "overwrite_oldest") { out = OverflowPolicy::OverwriteOldest; return true; }
    return false;
}
inline void to_json(nlohmann::json& j, const OverflowPolicy& p) {
    j = std::string(to_string(p));
}
inline void from_json(const nlohmann::json& j, OverflowPolicy& out) noexcept {
    const auto* name = j.get_ptr<const nlohmann::json::string_t*>();
    if (name != nullptr) {
        static_cast<void>(from_string(std::string_view(*name), out));
    }
}

/// Shard lifecycle state observed by the consumer drain loop.
enum class ShardState : std::uint8_t {
    Active,  ///< Slot registered and accepting records.
    Draining,///< Producer thread exited; consumer must drain then free.
    Free,    ///< Returned to the free-slot pool.
};

[[nodiscard]] constexpr std::string_view to_string(ShardState s) noexcept {
    switch (s) {
        case ShardState::Active:   return "active";
        case ShardState::Draining: return "draining";
        case ShardState::Free:     return "free";
        default:                   return "Unknown";
    }
}
[[nodiscard]] constexpr bool from_string(std::string_view name,
                                         ShardState& out) noexcept {
    if (name == "active") { out = ShardState::Active; return true; }
    if (name == "draining") { out = ShardState::Draining; return true; }
    if (name == "free") { out = ShardState::Free; return true; }
    return false;
}
inline void to_json(nlohmann::json& j, const ShardState& s) {
    j = std::string(to_string(s));
}
inline void from_json(const nlohmann::json& j, ShardState& out) noexcept {
    const auto* name = j.get_ptr<const nlohmann::json::string_t*>();
    if (name != nullptr) {
        static_cast<void>(from_string(std::string_view(*name), out));
    }
}

/// Anti-spam quotas and suppression policy (nested inside LoggerOptions).
struct SpamProtectionConfig {
    bool enable_repeat_suppression{true};                  ///< Level 1 on.
    std::uint32_t repeat_suppression_window_ms{1000};     ///< Collapse window.
    std::uint32_t repeat_suppression_max_count{100000};   ///< Summary threshold.
    std::uint32_t max_messages_per_sec_per_shard{100000}; ///< 0 = no limit.
    std::uint64_t max_bytes_per_sec_per_shard{52428800ULL};///< 0 = no limit.
    std::uint64_t max_total_bytes_per_sec{209715200ULL};  ///< 0 = no limit.
    LogLevel bypass_min_level{LogLevel::Fatal};           ///< Always-pass floor (Fatal = highest before Off).

    [[nodiscard]] std::string to_string() const;
};
[[nodiscard]] inline std::string to_string(const SpamProtectionConfig& cfg) {
    return cfg.to_string();
}

/// Unified runtime configuration for the async logging engine.
struct LoggerOptions {
    std::string log_root_directory{"./logs"};
    std::string app_name{"nexigen_app"};
    LogLevel initial_log_level{LogLevel::Info};
    std::uint64_t max_file_size_bytes{104857600ULL}; ///< 0 = no limit.
    std::uint32_t spsc_ring_buffer_capacity{65536U}; ///< rounded up via bit_ceil.
    std::uint32_t message_capacity{128U}; ///< inline argument blob capacity (max bytes per record).
    std::uint32_t max_producer_threads{64U};
    OverflowPolicy overflow_policy{OverflowPolicy::DropNewest};
    std::vector<std::uint64_t> flush_cpu_affinity{};   ///< empty = OS placement.
    std::uint32_t worker_poll_interval_us{1000U};     ///< 1 ms.
    bool sync_flush_on_critical{true};
    SpamProtectionConfig spam_protection{};

    [[nodiscard]] std::string to_string() const;
};
[[nodiscard]] inline std::string to_string(const LoggerOptions& opts) {
    return opts.to_string();
}

// --- free to_string mirrors (mandate 9) are already provided by the
//     constexpr overloads above (string_view); no std::string wrapper needed.

// --- ADL JSON for configuration structs (cold path; definitions in
//     src/logging/logger_options_format.cpp) ---
void to_json(nlohmann::json& j, const SpamProtectionConfig& cfg);
void from_json(const nlohmann::json& j, SpamProtectionConfig& out) noexcept;

void to_json(nlohmann::json& j, const LoggerOptions& opts);
void from_json(const nlohmann::json& j, LoggerOptions& out) noexcept;

} // namespace nexigen::logging

// --- std::formatter specializations (mandate 11): must live in namespace std,
//     outside nexigen::logging (same pattern as log_level.hpp).
template <>
struct std::formatter<nexigen::logging::OverflowPolicy> : std::formatter<std::string_view> {
    template <class Ctx>
    constexpr auto format(const nexigen::logging::OverflowPolicy& p, Ctx& ctx) const {
        return std::formatter<std::string_view>::format(nexigen::logging::to_string(p), ctx);
    }
};

template <>
struct std::formatter<nexigen::logging::ShardState> : std::formatter<std::string_view> {
    template <class Ctx>
    constexpr auto format(const nexigen::logging::ShardState& s, Ctx& ctx) const {
        return std::formatter<std::string_view>::format(nexigen::logging::to_string(s), ctx);
    }
};

template <>
struct std::formatter<nexigen::logging::SpamProtectionConfig> : std::formatter<std::string_view> {
    template <class Ctx>
    constexpr auto format(const nexigen::logging::SpamProtectionConfig& cfg, Ctx& ctx) const {
        return std::formatter<std::string_view>::format(nexigen::logging::to_string(cfg), ctx);
    }
};

template <>
struct std::formatter<nexigen::logging::LoggerOptions> : std::formatter<std::string_view> {
    template <class Ctx>
    constexpr auto format(const nexigen::logging::LoggerOptions& opts, Ctx& ctx) const {
        return std::formatter<std::string_view>::format(nexigen::logging::to_string(opts), ctx);
    }
};
