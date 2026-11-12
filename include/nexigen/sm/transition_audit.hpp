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

/// Asynchronous transition journal: one producer (the arbitration thread),
/// one consumer (the flusher thread).
class TransitionAudit {
public:
    /// Open (or append to) the audit log at `path` and start the flusher with
    /// the effective ThreadSpec (empty = inherited attributes).
    explicit TransitionAudit(std::string path, core::ThreadSpec spec = {});

    /// Stop the flusher, drain what is queued, and close the log.
    ~TransitionAudit();

    TransitionAudit(const TransitionAudit&) = delete;
    TransitionAudit& operator=(const TransitionAudit&) = delete;
    TransitionAudit(TransitionAudit&&) = delete;
    TransitionAudit& operator=(TransitionAudit&&) = delete;

    /// Append one transition record in O(1) (non-blocking; drops on overflow).
    void record(const TransitionRecord& record) noexcept;

    /// Number of records written to the log.
    [[nodiscard]] std::uint64_t recorded() const noexcept {
        return recorded_.load(std::memory_order_relaxed);
    }

    /// Number of records dropped because the ring was full.
    [[nodiscard]] std::uint64_t dropped() const noexcept {
        return dropped_.load(std::memory_order_relaxed);
    }

    /// Thread-attribute application result (Ok when the worker started).
    [[nodiscard]] core::Status startup_status() const noexcept {
        const core::ErrorCode error = worker_.start_code();
        return error == core::ErrorCode::Ok ? core::Status{} : core::Status{std::unexpected(error)};
    }

    [[nodiscard]] bool thread_ready() const noexcept { return worker_.ready(); }
    [[nodiscard]] std::uint64_t thread_mask() const noexcept { return worker_.mask(); }
    [[nodiscard]] const core::ThreadApplyReport& thread_report() const noexcept {
        return worker_.report();
    }

    /// Cold-path diagnostic (mandate 9).
    [[nodiscard]] std::string to_string() const {
        return std::format("TransitionAudit(path={}, recorded={}, dropped={})", path_, recorded(),
                           dropped());
    }

private:
    void worker_loop(std::stop_token stop) noexcept;

    struct Body {
        TransitionAudit* self{nullptr};
        void run(std::stop_token stop) noexcept { self->worker_loop(stop); }
    };

    std::string path_;
    std::FILE* file_{nullptr};
    core::SpscRingBuffer<TransitionRecord, 1024> queue_;
    std::atomic<std::uint64_t> recorded_{0};
    std::atomic<std::uint64_t> dropped_{0};
    core::WorkerThread<Body> worker_; ///< LAST member: joins before file_ closes
};

/// Free helper (mandate 9): mirrors the member.
[[nodiscard]] inline std::string to_string(const TransitionAudit& audit) {
    return audit.to_string();
}

} // namespace nexigen::sm

/// The audit log formats as its diagnostic string (mandate 11).
template <> struct std::formatter<nexigen::sm::TransitionAudit> : std::formatter<std::string_view> {
    template <class Ctx> auto format(const nexigen::sm::TransitionAudit& audit, Ctx& ctx) const {
        const std::string text = audit.to_string();
        return std::formatter<std::string_view>::format(text, ctx);
    }
};
