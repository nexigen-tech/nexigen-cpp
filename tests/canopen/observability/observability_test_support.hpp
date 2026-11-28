// Shared harness for the nexigen::canopen::observability test suite.
//
// Zero-dependency style mirroring tests/synchro: a plain NX_CHECK counter,
// run_suite registration, a recording logger, and an armed global allocation
// counter. The metrics fixtures reuse the real provider with a null or
// recording sink.
#pragma once

#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <format>
#include <mutex>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include <nexigen/canopen/observability/can_open_log_policy.hpp>
#include <nexigen/core/log_level.hpp>
#include <nexigen/core/result.hpp>
#include <nexigen/logging/concepts/logger_concept.hpp>
#include <nexigen/logging/detail/format_param.hpp>
#include <nexigen/metrics/metric_provider.hpp>
#include <nexigen/metrics/meter.hpp>
#include <nexigen/metrics/metric_provider_config.hpp>
#include <nexigen/metrics/metric_record.hpp>
#include <nexigen/metrics/metric_snapshot.hpp>
#include <nexigen/metrics/null_metric_sink.hpp>

inline int g_failures = 0;

#define NX_CHECK(condition)                                                                        \
    do {                                                                                           \
        if (!(condition)) {                                                                        \
            ++g_failures;                                                                          \
            std::fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #condition);              \
        }                                                                                          \
    } while (false)

#define NX_CHECK_EQ(actual, expected)                                                              \
    do {                                                                                           \
        const auto nx_actual = (actual);                                                           \
        const auto nx_expected = (expected);                                                       \
        if (!(nx_actual == nx_expected)) {                                                         \
            ++g_failures;                                                                          \
            std::fprintf(stderr, "FAIL %s:%d: %s == %s\n", __FILE__, __LINE__, #actual,            \
                         #expected);                                                               \
        }                                                                                          \
    } while (false)

/// @brief Run one suite body and print its failure delta.
/// @param name Suite label.
/// @param body Suite function.
inline void run_suite(const char* name, void (*body)()) {
    const int before = g_failures;
    body();
    std::printf("[%-28s] %s (%d failed)\n", name, g_failures == before ? "OK" : "FAILED",
                g_failures - before);
}

// --- allocation counting -----------------------------------------------------

/// @brief Start counting global heap allocations.
void allocation_counter_arm() noexcept;
/// @brief Stop counting and return the number observed while armed.
/// @return Allocation count since the matching arm.
std::uint64_t allocation_counter_disarm() noexcept;

// --- recording logger --------------------------------------------------------

/// @brief One captured record.
struct CapturedRecord {
    nexigen::core::LogLevel level{nexigen::core::LogLevel::Info};
    std::string text;
};

/// @brief Captured records of the current process (test-only).
/// @return Mutable record vector.
std::vector<CapturedRecord>& captured_records() noexcept;

/// @brief Drop every captured record.
void clear_captured_records() noexcept;

/// @brief Search captured messages for a substring.
/// @param needle Substring to find.
/// @return `true` when at least one record contains `needle`.
bool captured_contains(std::string_view needle) noexcept;

/// @brief Count captured messages containing a substring.
/// @param needle Substring to count.
/// @return Number of matching records.
std::size_t captured_count(std::string_view needle) noexcept;

/// @brief Mutex serializing record storage across threads.
/// @return Process-wide static mutex.
std::mutex& captured_mutex() noexcept;

/// @brief Number of captured records at a level.
/// @param level Level to count.
/// @return Count of records with the exact level.
std::size_t captured_level_count(nexigen::core::LogLevel level) noexcept;

/// @brief Recording logger that formats immediately (test-only).
class CaptureLogger {
public:
    /// @brief Store the active level.
    /// @param level New level.
    void set_level(nexigen::core::LogLevel level) noexcept { level_ = level; }
    /// @brief Read the active level.
    /// @return Stored level.
    [[nodiscard]] nexigen::core::LogLevel level() const noexcept { return level_; }

    /// @brief Render and store one record.
    /// @tparam Args Format argument types.
    /// @param level Severity.
    /// @param fmt Checked format literal and call site.
    /// @param args Values to render.
    template <class... Args>
    void log(nexigen::core::LogLevel level, nexigen::logging::detail::format_param_t<Args...> fmt,
             Args&&... args) noexcept {
        auto& records = captured_records();
        std::lock_guard<std::mutex> guard{captured_mutex()};
        records.push_back(
            CapturedRecord{level, std::vformat(fmt.fmt.get(), std::make_format_args(args...))});
    }

    template <class... Args>
    void verbose(nexigen::logging::detail::format_param_t<Args...> fmt, Args&&... args) noexcept {
        log(nexigen::core::LogLevel::Verbose, fmt, std::forward<Args>(args)...);
    }
    template <class... Args>
    void debug(nexigen::logging::detail::format_param_t<Args...> fmt, Args&&... args) noexcept {
        log(nexigen::core::LogLevel::Debug, fmt, std::forward<Args>(args)...);
    }
    template <class... Args>
    void info(nexigen::logging::detail::format_param_t<Args...> fmt, Args&&... args) noexcept {
        log(nexigen::core::LogLevel::Info, fmt, std::forward<Args>(args)...);
    }
    template <class... Args>
    void warning(nexigen::logging::detail::format_param_t<Args...> fmt, Args&&... args) noexcept {
        log(nexigen::core::LogLevel::Warning, fmt, std::forward<Args>(args)...);
    }
    template <class... Args>
    void error(nexigen::logging::detail::format_param_t<Args...> fmt, Args&&... args) noexcept {
        log(nexigen::core::LogLevel::Error, fmt, std::forward<Args>(args)...);
    }
    template <class... Args>
    void fatal(nexigen::logging::detail::format_param_t<Args...> fmt, Args&&... args) noexcept {
        log(nexigen::core::LogLevel::Fatal, fmt, std::forward<Args>(args)...);
    }

    /// @brief Succeed without buffering.
    /// @return Empty status.
    [[nodiscard]] nexigen::core::Status flush() noexcept { return {}; }

private:
    nexigen::core::LogLevel level_{nexigen::core::LogLevel::Verbose};
};

static_assert(nexigen::logging::concepts::Logger<CaptureLogger>);

/// @brief Mutex serializing record storage across threads.
/// @return Process-wide static mutex.
std::mutex& captured_mutex() noexcept;

// --- metrics fixtures --------------------------------------------------------

/// @brief One provider, one null sink, and one meter dedicated to one bus.
struct MeterFixture {
    nexigen::metrics::NullMetricSink sink{};
    nexigen::metrics::MetricProviderConfig config{};
    nexigen::metrics::MetricProvider provider{config, sink};
    nexigen::metrics::Meter* meter{nullptr};

    /// @brief Create the provider and bind the `canopen.can0` meter.
    explicit MeterFixture(std::string_view meter_name = "canopen.can0",
                          std::string_view version = "1.0") {
        auto created = provider.get_meter(meter_name, version);
        if (created.has_value()) {
            meter = *created;
        }
    }

    MeterFixture(const MeterFixture&) = delete;
    MeterFixture& operator=(const MeterFixture&) = delete;
};

/// @brief Fixed-buffer sink that keeps the last harvested metric batch.
struct RecordingMetricSink {
    static constexpr std::uint32_t capacity = 64U;                  ///< Record slots.
    std::array<nexigen::metrics::MetricRecord, capacity> records{}; ///< Last batch.
    std::uint32_t count{0U};                                        ///< Valid records.

    /// @brief Copy one collected batch into the fixed buffer.
    /// @param batch Harvested snapshot.
    /// @return Success status, always.
    [[nodiscard]] nexigen::core::Status
    on_event(const nexigen::metrics::MetricBatchSnapshot& batch) noexcept {
        count = 0U;
        if (batch.records == nullptr) {
            return {};
        }
        const std::uint32_t copy_count = std::min(batch.record_count, capacity);
        for (std::uint32_t index = 0U; index < copy_count; ++index) {
            records[index] = batch.records[index];
        }
        count = copy_count;
        return {};
    }

    /// @brief Complete a flush request without buffering.
    /// @return Success status, always.
    [[nodiscard]] nexigen::core::Status flush() noexcept { return {}; }
};

/// @brief Find one harvested record by metric name.
/// @param sink Last harvested batch.
/// @param name Metric name to locate.
/// @return Matching record, or `nullptr`.
inline const nexigen::metrics::MetricRecord* find_metric_record(const RecordingMetricSink& sink,
                                                                std::string_view name) noexcept {
    for (std::uint32_t index = 0U; index < sink.count; ++index) {
        if (nexigen::metrics::detail::view_name(sink.records[index].identity.metric_name) == name) {
            return &sink.records[index];
        }
    }
    return nullptr;
}

/// @brief Dedicated provider, recording sink, and meter for snapshot assertions.
struct RecordingMeterFixture {
    RecordingMetricSink sink{};
    nexigen::metrics::MetricProviderConfig config{};
    nexigen::metrics::MetricProvider provider{config, sink};
    nexigen::metrics::Meter* meter{nullptr};

    /// @brief Create the provider and bind the `canopen.can0` meter.
    explicit RecordingMeterFixture(std::string_view meter_name = "canopen.can0",
                                   std::string_view version = "1.0") {
        auto created = provider.get_meter(meter_name, version);
        if (created.has_value()) {
            meter = *created;
        }
    }

    RecordingMeterFixture(const RecordingMeterFixture&) = delete;
    RecordingMeterFixture& operator=(const RecordingMeterFixture&) = delete;

    /// @brief Harvest the current registry into the recording sink.
    /// @return `true` when `collect_now()` succeeded.
    [[nodiscard]] bool harvest() noexcept { return provider.collect_now().has_value(); }
};

/// @brief Build a policy with the documented defaults.
/// @return A default `CanOpenLogPolicy`.
[[nodiscard]] inline nexigen::canopen::observability::CanOpenLogPolicy default_policy() {
    return nexigen::canopen::observability::CanOpenLogPolicy{};
}

/// @brief Convert milliseconds to the gate's nanosecond clock.
/// @param milliseconds Milliseconds.
/// @return Equivalent nanoseconds.
[[nodiscard]] inline constexpr std::uint64_t ms_to_ns(std::uint64_t milliseconds) noexcept {
    return milliseconds * 1'000'000ULL;
}
