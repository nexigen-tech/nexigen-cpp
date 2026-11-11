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
#include <thread>
#include <utility>

#include <nexigen/core/spsc_ring_buffer.hpp>
#include <nexigen/phm/fault_kind.hpp>

namespace nexigen::phm {

/// One recorded violation: kind, timestamp, process index, checkpoint id.
struct FaultRecord {
    FaultKind kind{FaultKind::AliveFailure};
    std::uint32_t process_index{0};
    std::uint32_t checkpoint_id{0};
    std::uint64_t timestamp_ns{0};

    /// Cold-path diagnostic (mandate 9).
    [[nodiscard]] std::string to_string() const {
        return std::format("FaultRecord(kind={}, process={}, checkpoint={}, timestamp_ns={})",
                           nexigen::phm::to_string(kind), process_index, checkpoint_id,
                           timestamp_ns);
    }
};

/// Free helper (mandate 9): mirrors the member.
[[nodiscard]] inline std::string to_string(const FaultRecord& record) {
    return record.to_string();
}

/// Asynchronous fault log: one producer (the monitoring thread), one consumer
/// (the storage thread).
class FaultStorage {
public:
    /// Open (or append to) the fault log at `path` and start the storage
    /// thread.
    explicit FaultStorage(std::string path);

    /// Stop the storage thread, flush what is queued, and close the log.
    ~FaultStorage();

    FaultStorage(const FaultStorage&) = delete;
    FaultStorage& operator=(const FaultStorage&) = delete;
    FaultStorage(FaultStorage&&) = delete;
    FaultStorage& operator=(FaultStorage&&) = delete;

    /// Queue one fault record (non-blocking; drops on overflow).
    void record(const FaultRecord& record) noexcept;

    /// Number of records written to the log.
    [[nodiscard]] std::uint64_t recorded() const noexcept {
        return recorded_.load(std::memory_order_relaxed);
    }

    /// Number of records dropped because the queue was full.
    [[nodiscard]] std::uint64_t dropped() const noexcept {
        return dropped_.load(std::memory_order_relaxed);
    }

    /// Cold-path diagnostic (mandate 9).
    [[nodiscard]] std::string to_string() const {
        return std::format("FaultStorage(path={}, recorded={}, dropped={})", path_, recorded(),
                           dropped());
    }

private:
    void worker_loop() noexcept;

    std::string path_;
    std::FILE* file_{nullptr};
    core::SpscRingBuffer<FaultRecord, 1024> queue_;
    std::atomic<bool> shutdown_{false};
    std::atomic<std::uint64_t> recorded_{0};
    std::atomic<std::uint64_t> dropped_{0};
    std::thread worker_;
};

/// Free helper (mandate 9): mirrors the member.
[[nodiscard]] inline std::string to_string(const FaultStorage& storage) {
    return storage.to_string();
}

} // namespace nexigen::phm

/// The storage formats as its diagnostic string (mandate 11).
template <> struct std::formatter<nexigen::phm::FaultStorage> : std::formatter<std::string_view> {
    template <class Ctx> auto format(const nexigen::phm::FaultStorage& storage, Ctx& ctx) const {
        const std::string text = storage.to_string();
        return std::formatter<std::string_view>::format(text, ctx);
    }
};
