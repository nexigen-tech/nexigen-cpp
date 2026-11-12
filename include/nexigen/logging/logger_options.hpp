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

/// @brief Unified runtime configuration for the async logging engine.
///
/// A single aggregate applied once by `initialize()`, replacing the former
/// compile-time template parameters. Every field is read during initialization —
/// ring sizes, the slot array, the sink, and the worker's `ThreadSpec` are all
/// fixed from it — and afterwards the engine consults only its own relaxed
/// atomics, so the struct is never touched by a producer.
///
/// The struct owns `std::string` members and is therefore not trivially
/// copyable; copying it is a cold-path operation.
///
/// @note Spec `logging/logger-options`, requirement
///       "LoggerOptions defines the engine configuration".
struct LoggerOptions {
    /// @brief Root directory for log output; the sink creates period subdirectories below it.
    std::string log_root_directory{"./logs"};
    /// @brief Severity threshold applied at initialization (runtime changes go through
    ///        `set_log_level`).
    core::LogLevel initial_log_level{core::LogLevel::Info};
    /// @brief Size-based rotation threshold in bytes; `0` = no limit.
    std::uint64_t max_file_size_bytes{104857600ULL};
    /// @brief Requested shard ring capacity; rounded up to a power of two via `std::bit_ceil`.
    std::uint32_t spsc_ring_buffer_capacity{65536U};
    /// @brief Inline argument blob capacity: the maximum serialized bytes per record.
    std::uint32_t message_capacity{128U};
    /// @brief Number of producer shard slots reserved at initialization.
    std::uint32_t max_producer_threads{64U};
    /// @brief What to do when a producer finds its shard ring full.
    OverflowPolicy overflow_policy{OverflowPolicy::DropNewest};
    /// @brief Canonical attributes for the file-writer thread (empty CPU set = inherited).
    core::ThreadSpec worker_thread{};
    /// @brief Ceiling of the background writer's idle sleep, in microseconds (default 1 ms).
    std::uint32_t worker_poll_interval_us{1000U};
    /// @brief Forces a synchronous flush when a critical-level record is logged.
    bool sync_flush_on_critical{true};
    /// @brief Anti-spam quotas and suppression policy consumed by the producer hot path.
    SpamProtectionConfig spam_protection{};

    /// @brief Cold-path diagnostic rendering of every option field.
    ///
    /// @return Non-empty text containing each field name and its value, including the
    ///         complete worker thread spec.
    /// @note Project mandate 9 (`to_string` on every public value type); the body lives in
    ///       `logger_options.cpp`.
    [[nodiscard]] std::string to_string() const;
};
/// @brief Free `to_string` entry point for the engine options (mandate 9).
///
/// @param opts Options to render.
/// @return Exactly `opts.to_string()`.
/// @note Project mandate 9; ADL-friendly mirror of the member function.
[[nodiscard]] inline std::string to_string(const LoggerOptions& opts) {
    return opts.to_string();
}

// --- ADL JSON for LoggerOptions (cold path; definition in
//     src/logging/logger_options.cpp) ---
/// @brief ADL JSON writer: serializes every option field, worker spec included.
///
/// @param j Destination JSON object; overwritten with the options' field map.
/// @param opts Options to serialize.
/// @note Project mandate 10 (JSON for every configuration value type).
/// @note Spec `logging/logger-options`, requirement
///       "LoggerOptions defines the engine configuration".
void to_json(nlohmann::json& j, const LoggerOptions& opts);
/// @brief ADL JSON reader that is hostile-input safe.
///
/// Absent or ill-typed fields leave the corresponding member untouched, so the
/// call never throws and a bad document cannot erase a valid configuration.
///
/// @param j Source JSON object.
/// @param out Destination options, updated field by field.
/// @note Project mandate 10; non-throwing under `JSON_NOEXCEPTION=1`.
void from_json(const nlohmann::json& j, LoggerOptions& out) noexcept;

} // namespace nexigen::logging
