// nexigen::core — one owner for a background thread (change add-worker-thread;
// spec core/worker-thread; design D1–D6).
//
// WorkerThread<Body> owns one std::jthread, applies its own ThreadSpec BEFORE
// any body hook, publishes a readiness snapshot, and joins on stop. The body
// is stored inline (no std::function): `on_start()` is optional and returns
// Status; `run(std::stop_token)` is required and must be noexcept.
//
// LIFETIME CONTRACT: the owning object must outlive the worker, and the
// WorkerThread member MUST be declared LAST — the destructor joins before the
// members captured by the body are destroyed (same rule as the providers it
// replaces).
#pragma once

#include <atomic>
#include <concepts>
#include <cstdint>
#include <format>
#include <optional>
#include <stop_token>
#include <string>
#include <thread>
#include <type_traits>
#include <utility>

#include <nlohmann/json.hpp>

#include <nexigen/core/affinity.hpp>
#include <nexigen/core/result.hpp>
#include <nexigen/core/thread_management.hpp>

namespace nexigen::core {

/// The required body contract: one noexcept run loop taking the stop token.
template <class Body>
concept WorkerBody = requires(Body& body, std::stop_token stop) {
    { body.run(stop) } noexcept -> std::same_as<void>;
};

/// Optional start hook: runs on the worker after the spec is applied and
/// before `run`; the first non-Ok status is published in start_code.
template <class Body>
concept WorkerBodyWithStart = WorkerBody<Body> && requires(Body& body) {
    { body.on_start() } noexcept -> std::same_as<Status>;
};

template <WorkerBody Body> class WorkerThread {
public:
    WorkerThread() noexcept = default;
    ~WorkerThread() { stop(); }

    WorkerThread(const WorkerThread&) = delete;
    WorkerThread& operator=(const WorkerThread&) = delete;
    WorkerThread(WorkerThread&&) = delete;
    WorkerThread& operator=(WorkerThread&&) = delete;

    /// Validate the spec and start the worker. A second start while the
    /// thread is joinable returns Conflict. In Required mode this blocks
    /// until the start sequence is published and returns the attribute error
    /// (joining the worker); BestEffort returns right after thread creation.
    [[nodiscard]] Status start(const ThreadSpec& spec, Body body) noexcept {
        if (thread_.joinable()) {
            return std::unexpected(ErrorCode::Conflict);
        }
        const Status valid = spec.validate();
        if (!valid) {
            return valid;
        }
        start_.ready.store(false, std::memory_order_relaxed);
        start_.code.store(0U, std::memory_order_relaxed);
        start_.mask.store(0U, std::memory_order_relaxed);
        start_.report = ThreadApplyReport{};
        spec_ = spec;
        body_.emplace(std::move(body));
        thread_ = std::jthread([this](std::stop_token stop) { run(std::move(stop)); });
        if (spec_.apply_mode() == ThreadApplyMode::Required) {
            start_.ready.wait(false, std::memory_order_acquire);
            const ErrorCode error = start_.report.first_error();
            if (error != ErrorCode::Ok) {
                stop();
                // The start snapshot (start_code/report) stays readable, but a
                // failed worker is not "ready": preserve the providers'
                // writer_started()/worker_started() contract.
                start_.ready.store(false, std::memory_order_relaxed);
                return std::unexpected(error);
            }
        }
        return {};
    }

    /// Request cooperative stop; the body observes it through its stop token.
    void request_stop() noexcept {
        if (thread_.joinable()) {
            thread_.request_stop();
        }
    }

    /// Join the worker and release the body storage. Safe without a thread.
    void join() noexcept {
        if (thread_.joinable()) {
            thread_.join();
        }
        body_.reset();
    }

    /// Request stop and join.
    void stop() noexcept {
        request_stop();
        join();
    }

    [[nodiscard]] bool joinable() const noexcept { return thread_.joinable(); }
    [[nodiscard]] bool ready() const noexcept {
        return start_.ready.load(std::memory_order_acquire);
    }
    [[nodiscard]] ErrorCode start_code() const noexcept {
        return static_cast<ErrorCode>(start_.code.load(std::memory_order_relaxed));
    }
    [[nodiscard]] const ThreadApplyReport& report() const noexcept {
        static_cast<void>(start_.ready.load(std::memory_order_acquire));
        return start_.report;
    }
    [[nodiscard]] std::uint64_t mask() const noexcept {
        return start_.mask.load(std::memory_order_relaxed);
    }
    [[nodiscard]] const ThreadSpec& spec() const noexcept { return spec_; }

    /// Cold-path diagnostic (mandate 9).
    [[nodiscard]] std::string to_string() const {
        return std::format("WorkerThread(spec={}, ready={}, start_code={}, mask={:#x}, report={})",
                           spec_.to_string(), ready(), core::to_string(start_code()), mask(),
                           report().to_string());
    }

private:
    /// One cache line of start state: report/code/mask are written before the
    /// ready release store and read after the acquire load.
    struct alignas(64) StartCell {
        std::atomic<bool> ready{false};
        std::atomic<std::uint8_t> code{0U};
        std::atomic<std::uint64_t> mask{0U};
        ThreadApplyReport report{};
    };

    void run(std::stop_token stop) noexcept {
        if (!body_.has_value()) {
            return;
        }
        Body& body = *body_;
        const Status attributes = apply_thread_spec_self(spec_, start_.report);
        std::uint8_t first_error = static_cast<std::uint8_t>(start_.report.first_error());
        if (!attributes && spec_.apply_mode() == ThreadApplyMode::Required) {
            publish(first_error, 0U);
            return; // required attribute failure: no on_start and no run
        }
        if constexpr (WorkerBodyWithStart<Body>) {
            const Status started = body.on_start();
            if (!started && first_error == 0U) {
                first_error = static_cast<std::uint8_t>(started.error());
            }
        }
        std::uint64_t mask_now = 0U;
        static_cast<void>(affinity::self_affinity(mask_now));
        publish(first_error, mask_now);
        body.run(stop);
    }

    void publish(std::uint8_t code, std::uint64_t mask_now) noexcept {
        start_.code.store(code, std::memory_order_relaxed);
        start_.mask.store(mask_now, std::memory_order_relaxed);
        start_.ready.store(true, std::memory_order_release);
        start_.ready.notify_one();
    }

    ThreadSpec spec_{};
    std::optional<Body> body_{};
    StartCell start_{};
    std::jthread thread_{}; ///< LAST member: joins before body/spec die
};

/// One-shot spec probe: start an empty-bodied worker, wait for the start
/// sequence, join it, and return the attribute application status. The caller's
/// thread attributes are never modified.
[[nodiscard]] inline Status probe_thread_spec(const ThreadSpec& spec) noexcept {
    struct Body {
        void run(std::stop_token) noexcept {}
    };
    WorkerThread<Body> worker{};
    const Status started = worker.start(spec, Body{});
    if (!started) {
        return started;
    }
    worker.join();
    return {};
}

/// Free helper (mandate 9): mirrors the member.
template <WorkerBody Body>
[[nodiscard]] inline std::string to_string(const WorkerThread<Body>& worker) {
    return worker.to_string();
}

/// Cold-path ADL JSON (mandate 10). A live worker is not reconstructible:
/// there is deliberately no from_json.
template <WorkerBody Body>
inline void to_json(nlohmann::json& j, const WorkerThread<Body>& worker) {
    const ThreadApplyReport& report = worker.report();
    j = nlohmann::json{
        {"spec", worker.spec()},
        {"ready", worker.ready()},
        {"start_code", std::string(core::to_string(worker.start_code()))},
        {"mask", worker.mask()},
        {"report",
         nlohmann::json{
             {"requested", report.requested()},
             {"applied", report.applied()},
             {"failed", report.failed()},
             {"rollback_failed", report.rollback_failed()},
             {"first_error", std::string(core::to_string(report.first_error()))},
         }},
    };
}

} // namespace nexigen::core

/// The worker formats as its diagnostic string (mandate 11).
template <nexigen::core::WorkerBody Body>
struct std::formatter<nexigen::core::WorkerThread<Body>> : std::formatter<std::string_view> {
    template <class Ctx>
    auto format(const nexigen::core::WorkerThread<Body>& worker, Ctx& ctx) const {
        const std::string text = worker.to_string();
        return std::formatter<std::string_view>::format(text, ctx);
    }
};
