// nexigen-logview — asynchronous JSONL/CSV/TXT export.
#pragma once

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <semaphore>
#include <string>
#include <thread>

#include <nexigen/core/result.hpp>
#include <nexigen/core/spsc_ring_buffer.hpp>
#include <nexigen/core/thread_management.hpp>

#include "export_format.hpp"
#include "export_record.hpp"
#include "time_context.hpp"

namespace nexigen::logview {

/// @brief Snapshot of an export job.
struct ExportStats {
    std::uint64_t queued{0};  ///< Records accepted into the queue.
    std::uint64_t written{0}; ///< Records formatted and written.
    std::uint64_t missing{0}; ///< Selected records evicted before queueing.
    std::uint64_t total{0};   ///< Expected record count.
    bool active{false};       ///< Writer still running.
    bool cancelled{false};    ///< User cancelled the job.
    bool failed{false};       ///< The file could not be written.

    /// @brief Completion ratio in `0.0..1.0`.
    /// @return Written divided by total, or `1.0` when total is zero.
    [[nodiscard]] double ratio() const noexcept {
        if (total == 0U) {
            return 1.0;
        }
        return static_cast<double>(written) / static_cast<double>(total);
    }

    /// @brief Cold-path diagnostic rendering.
    /// @return Text with the counters.
    [[nodiscard]] std::string to_string() const;
};

/// @brief Free diagnostic mirror of `ExportStats::to_string`.
/// @param stats Stats to render.
/// @return The same text.
[[nodiscard]] inline std::string to_string(const ExportStats& stats) {
    return stats.to_string();
}

/// @brief Bounded export queue plus one formatting/writing thread.
///
/// The UI thread is the sole producer and never blocks; the writer thread is
/// the sole consumer. Queue capacity is fixed at 4096 records, so an export of
/// any size streams through constant memory.
class ExportWriter {
public:
    /// @brief Queue capacity in records.
    static constexpr std::size_t queue_capacity = 4096;

    /// @brief Construct an idle writer.
    ExportWriter() = default;
    /// @brief Stop and join a running writer.
    ~ExportWriter();

    /// @brief Copying is disabled: the writer owns a thread and queue.
    ExportWriter(const ExportWriter&) = delete;
    /// @brief Copy assignment is disabled.
    ExportWriter& operator=(const ExportWriter&) = delete;

    /// @brief Start a job and open the output file.
    /// @param path Destination file.
    /// @param format Output format.
    /// @param total Expected record count for progress.
    /// @param time Time context used for ISO timestamps.
    /// @return `Status{}` on success.
    [[nodiscard]] nexigen::core::Status start(const std::string& path, ExportFormat format,
                                              std::uint64_t total, TimeContext time,
                                              nexigen::core::ThreadSpec spec = {}) noexcept;

    /// @brief Offer one record to the writer.
    /// @param record Record copy.
    /// @return `true` when queued; `false` when the queue is full or the job is
    ///         inactive.
    bool try_enqueue(const ExportRecord& record) noexcept;

    /// @brief Count a selected record that was evicted before queueing.
    void record_missing() noexcept;

    /// @brief Mark input complete and flush the remaining queue.
    /// @return Final stats after the writer thread has stopped.
    ExportStats finish() noexcept;

    /// @brief Ask the writer to stop after closing the partial file.
    void cancel() noexcept;

    /// @brief Current progress snapshot.
    /// @return Stats read with relaxed atomics.
    [[nodiscard]] ExportStats stats() const noexcept;
    /// @brief Cold-path diagnostic rendering.
    /// @return Text with the current stats.
    [[nodiscard]] std::string to_string() const { return stats().to_string(); }

private:
    /// @brief Writer thread body.
    void run() noexcept;
    /// @brief Format and write one record.
    /// @return `false` on any write failure.
    bool write_record(const ExportRecord& record) noexcept;

    nexigen::core::SpscRingBuffer<ExportRecord, queue_capacity> queue_{}; ///< Bounded hand-off.
    std::binary_semaphore wake_{0};            ///< Producer-to-writer wake-up.
    std::jthread worker_{};                    ///< Writer thread.
    std::atomic<bool> finished_{false};        ///< No more records will be queued.
    std::atomic<bool> cancelled_{false};       ///< User cancellation.
    std::atomic<bool> failed_{false};          ///< Write failure.
    std::atomic<std::uint64_t> written_{0};    ///< Records written.
    std::atomic<std::uint64_t> queued_{0};     ///< Records queued.
    std::atomic<std::uint64_t> missing_{0};    ///< Evicted before queueing.
    std::uint64_t total_{0};                   ///< Expected count.
    ExportFormat format_{ExportFormat::Jsonl}; ///< Selected format.
    TimeContext time_{};                       ///< Timestamp context.
    nexigen::core::ThreadSpec spec_{};         ///< Writer placement request.
    int fd_{-1};                               ///< Open output descriptor.
};

} // namespace nexigen::logview
