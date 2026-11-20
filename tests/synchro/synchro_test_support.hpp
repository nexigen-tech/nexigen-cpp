// Shared harness for the nexigen::synchro test suite.
//
// Zero-dependency style, mirroring tests/com/event_system_test.cpp: a plain
// NX_CHECK counter, run_suite registration, a recording logger, and an armed
// global allocation counter.
#pragma once

#include <atomic>
#include <algorithm>
#include <array>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <format>
#include <mutex>
#include <string>
#include <string_view>
#include <thread>
#include <utility>
#include <vector>

#include <unistd.h>

#include <nexigen/com/event_channel.hpp>
#include <nexigen/com/event_header.hpp>
#include <nexigen/com/loss_info.hpp>
#include <nexigen/core/log_level.hpp>
#include <nexigen/core/result.hpp>
#include <nexigen/logging/concepts/logger_concept.hpp>
#include <nexigen/logging/detail/format_param.hpp>
#include <nexigen/metrics/metric_provider.hpp>
#include <nexigen/metrics/metric_record.hpp>
#include <nexigen/metrics/metric_snapshot.hpp>
#include <nexigen/metrics/meter.hpp>
#include <nexigen/metrics/null_metric_sink.hpp>
#include <nexigen/synchro/sync_channel_config.hpp>
#include <nexigen/synchro/sync_config.hpp>
#include <nexigen/synchro/sync_consumer_config.hpp>
#include <nexigen/synchro/sync_payload.hpp>

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
    std::printf("[%-26s] %s (%d failed)\n", name, g_failures == before ? "OK" : "FAILED",
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

/// @brief Mutex serializing record storage across threads.
/// @return Process-wide static mutex.
std::mutex& captured_mutex() noexcept;

/// @brief Search captured messages for a substring.
/// @param needle Substring to find.
/// @return `true` when at least one record contains `needle`.
bool captured_contains(std::string_view needle) noexcept;

/// @brief Count captured messages containing a substring.
/// @param needle Substring to count.
/// @return Number of matching records.
std::size_t captured_count(std::string_view needle) noexcept;

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

/// @brief Build a process-unique service name for one test channel.
/// @param tag Short ASCII tag.
/// @return Valid service name string.
inline std::string unique_service(const char* tag) {
    static std::atomic<std::uint64_t> counter{0};
    return std::format("Nexigen/SynchroTest/{}/{}/{}", tag, ::getpid(),
                       counter.fetch_add(1U, std::memory_order_relaxed));
}

// --- metrics fixture ---------------------------------------------------------

/// @brief One provider, one null sink, and one meter dedicated to one endpoint.
///
/// A `MetricProvider` owns its registry; each endpoint needs its own provider
/// (and therefore its own meter) because the synchro metric names are fixed and
/// duplicate registration fails.
struct MeterFixture {
    nexigen::metrics::NullMetricSink sink{};
    nexigen::metrics::MetricProvider provider{nexigen::metrics::MetricProviderConfig{}, sink};
    nexigen::metrics::Meter* meter{nullptr};

    /// @brief Create the provider and bind the first meter.
    MeterFixture() {
        auto created = provider.get_meter("synchro_test", "1.0");
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

/// @brief Dedicated provider, recording sink, and meter for value assertions.
struct RecordingMeterFixture {
    RecordingMetricSink sink{};
    nexigen::metrics::MetricProvider provider{nexigen::metrics::MetricProviderConfig{}, sink};
    nexigen::metrics::Meter* meter{nullptr};

    /// @brief Create the provider and bind the first meter.
    RecordingMeterFixture() {
        auto created = provider.get_meter("synchro_recording", "1.0");
        if (created.has_value()) {
            meter = *created;
        }
    }

    RecordingMeterFixture(const RecordingMeterFixture&) = delete;
    RecordingMeterFixture& operator=(const RecordingMeterFixture&) = delete;

    /// @brief Harvest and read one integer gauge.
    /// @param name Metric name.
    /// @param out Receives the gauge value.
    /// @return `true` when the metric was harvested as a gauge.
    [[nodiscard]] bool gauge_value(std::string_view name, std::int64_t& out) noexcept {
        if (!provider.collect_now().has_value()) {
            return false;
        }
        const auto* record = find_metric_record(sink, name);
        if (record == nullptr || record->kind != nexigen::metrics::MetricKind::Gauge) {
            return false;
        }
        out = record->scalar.gauge_int;
        return true;
    }

    /// @brief Harvest and read one counter.
    /// @param name Metric name.
    /// @param out Receives the counter value.
    /// @return `true` when the metric was harvested as a counter.
    [[nodiscard]] bool counter_value(std::string_view name, std::uint64_t& out) noexcept {
        if (!provider.collect_now().has_value()) {
            return false;
        }
        const auto* record = find_metric_record(sink, name);
        if (record == nullptr || record->kind != nexigen::metrics::MetricKind::Counter) {
            return false;
        }
        out = record->scalar.counter;
        return true;
    }
};

// --- configuration helpers ---------------------------------------------------

/// @brief Build a channel configuration around one unique service name.
/// @param service Service name string.
/// @param cycle_period_ns Cycle period.
/// @param phases_per_cycle Phase count.
/// @return Valid channel configuration.
inline nexigen::synchro::SyncChannelConfig make_channel(const std::string& service,
                                                        std::uint64_t cycle_period_ns = 10'000'000U,
                                                        std::uint32_t phases_per_cycle = 10U) {
    nexigen::synchro::SyncChannelConfig channel{};
    channel.service_name = service;
    channel.cycle_period_ns = cycle_period_ns;
    channel.phases_per_cycle = phases_per_cycle;
    return channel;
}

/// @brief Build a valid generator configuration.
/// @param service Service name string.
/// @param cycle_period_ns Cycle period.
/// @param phases_per_cycle Phase count.
/// @return Valid generator configuration.
inline nexigen::synchro::SyncConfig
make_generator_config(const std::string& service, std::uint64_t cycle_period_ns = 10'000'000U,
                      std::uint32_t phases_per_cycle = 10U) {
    nexigen::synchro::SyncConfig config{};
    config.channel = make_channel(service, cycle_period_ns, phases_per_cycle);
    config.spin_threshold_ns = 100'000U;
    return config;
}

/// @brief Build a valid consumer configuration.
/// @param service Service name string.
/// @param cycle_period_ns Cycle period.
/// @param phases_per_cycle Phase count.
/// @return Valid consumer configuration.
inline nexigen::synchro::SyncConsumerConfig
make_consumer_config(const std::string& service, std::uint64_t cycle_period_ns = 10'000'000U,
                     std::uint32_t phases_per_cycle = 10U) {
    nexigen::synchro::SyncConsumerConfig config{};
    config.channel = make_channel(service, cycle_period_ns, phases_per_cycle);
    config.max_allowed_latency_ns = 0U;
    config.sync_loss_timeout_ns = 0U;
    config.initial_sync_timeout_ns = 0U;
    return config;
}

// --- transport collectors ----------------------------------------------------

/// @brief Counters filled by a raw `com::EventConsumer` callback.
struct CollectedEvents {
    std::uint64_t events{0};
    std::uint64_t losses{0};
    std::uint64_t first_sequence{0};
    std::uint64_t last_sequence{0};
    std::uint64_t last_epoch{0};
    std::uint64_t losses_lost_count{0};
    std::uint64_t epoch_changes{0};
    std::uint64_t first_sequence_of_new_epoch{0};
    std::uint64_t first_cycle_of_new_epoch{0};
    std::uint64_t last_absolute{0};
    bool has_previous{false};
    bool monotonic{true};
    nexigen::com::LossReason last_loss{nexigen::com::LossReason::TransportGap};
    nexigen::synchro::SyncPayload last_payload{};

    /// @brief Forget the continuity position but keep cumulative counters.
    void reset_positions() noexcept {
        has_previous = false;
        last_absolute = 0U;
        monotonic = true;
    }
};

/// @brief Event callback bound to one `CollectedEvents`.
struct CollectingEventCallback {
    CollectedEvents* out{nullptr};

    void operator()(const nexigen::com::EventHeader& header,
                    const nexigen::synchro::SyncPayload& payload) noexcept {
        const std::uint64_t absolute =
            payload.cycle_number * payload.phases_total + payload.phase_index;
        if (out->events == 0U) {
            out->first_sequence = header.sequence_id;
        } else if (header.producer_epoch != out->last_epoch) {
            ++out->epoch_changes;
            out->first_sequence_of_new_epoch = header.sequence_id;
            out->first_cycle_of_new_epoch = payload.cycle_number;
            out->reset_positions();
        } else if (out->has_previous && absolute != out->last_absolute + 1U) {
            out->monotonic = false;
        }
        ++out->events;
        out->last_sequence = header.sequence_id;
        out->last_epoch = header.producer_epoch;
        out->last_absolute = absolute;
        out->has_previous = true;
        out->last_payload = payload;
    }
};

/// @brief Loss callback bound to one `CollectedEvents`.
struct CollectingLossCallback {
    CollectedEvents* out{nullptr};

    void operator()(const nexigen::com::LossInfo& info) noexcept {
        ++out->losses;
        out->last_loss = info.reason;
        out->losses_lost_count += info.lost_count;
    }
};

/// @brief Concrete raw transport consumer type used by the generator tests.
using RawSyncConsumer =
    nexigen::com::EventConsumer<nexigen::synchro::SyncPayload, CollectingEventCallback,
                                CollectingLossCallback>;

// --- wait helper -------------------------------------------------------------

/// @brief Poll a predicate until it holds or the budget expires.
/// @tparam Predicate Callable returning `bool`.
/// @param predicate Condition to observe.
/// @param budget Maximum wall time to keep polling.
/// @return `true` when the predicate held; the final sample otherwise.
template <class Predicate>
bool wait_for(Predicate predicate,
              std::chrono::milliseconds budget = std::chrono::milliseconds{2'000}) {
    const auto deadline = std::chrono::steady_clock::now() + budget;
    while (std::chrono::steady_clock::now() < deadline) {
        if (predicate()) {
            return true;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds{1});
    }
    return predicate();
}

/// @brief Drive a consumer set until a predicate holds or the budget expires.
///
/// Each iteration performs one bounded wait, so the set drains its consumer and
/// every callback runs on the calling thread.
/// @tparam Set `com::EventConsumerSet`-like type with `wait_for`.
/// @tparam Predicate Callable returning `bool`.
/// @param set Consumer set to pump.
/// @param predicate Condition to observe between waits.
/// @param budget Maximum wall time to keep pumping.
/// @return `true` when the predicate held; the final sample otherwise.
template <class Set, class Predicate>
bool pump_until(Set& set, Predicate predicate,
                std::chrono::milliseconds budget = std::chrono::milliseconds{2'000}) {
    const auto deadline = std::chrono::steady_clock::now() + budget;
    while (std::chrono::steady_clock::now() < deadline) {
        if (predicate()) {
            return true;
        }
        static_cast<void>(set.wait_for(std::chrono::milliseconds{10}));
    }
    return predicate();
}
