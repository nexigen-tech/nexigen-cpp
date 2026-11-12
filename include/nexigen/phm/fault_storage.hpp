// nexigen::phm — non-blocking fault storage (change add-phm; spec
// phm/recovery "Fault storage"; design D5).
//
// Violations are pushed onto a bounded wait-free SPSC queue and drained by a
// dedicated infrastructure thread that appends them to the fault log; the
// monitoring loop never blocks on I/O. When the queue overflows (a
// pathological flood), records are dropped and counted rather than blocking.
#pragma once

#include <atomic>
#include <cstdint>
#include <cstdio>
#include <format>
#include <string>
#include <utility>

#include <nexigen/core/spsc_ring_buffer.hpp>
#include <nexigen/core/worker_thread.hpp>
#include <nexigen/phm/fault_kind.hpp>

namespace nexigen::phm {

/// @brief One recorded violation: kind, timestamp, process index, checkpoint
///        id.
///
/// A plain value type queued by the monitoring thread and written by the
/// storage thread; it owns no resources and is safe to copy.
///
/// @note Spec `phm/recovery`, requirement "Fault storage": each record carries
///       the fault type, timestamp, process id, and checkpoint id.
struct FaultRecord {
    FaultKind kind{FaultKind::AliveFailure}; ///< Supervision domain that failed.
    std::uint32_t process_index{0};          ///< Manifest index of the process.
    std::uint32_t checkpoint_id{0};          ///< Last checkpoint involved, 0 when none.
    std::uint64_t timestamp_ns{0};           ///< Detection time in nanoseconds.

    /// @brief Cold-path diagnostic rendering of the record.
    ///
    /// @return Text naming the kind, process index, checkpoint id, and
    ///         timestamp.
    /// @note Mandate 9 (universal `to_string`); allocates, so cold path only.
    ///       Spec `phm/recovery`, requirement "Fault storage".
    [[nodiscard]] std::string to_string() const {
        return std::format("FaultRecord(kind={}, process={}, checkpoint={}, timestamp_ns={})",
                           nexigen::phm::to_string(kind), process_index, checkpoint_id,
                           timestamp_ns);
    }
};

/// @brief Free helper mirroring `FaultRecord::to_string()` (mandate 9).
///
/// @param record Record to render.
/// @return The record's diagnostic text.
/// @note Mandate 9; allocates, so cold path only.
[[nodiscard]] inline std::string to_string(const FaultRecord& record) {
    return record.to_string();
}

/// @brief Asynchronous fault log with one producer and one consumer.
///
/// Records are pushed onto a bounded, wait-free SPSC queue by the monitoring
/// thread and drained by a dedicated infrastructure thread that appends them to
/// the log file; the monitoring loop never blocks on I/O. When the queue
/// overflows under a pathological flood, records are dropped and counted
/// instead of blocking the producer. The writer applies its own `ThreadSpec`
/// before it starts draining, and the owning object must outlive the thread.
///
/// @note Spec `phm/recovery`, requirement "Fault storage": every violation is
///       written asynchronously on a dedicated infrastructure thread without
///       blocking the monitoring loop. Spec `phm/manifest`, requirement "PHM
///       manifest carries background thread specs", governs the thread spec.
/// @warning `record()` has exactly one producer thread (the monitoring loop);
///          calling it from two threads violates the SPSC contract. The
///          `WorkerThread` member is declared LAST so it joins before the file
///          handle is closed.
class FaultStorage {
public:
    /// @brief Open (or append to) the fault log and start the storage thread.
    ///
    /// The file is opened for appending; if it cannot be opened, records are
    /// still consumed and counted but nothing is persisted.
    ///
    /// @param path Path of the fault log; the parent directory must exist.
    /// @param spec Thread attributes for the storage thread; an empty spec
    ///        preserves every inherited attribute.
    /// @note Spec `phm/recovery`, requirement "Fault storage". Spec
    ///       `phm/manifest`, requirement "PHM manifest carries background
    ///       thread specs" requires the effective spec to be
    ///       `effective_thread_spec(fault_storage_thread, bg_thread)`; this
    ///       constructor applies exactly the spec it receives. Start failures
    ///       are not thrown; query `startup_status()`.
    explicit FaultStorage(std::string path, core::ThreadSpec spec = {});

    /// @brief Request stop, join the storage thread, and close the log.
    ///
    /// The loop drains the batch it has already popped, so only records queued
    /// after the last drain can be discarded at shutdown.
    ///
    /// @note Spec `phm/recovery`, requirement "Fault storage".
    ~FaultStorage();

    /// @brief Copy construction is deleted: the storage owns a thread and file.
    FaultStorage(const FaultStorage&) = delete;

    /// @brief Copy assignment is deleted: the storage owns a thread and file.
    FaultStorage& operator=(const FaultStorage&) = delete;

    /// @brief Move construction is deleted: the worker captures `this`.
    FaultStorage(FaultStorage&&) = delete;

    /// @brief Move assignment is deleted: the worker captures `this`.
    FaultStorage& operator=(FaultStorage&&) = delete;

    /// @brief Queue one fault record without blocking.
    ///
    /// On a full queue the record is discarded and counted in `dropped()`; the
    /// producer is never stalled by storage I/O.
    ///
    /// @param record Record to enqueue; copied into the ring buffer.
    /// @note Spec `phm/recovery`, requirement "Fault storage".
    /// @warning Single producer only: call exclusively from the monitoring
    ///          thread.
    void record(const FaultRecord& record) noexcept;

    /// @brief Number of records consumed by the storage thread.
    ///
    /// @return Monotonic count; records are counted even when the log file
    ///         could not be opened (nothing was persisted in that case).
    /// @note Spec `phm/recovery`, requirement "Fault storage".
    [[nodiscard]] std::uint64_t recorded() const noexcept {
        return recorded_.load(std::memory_order_relaxed);
    }

    /// @brief Number of records dropped because the queue was full.
    ///
    /// @return Monotonic count of overflow drops.
    /// @note Spec `phm/recovery`, requirement "Fault storage".
    [[nodiscard]] std::uint64_t dropped() const noexcept {
        return dropped_.load(std::memory_order_relaxed);
    }

    /// @brief Thread-attribute application result.
    ///
    /// @return Empty `core::Status` when the worker started cleanly, or the
    ///         first `core::ErrorCode` recorded during startup.
    /// @note Spec `phm/manifest`, requirement "PHM manifest carries background
    ///       thread specs". The constructor starts the worker; this accessor
    ///       exposes the recorded start result.
    [[nodiscard]] core::Status startup_status() const noexcept {
        const core::ErrorCode error = worker_.start_code();
        return error == core::ErrorCode::Ok ? core::Status{} : core::Status{std::unexpected(error)};
    }

    /// @brief Whether the storage thread has published its start snapshot.
    ///
    /// @return `true` after the worker applied its spec and published the
    ///         readiness snapshot.
    /// @note Spec `core/worker-thread`, requirement "Start publishes a readable
    ///       readiness snapshot".
    [[nodiscard]] bool thread_ready() const noexcept { return worker_.ready(); }

    /// @brief Post-application affinity of the storage thread.
    ///
    /// @return Affinity mask with cores above 63 dropped by the legacy 64-bit
    ///         diagnostic domain; 0 while unpublished.
    /// @note Spec `core/worker-thread`, requirement "WorkerThread keeps its
    ///       lifetime contract observable".
    [[nodiscard]] std::uint64_t thread_mask() const noexcept { return worker_.mask(); }

    /// @brief Attribute-application report published at startup.
    ///
    /// @return Reference to the immutable startup report; acquire-loads the
    ///         ready flag first, so the report is visible.
    /// @note Spec `core/worker-thread`, requirement "Start publishes a readable
    ///       readiness snapshot".
    /// @warning Cold path only; the reference is valid for the storage's
    ///          lifetime.
    [[nodiscard]] const core::ThreadApplyReport& thread_report() const noexcept {
        return worker_.report();
    }

    /// @brief Cold-path diagnostic naming the log path and counters.
    ///
    /// @return Text of the form
    ///         `FaultStorage(path=<path>, recorded=<n>, dropped=<n>)`.
    /// @note Mandate 9 (universal `to_string`); allocates, so cold path only.
    ///       Spec `phm/recovery`, requirement "Fault storage".
    [[nodiscard]] std::string to_string() const {
        return std::format("FaultStorage(path={}, recorded={}, dropped={})", path_, recorded(),
                           dropped());
    }

private:
    /// @brief Drain the queue until a stop is requested, sleeping while idle.
    ///
    /// @param stop Stop token observed between drain batches.
    /// @note Spec `phm/recovery`, requirement "Fault storage".
    void worker_loop(std::stop_token stop) noexcept;

    /// @brief Worker body forwarding the stop token to `worker_loop`.
    struct Body {
        FaultStorage* self{nullptr}; ///< Owning storage; outlives the thread.

        /// @brief Run the storage loop.
        ///
        /// @param stop Stop token supplied by the worker.
        void run(std::stop_token stop) noexcept { self->worker_loop(stop); }
    };

    std::string path_;                              ///< Log path rendered by `to_string()`.
    std::FILE* file_{nullptr};                      ///< Append handle, null when open failed.
    core::SpscRingBuffer<FaultRecord, 1024> queue_; ///< Bounded producer/consumer queue.
    std::atomic<std::uint64_t> recorded_{0};        ///< Records consumed by the writer.
    std::atomic<std::uint64_t> dropped_{0};         ///< Records dropped on a full queue.
    core::WorkerThread<Body> worker_;               ///< LAST member: joins before file_ closes
};

/// @brief Free helper mirroring `FaultStorage::to_string()` (mandate 9).
///
/// @param storage Storage to render.
/// @return The storage's diagnostic text.
/// @note Mandate 9; allocates, so cold path only.
[[nodiscard]] inline std::string to_string(const FaultStorage& storage) {
    return storage.to_string();
}

} // namespace nexigen::phm

/// @brief Formats a `FaultStorage` as its diagnostic string.
///
/// Delegates to the member `to_string`, so `std::format("{}", storage)` equals
/// the member output.
///
/// @note Mandate 11 (universal `std::formatter`); cold path only. Spec
///       `phm/recovery`, requirement "Fault storage".
template <> struct std::formatter<nexigen::phm::FaultStorage> : std::formatter<std::string_view> {
    /// @brief Write the storage's diagnostic text into the format context.
    ///
    /// @tparam Ctx `std::format` context type, for example
    ///         `std::format_context`.
    /// @param storage Storage to format.
    /// @param ctx Output context supplied by `std::format`.
    /// @return Iterator one past the last written character.
    template <class Ctx> auto format(const nexigen::phm::FaultStorage& storage, Ctx& ctx) const {
        const std::string text = storage.to_string();
        return std::formatter<std::string_view>::format(text, ctx);
    }
};
