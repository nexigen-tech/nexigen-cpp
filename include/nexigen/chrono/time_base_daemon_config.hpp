// nexigen::chrono — time-base daemon configuration (change
// add-time-base-service; spec chrono/time-base "Daemon configuration").
//
// Cold-path data only: the daemon reads it once at start, before it publishes
// anything. It names the suite segment, the publication cadence, the estimator
// window, and every hardware domain the deployment declares. A deployment with
// no declared hardware domain is valid and yields a host-only base.
#pragma once

#include <chrono>
#include <cstdint>
#include <format>
#include <string>
#include <string_view>
#include <vector>

#include <nlohmann/json.hpp>

#include <nexigen/core/result.hpp>
#include <nexigen/core/thread_management.hpp>

#include <nexigen/chrono/device_access_mode.hpp>
#include <nexigen/chrono/sync_mode.hpp>
#include <nexigen/chrono/time_sync_policy.hpp>

namespace nexigen::chrono {

/// @brief One declared hardware timer domain.
///
/// `uuid` is the device identity the firmware reports (a stable UUID, not a PCI
/// address, which changes across enumeration). `access` names the backend the
/// daemon uses: a PTP clock path such as `/dev/ptp2` or a vendor register
/// device. `nominal_hz` is advisory and used only for diagnostics and estimator
/// sanity bounds; zero means unknown.
///
/// @note Spec `chrono/time-base`, requirement "Daemon configuration".
struct HardwareDomainConfig {
    std::string name{};          ///< Logical domain name (diagnostics and history).
    std::string uuid{};          ///< Stable firmware identity.
    std::string access{};        ///< Backend access path (PTP clock or register device).
    std::uint64_t nominal_hz{0}; ///< Advisory counter frequency; zero = unknown.
    DeviceAccessMode mode{DeviceAccessMode::RegisterPair}; ///< Pair acquisition mode.
    /// Use an in-process simulated device instead of a hardware node (tests,
    /// CI, and lab bring-up of a declared domain).
    bool simulate{false};

    /// @brief Render this declaration as a diagnostic string.
    ///
    /// @return Text naming the domain, UUID, access path and nominal rate.
    /// @note Project mandate 9; allocates, cold path only.
    [[nodiscard]] std::string to_string() const;
};

/// @brief Free-helper mirror of `HardwareDomainConfig::to_string`.
///
/// @param domain Declaration to render.
/// @return The same text as the member.
/// @note Project mandate 9.
[[nodiscard]] std::string to_string(const HardwareDomainConfig& domain);

/// @brief Serialize one declared domain as JSON (cold path only).
///
/// @param j Destination JSON value.
/// @param domain Declaration to serialize.
/// @note Project mandate 10.
void to_json(nlohmann::json& j, const HardwareDomainConfig& domain);

/// @brief Parse one declared domain from JSON, strictly non-throwing.
///
/// @param j Source JSON value.
/// @param out Destination declaration, updated in place.
/// @note Project mandate 10.
void from_json(const nlohmann::json& j, HardwareDomainConfig& out) noexcept;

/// @brief Complete daemon configuration.
///
/// @note Spec `chrono/time-base`, requirement "Daemon configuration".
struct TimeBaseDaemonConfig {
    /// POSIX shared-memory name; the suite key is appended by the caller that
    /// knows the run identity, or this value is used verbatim.
    std::string segment_name{"/nexigen-timebase"};

    /// Publication cadence; the heartbeat is refreshed once per interval.
    std::chrono::nanoseconds publish_interval{std::chrono::milliseconds{100}};

    /// Window the estimator keeps for a device domain map.
    std::chrono::nanoseconds fit_window{std::chrono::seconds{10}};

    /// A residual above this bound marks a device domain `Holdover`.
    std::uint64_t max_residual_ns{50'000};

    /// Whether the daemon measures and publishes the host TSC domain.
    bool publish_tsc{true};

    /// Synchronization mode the deployment expects the base to run in.
    SyncMode sync_mode{SyncMode::Standalone};

    /// Time-sync monitor thresholds used on every publication.
    TimeSyncPolicy time_sync{};

    /// How long `PtpSynced` survives without TAI before becoming `Standalone`.
    std::chrono::nanoseconds holdover_window{std::chrono::seconds{10}};

    /// Recorder session root for time-base records; empty disables recording.
    std::string record_session_path{};

    /// History file path; one JSON line per published change.
    std::string history_path{"chrono.jsonl"};

    /// Declared hardware domains; empty means a host-only base.
    std::vector<HardwareDomainConfig> domains{};

    /// Thread specification for the daemon main loop.
    core::ThreadSpec main_thread{};

    /// @brief Render this configuration as a diagnostic string.
    ///
    /// @return Text naming the segment, timings, flags and declared domains.
    /// @note Project mandate 9; allocates, cold path only.
    [[nodiscard]] std::string to_string() const;
};

/// @brief Free-helper mirror of `TimeBaseDaemonConfig::to_string`.
///
/// @param config Configuration to render.
/// @return The same text as the member.
/// @note Project mandate 9.
[[nodiscard]] std::string to_string(const TimeBaseDaemonConfig& config);

/// @brief Serialize the complete configuration as JSON (cold path only).
///
/// @param j Destination JSON value.
/// @param config Configuration to serialize.
/// @note Project mandate 10.
void to_json(nlohmann::json& j, const TimeBaseDaemonConfig& config);

/// @brief Parse the complete configuration from JSON, strictly non-throwing.
///
/// @param j Source JSON value.
/// @param out Destination configuration, each recognized field in place.
/// @note Project mandate 10.
void from_json(const nlohmann::json& j, TimeBaseDaemonConfig& out) noexcept;

/// @brief Read and validate the daemon configuration from an explicit path.
///
/// Missing file returns `NotFound`, malformed JSON or a negative duration
/// returns `InvalidArgument`, and an unreadable file returns `SystemError`. On
/// any failure `out` keeps its previous value (defaults when the caller passed
/// a default-constructed object).
///
/// @param path Explicit configuration file path.
/// @param out Destination configuration.
/// @return Success, `NotFound`, `InvalidArgument`, or `SystemError`.
/// @note Spec `chrono/time-base`, requirement "Daemon configuration"; cold path
///       only, never called from a publish loop.
[[nodiscard]] core::Status load_time_base_daemon_config(std::string_view path,
                                                        TimeBaseDaemonConfig& out) noexcept;

} // namespace nexigen::chrono
