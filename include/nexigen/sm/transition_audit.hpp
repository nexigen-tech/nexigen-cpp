// nexigen::sm — non-blocking transition audit log (change add-sm; spec
// sm/telemetry "Transition audit log"; design D7).
//
// The arbitration thread appends transition records to a pre-allocated
// lock-free SPSC ring in O(1) without I/O or mutexes; a low-priority
// background thread formats and flushes them to the audit log. On overflow
// records are dropped and counted rather than blocking.
#pragma once

#include <atomic>
#include <cstdint>
#include <cstdio>
#include <format>
#include <string>
#include <utility>

#include <nexigen/core/spsc_ring_buffer.hpp>
#include <nexigen/core/worker_thread.hpp>
#include <nexigen/sm/transition_record.hpp>

namespace nexigen::sm {

/// @brief Asynchronous transition journal: one producer, one consumer.
///
/// The arbitration thread calls `record()` from its hot path; a background
/// `WorkerThread` drains a fixed 1,024-slot SPSC ring and writes one formatted
/// line per record. The producer never blocks: a full ring drops the record and
/// increments `dropped()`. The flusher applies the thread spec it was
/// constructed with (empty means inherited attributes), which the SM resolves
/// with `core::effective_thread_spec(audit_thread, bg_thread)`.
///
/// The type is non-copyable and non-movable: it owns a file handle and the
/// worker thread. Declare it after the members its worker touches; the worker
/// is deliberately the last member so it joins before the file closes.
///
/// @note Spec `sm/telemetry`, requirement "Transition audit log".
/// @note Spec `sm/manifest`, requirement "SM manifest carries background
///       thread specs".
class TransitionAudit {
public:
    /// @brief Open the audit log and start the flusher.
    ///
    /// The file is opened in append mode. A path that cannot be opened leaves
    /// the log unavailable while records are still counted; the worker start
    /// outcome is exposed through `startup_status()`.
    ///
    /// @param path Audit-log path to append to.
    /// @param spec Effective thread spec for the flusher; an empty default
    ///        preserves the inherited attributes.
    /// @note Spec `sm/telemetry`, requirement "Transition audit log".
    /// @note Spec `core/thread-management`, requirement "Spec emptiness and
    ///       fallback are explicit".
    explicit TransitionAudit(std::string path, core::ThreadSpec spec = {});

    /// @brief Stop the flusher, join it, and close the log.
    ///
    /// The worker is stopped and joined before the file handle closes, so the
    /// consumer never touches a closed file. Records still queued after the
    /// stop request may remain unwritten.
    ~TransitionAudit();

    /// @brief Deleted: the audit owns a file handle and a worker thread.
    TransitionAudit(const TransitionAudit&) = delete;

    /// @brief Deleted: the audit owns a file handle and a worker thread.
    TransitionAudit& operator=(const TransitionAudit&) = delete;

    /// @brief Deleted: the worker body points at `this`.
    TransitionAudit(TransitionAudit&&) = delete;

    /// @brief Deleted: the worker body points at `this`.
    TransitionAudit& operator=(TransitionAudit&&) = delete;

    /// @brief Append one transition record in O(1), never blocking.
    ///
    /// A push into the SPSC ring is lock-free and allocation-free. When the
    /// ring is full the record is dropped and `dropped()` increases instead of
    /// waiting for the consumer.
    ///
    /// @param record Record to enqueue; copied into the ring.
    /// @note Spec `sm/telemetry`, requirement "Transition audit log"
    ///       (transitions are journaled without blocking).
    void record(const TransitionRecord& record) noexcept;

    /// @brief Number of records consumed by the flusher.
    ///
    /// @return Relaxed load of the flusher counter. Records are counted when
    ///         drained even if the log file could not be opened.
    [[nodiscard]] std::uint64_t recorded() const noexcept {
        return recorded_.load(std::memory_order_relaxed);
    }

    /// @brief Number of records dropped because the ring was full.
    ///
    /// @return Relaxed load of the drop counter.
    [[nodiscard]] std::uint64_t dropped() const noexcept {
        return dropped_.load(std::memory_order_relaxed);
    }

    /// @brief Thread-attribute application result.
    ///
    /// @return Empty `core::Status` when the worker started successfully, or
    ///         the first attribute/start error otherwise.
    /// @note Spec `core/worker-thread`, requirement "Start publishes a
    ///       readable readiness snapshot".
    [[nodiscard]] core::Status startup_status() const noexcept {
        const core::ErrorCode error = worker_.start_code();
        return error == core::ErrorCode::Ok ? core::Status{} : core::Status{std::unexpected(error)};
    }

    /// @brief True once the worker published its start snapshot.
    ///
    /// @return The worker's readiness flag (acquire-ordered).
    [[nodiscard]] bool thread_ready() const noexcept { return worker_.ready(); }

    /// @brief Diagnostic affinity mask of the flusher thread.
    ///
    /// @return Legacy 64-bit mask snapshot; cores above 63 are dropped.
    [[nodiscard]] std::uint64_t thread_mask() const noexcept { return worker_.mask(); }

    /// @brief The flusher's thread-attribute application report.
    ///
    /// @return Const reference to the worker-owned startup snapshot.
    [[nodiscard]] const core::ThreadApplyReport& thread_report() const noexcept {
        return worker_.report();
    }

    /// @brief Render the audit state as a diagnostic string.
    ///
    /// @return Text naming the path, recorded count, and dropped count;
    ///         allocates.
    /// @note Spec `sm/telemetry`, requirement "Telemetry diagnostics".
    /// @note Cold-path diagnostic only (project mandate 9).
    [[nodiscard]] std::string to_string() const {
        return std::format("TransitionAudit(path={}, recorded={}, dropped={})", path_, recorded(),
                           dropped());
    }

private:
    /// @brief Drain the ring and append formatted lines until stopped.
    ///
    /// Idles for 5 ms when the ring is empty; the loop is the only consumer, so
    /// it never takes a lock.
    ///
    /// @param stop Stop token requested by the destructor.
    void worker_loop(std::stop_token stop) noexcept;

    /// @brief Worker body adapter forwarding the stop token to the owner.
    struct Body {
        TransitionAudit* self{nullptr}; ///< Owning audit instance.

        /// @brief Forward one loop iteration to `worker_loop`.
        ///
        /// @param stop Stop token supplied by the worker runtime.
        void run(std::stop_token stop) noexcept { self->worker_loop(stop); }
    };

    std::string path_;                                   ///< Audit-log path.
    std::FILE* file_{nullptr};                           ///< Append handle; null when open failed.
    core::SpscRingBuffer<TransitionRecord, 1024> queue_; ///< Fixed-capacity SPSC ring.
    std::atomic<std::uint64_t> recorded_{0};             ///< Records drained by the flusher.
    std::atomic<std::uint64_t> dropped_{0};              ///< Records dropped on a full ring.
    core::WorkerThread<Body> worker_;                    ///< LAST member: joins before file_ closes
};

/// @brief Free diagnostic helper mirroring `TransitionAudit::to_string`.
///
/// @param audit Audit to render.
/// @return The member `to_string()` output.
/// @note Spec `sm/telemetry`, requirement "Telemetry diagnostics".
/// @note Cold-path diagnostic only (project mandate 9).
[[nodiscard]] inline std::string to_string(const TransitionAudit& audit) {
    return audit.to_string();
}

} // namespace nexigen::sm

/// @brief `std::formatter` for `TransitionAudit` (project mandate 11).
///
/// Delegates to `std::formatter<std::string_view>` after rendering the member
/// diagnostic, so `std::format("{}", audit)` equals `to_string()`.
///
/// @note Spec `sm/telemetry`, requirement "Telemetry diagnostics".
template <> struct std::formatter<nexigen::sm::TransitionAudit> : std::formatter<std::string_view> {
    /// @brief Format the audit as its diagnostic string.
    ///
    /// @tparam Ctx Format context type supplied by `std::format`.
    /// @param audit Audit to render.
    /// @param ctx Output iterator context receiving the text.
    /// @return Iterator past the written characters.
    template <class Ctx> auto format(const nexigen::sm::TransitionAudit& audit, Ctx& ctx) const {
        const std::string text = audit.to_string();
        return std::formatter<std::string_view>::format(text, ctx);
    }
};
