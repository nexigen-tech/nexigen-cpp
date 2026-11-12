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

/// @brief Required body contract: one `noexcept` run loop taking a stop token.
///
/// @tparam Body Candidate body type; the constraint requires
///         `body.run(std::stop_token)` to be `noexcept` and return `void`.
/// @note Spec `core/worker-thread`, requirement "Body contract is two-phase and
///       stop-aware".
template <class Body>
concept WorkerBody = requires(Body& body, std::stop_token stop) {
    { body.run(stop) } noexcept -> std::same_as<void>;
};

/// @brief Optional start hook: a `noexcept` `on_start()` returning `Status`.
///
/// Runs on the worker after the spec is applied and before `run`; the first
/// non-`Ok` status is published in `start_code`.
///
/// @tparam Body Candidate body type; constrained by `WorkerBody<Body>` plus a
///         `noexcept` `on_start()` returning `core::Status`.
/// @note Spec `core/worker-thread`, requirement "Body contract is two-phase and
///       stop-aware".
template <class Body>
concept WorkerBodyWithStart = WorkerBody<Body> && requires(Body& body) {
    { body.on_start() } noexcept -> std::same_as<Status>;
};

/// @brief One owner for a background `std::jthread` and its inline body.
///
/// Applies the worker's own `ThreadSpec` before any body hook, publishes a
/// readiness snapshot (`ready`, `start_code`, report, post-pin mask), runs the
/// body, and joins on stop. The body is stored inline: no `std::function`, no
/// virtual dispatch, no per-tick indirection.
///
/// @tparam Body Worker body type constrained by `WorkerBody` (a `noexcept`
///         `run(std::stop_token)`).
/// @note Spec `core/worker-thread`, requirement "WorkerThread owns the thread
///       and body without type erasure".
/// @warning The owning object must outlive the worker, and the `WorkerThread`
///          member must be declared LAST so the destructor joins before the
///          members captured by the body are destroyed.
template <WorkerBody Body> class WorkerThread {
public:
    /// @brief Construct an idle owner with no thread and no body.
    WorkerThread() noexcept = default;
    /// @brief Request stop and join the worker, then release the body storage.
    ~WorkerThread() { stop(); }

    /// @brief Copy construction is deleted: one owner per thread.
    WorkerThread(const WorkerThread&) = delete;
    /// @brief Copy assignment is deleted: one owner per thread.
    WorkerThread& operator=(const WorkerThread&) = delete;
    /// @brief Move construction is deleted: the snapshot cell is address-bound.
    WorkerThread(WorkerThread&&) = delete;
    /// @brief Move assignment is deleted: the snapshot cell is address-bound.
    WorkerThread& operator=(WorkerThread&&) = delete;

    /// @brief Validate the spec and start the worker thread.
    ///
    /// In `ThreadApplyMode::Required` this blocks until the start sequence is
    /// published and returns the attribute error (joining the worker);
    /// `BestEffort` returns right after thread creation.
    ///
    /// @param spec Attributes to apply on the worker before any body hook.
    /// @param body Body moved into inline storage and run on the worker.
    /// @return `Status{}` when the thread started (and, in `Required` mode, every
    ///         requested attribute applied); `Conflict` when a thread is already
    ///         joinable; `InvalidArgument` when `spec.validate()` fails; the
    ///         attribute `ErrorCode` from a failed `Required` start.
    /// @note Spec `core/worker-thread`, requirement "Start publishes a readable
    ///       readiness snapshot".
    /// @warning A successful start is single-use: the worker is not restartable
    ///          until `join()` or `stop()`.
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

    /// @brief Request cooperative stop through the body's stop token.
    ///
    /// Safe when no thread is running: returns without blocking.
    void request_stop() noexcept {
        if (thread_.joinable()) {
            thread_.request_stop();
        }
    }

    /// @brief Join the worker and release the body storage.
    ///
    /// Safe without a thread (before `start` or after a completed join).
    void join() noexcept {
        if (thread_.joinable()) {
            thread_.join();
        }
        body_.reset();
    }

    /// @brief Request stop and join.
    void stop() noexcept {
        request_stop();
        join();
    }

    /// @brief Whether a thread has been started and not yet joined.
    ///
    /// @return `true` while the owned `std::jthread` is joinable.
    [[nodiscard]] bool joinable() const noexcept { return thread_.joinable(); }
    /// @brief Whether the start snapshot has been published.
    ///
    /// @return `true` after `publish()`; loaded with acquire ordering.
    [[nodiscard]] bool ready() const noexcept {
        return start_.ready.load(std::memory_order_acquire);
    }
    /// @brief First error recorded during startup (`Ok` when clean).
    ///
    /// @return The start error code published by the worker; also carries an
    ///         `on_start` failure without suppressing `run`.
    [[nodiscard]] ErrorCode start_code() const noexcept {
        return static_cast<ErrorCode>(start_.code.load(std::memory_order_relaxed));
    }
    /// @brief Attribute-application report published at startup.
    ///
    /// @return Reference to the immutable startup report; acquire-loads the
    ///         ready flag first so the report is visible.
    /// @warning Cold path only; the reference is valid for the worker's
    ///          lifetime.
    [[nodiscard]] const ThreadApplyReport& report() const noexcept {
        static_cast<void>(start_.ready.load(std::memory_order_acquire));
        return start_.report;
    }
    /// @brief Post-application affinity snapshot.
    ///
    /// @return The worker's affinity mask after pinning, with cores above 63
    ///         dropped by the legacy uint64 domain; 0 when unpublished.
    [[nodiscard]] std::uint64_t mask() const noexcept {
        return start_.mask.load(std::memory_order_relaxed);
    }
    /// @brief The immutable spec the worker applied to itself.
    ///
    /// @return Reference to the copy stored at `start` time.
    [[nodiscard]] const ThreadSpec& spec() const noexcept { return spec_; }

    /// @brief Cold-path diagnostic rendering of the worker state.
    ///
    /// @return Text naming the spec, readiness, start code, mask, and report.
    /// @note Spec `core/worker-thread`, requirement "WorkerThread diagnostics
    ///       follow the serialization contract"; mandate 9.
    [[nodiscard]] std::string to_string() const {
        return std::format("WorkerThread(spec={}, ready={}, start_code={}, mask={:#x}, report={})",
                           spec_.to_string(), ready(), core::to_string(start_code()), mask(),
                           report().to_string());
    }

private:
    /// @brief One cache line of start state.
    ///
    /// `report`, `code`, and `mask` are written before the `ready` release store
    /// and read only after a successful acquire load.
    struct alignas(64) StartCell {
        std::atomic<bool> ready{false};      ///< Release/acquire readiness latch.
        std::atomic<std::uint8_t> code{0U};  ///< First startup error, 0 = Ok.
        std::atomic<std::uint64_t> mask{0U}; ///< Post-pin affinity snapshot.
        ThreadApplyReport report{};          ///< Attribute-application report.
    };

    /// @brief Thread entry point: apply the spec, run hooks, then `run`.
    ///
    /// @param stop Stop token handed to the body's `run`.
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

    /// @brief Publish the start snapshot with release/acquire ordering.
    ///
    /// @param code First startup error, `0` for `Ok`.
    /// @param mask_now Post-pin affinity snapshot.
    void publish(std::uint8_t code, std::uint64_t mask_now) noexcept {
        start_.code.store(code, std::memory_order_relaxed);
        start_.mask.store(mask_now, std::memory_order_relaxed);
        start_.ready.store(true, std::memory_order_release);
        start_.ready.notify_one();
    }

    ThreadSpec spec_{};          ///< Immutable spec copy applied by the worker.
    std::optional<Body> body_{}; ///< Inline body storage; reset on join.
    StartCell start_{};          ///< Readiness snapshot shared with the caller.
    std::jthread thread_{};      ///< LAST member: joins before body/spec die
};

/// @brief One-shot spec probe on a throwaway worker thread.
///
/// Starts a `WorkerThread` with an empty body, waits for the start sequence,
/// joins it, and returns the attribute application status. The caller's thread
/// attributes are never modified.
///
/// @param spec Attributes to validate and apply on the probe thread.
/// @return `Status{}` when the start sequence succeeded (including a
///         `BestEffort` partial application); the `Required` failure error
///         otherwise.
/// @note Spec `core/worker-thread`, requirement "probe_thread_spec checks a spec
///       on a throwaway thread".
[[nodiscard]] inline Status probe_thread_spec(const ThreadSpec& spec) noexcept {
    /// @brief Empty probe body: the probe only needs the start sequence.
    struct Body {
        /// @brief Return immediately; the stop token is unused.
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

/// @brief Free mirror of the member `to_string` (mandate 9).
///
/// @tparam Body Worker body type constrained by `WorkerBody`.
/// @param worker Worker to render.
/// @return The same text as `worker.to_string()`.
/// @note Spec `core/worker-thread`, requirement "WorkerThread diagnostics follow
///       the serialization contract".
template <WorkerBody Body>
[[nodiscard]] inline std::string to_string(const WorkerThread<Body>& worker) {
    return worker.to_string();
}

/// @brief Cold-path ADL JSON dump of the worker state.
///
/// A live worker is not reconstructible: there is deliberately no `from_json`.
///
/// @tparam Body Worker body type constrained by `WorkerBody`.
/// @param j JSON value overwritten with spec/ready/start_code/mask/report.
/// @param worker Worker to serialize.
/// @note Spec `core/worker-thread`, requirement "WorkerThread diagnostics follow
///       the serialization contract"; mandate 10.
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

/// @brief The worker formats as its diagnostic string (mandate 11).
///
/// @tparam Body Worker body type constrained by `nexigen::core::WorkerBody`.
/// @note Spec `core/worker-thread`, requirement "WorkerThread diagnostics follow
///       the serialization contract".
template <nexigen::core::WorkerBody Body>
struct std::formatter<nexigen::core::WorkerThread<Body>> : std::formatter<std::string_view> {
    /// @brief Format the worker as `worker.to_string()`.
    ///
    /// @tparam Ctx Format context type, for example `std::format_context`.
    /// @param worker Worker to render.
    /// @param ctx Format context receiving the output.
    /// @return Iterator past the last character written.
    template <class Ctx>
    auto format(const nexigen::core::WorkerThread<Body>& worker, Ctx& ctx) const {
        const std::string text = worker.to_string();
        return std::formatter<std::string_view>::format(text, ctx);
    }
};
