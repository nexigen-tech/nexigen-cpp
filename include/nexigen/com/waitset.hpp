// nexigen::com — multiplexed kernel-level wait set (change add-com-waitset;
// spec com/waitset "Multiplexed blocking wait").
//
// Thin wrapper over iox2::WaitSet<Ipc>: attaches event listeners (optionally
// with a deadline or a periodic interval), blocks in the kernel until one of
// them fires, and dispatches the fired source to a callback whose signature
// uses only nexigen::com types (design D2) — no busy-polling, no iox2:: leak.
#pragma once

#include <chrono>
#include <cstdint>
#include <format>
#include <string>
#include <utility>

#include <iox2/iceoryx2.hpp>

#include <nexigen/com/callback_progression.hpp>
#include <nexigen/com/detail/error_mapping.hpp>
#include <nexigen/com/error.hpp>
#include <nexigen/com/event.hpp>
#include <nexigen/com/wait_set_attachment_id.hpp>
#include <nexigen/com/wait_set_guard.hpp>

namespace nexigen::com {

/// Multiplexes several attachments and blocks until one fires.
class WaitSet {
public:
    /// Create a wait set on the local IPC transport.
    [[nodiscard]] static Result<WaitSet> create() noexcept {
        auto created = iox2::WaitSetBuilder().create<iox2::ServiceType::Ipc>();
        if (!created) {
            return std::unexpected(detail::map_error(created.error()));
        }
        return WaitSet{std::move(*created)};
    }

    /// Create a wait set whose underlying transport does NOT install its own
    /// SIGINT/SIGTERM handlers (iceoryx2 SignalHandlingMode::Disabled): the
    /// caller installs and owns signal handling — needed by the PHM daemon,
    /// which must stop its loop on its own terms.
    [[nodiscard]] static Result<WaitSet> create_without_signal_handling() noexcept {
        auto created = iox2::WaitSetBuilder()
                           .signal_handling_mode(iox2::SignalHandlingMode::Disabled)
                           .create<iox2::ServiceType::Ipc>();
        if (!created) {
            return std::unexpected(detail::map_error(created.error()));
        }
        return WaitSet{std::move(*created)};
    }

    WaitSet(const WaitSet&) = delete;
    WaitSet& operator=(const WaitSet&) = delete;
    WaitSet(WaitSet&&) noexcept = default;
    WaitSet& operator=(WaitSet&&) noexcept = default;

    /// Attach an event listener; it fires when the paired notifier emits.
    [[nodiscard]] Result<WaitSetGuard> attach_notification(const EventListener& listener) noexcept {
        auto guard = inner_.attach_notification(listener.handle());
        if (!guard) {
            return std::unexpected(detail::map_error(guard.error()));
        }
        return WaitSetGuard{std::move(*guard)};
    }

    /// Attach an event listener plus a deadline; it fires on the event or
    /// when the deadline elapses (distinguished via has_event_from /
    /// has_missed_deadline). The deadline resets on every event.
    [[nodiscard]] Result<WaitSetGuard> attach_deadline(const EventListener& listener,
                                                       std::chrono::milliseconds deadline) noexcept {
        auto guard = inner_.attach_deadline(
            listener.handle(), iox2::bb::Duration::from_millis(deadline.count()));
        if (!guard) {
            return std::unexpected(detail::map_error(guard.error()));
        }
        return WaitSetGuard{std::move(*guard)};
    }

    /// Attach a periodic interval; it fires every `interval`.
    [[nodiscard]] Result<WaitSetGuard> attach_interval(std::chrono::milliseconds interval) noexcept {
        auto guard = inner_.attach_interval(iox2::bb::Duration::from_millis(interval.count()));
        if (!guard) {
            return std::unexpected(detail::map_error(guard.error()));
        }
        return WaitSetGuard{std::move(*guard)};
    }

    /// Block until a source fires and process it; the callback returns
    /// CallbackProgression::Continue to handle the next source or ::Stop to
    /// return. The callback runs synchronously on the calling thread.
    template <class Callback>
    [[nodiscard]] Status wait_and_process(Callback&& callback) noexcept {
        auto result = inner_.wait_and_process(make_adapter(callback));
        return to_status(result);
    }

    /// Block until one source fires and process a single callback invocation.
    template <class Callback>
    [[nodiscard]] Status wait_and_process_once(Callback&& callback) noexcept {
        auto result = inner_.wait_and_process_once(make_adapter(callback));
        return to_status(result);
    }

    /// Block until one source fires or `timeout` elapses; a timeout on an
    /// empty wait set simply returns (no spin).
    template <class Callback>
    [[nodiscard]] Status wait_and_process_once_with_timeout(
        Callback&& callback, std::chrono::milliseconds timeout) noexcept {
        auto result = inner_.wait_and_process_once_with_timeout(
            make_adapter(callback), iox2::bb::Duration::from_millis(timeout.count()));
        return to_status(result);
    }

    /// Maximum number of attachments this wait set can hold.
    [[nodiscard]] std::uint64_t capacity() const noexcept { return inner_.capacity(); }

    /// Current number of attachments.
    [[nodiscard]] std::uint64_t len() const noexcept { return inner_.len(); }

    /// True when nothing is attached.
    [[nodiscard]] bool is_empty() const noexcept { return inner_.is_empty(); }

    /// Cold-path diagnostic (mandate 9): the current attachment count.
    [[nodiscard]] std::string to_string() const {
        return std::format("WaitSet(attachments={})", len());
    }

private:
    explicit WaitSet(iox2::WaitSet<iox2::ServiceType::Ipc> inner) noexcept : inner_(std::move(inner)) {}

    /// Build the iox2 callback that adapts the com-typed callback (design D2).
    template <class Callback>
    auto make_adapter(Callback& callback) {
        return [&callback](iox2::WaitSetAttachmentId<iox2::ServiceType::Ipc> id)
                   -> iox2::CallbackProgression {
            const CallbackProgression progression = callback(WaitSetAttachmentId{std::move(id)});
            return progression == CallbackProgression::Stop ? iox2::CallbackProgression::Stop
                                                            : iox2::CallbackProgression::Continue;
        };
    }

    /// Collapse a run result/error into a Status (design D5); signal-driven
    /// termination and interrupts are normal stops, not failures.
    template <class RunResult>
    static Status to_status(const RunResult& result) noexcept {
        if (!result) {
            const Error error = detail::map_error(result.error());
            if (error == Error::Ok) {
                return {};
            }
            return std::unexpected(error);
        }
        return {};
    }

    iox2::WaitSet<iox2::ServiceType::Ipc> inner_;
};

/// Free helper (mandate 9): mirrors the member.
[[nodiscard]] inline std::string to_string(const WaitSet& waitset) {
    return waitset.to_string();
}

} // namespace nexigen::com

/// The wait set formats as its diagnostic string (mandate 11).
template <> struct std::formatter<nexigen::com::WaitSet> : std::formatter<std::string_view> {
    template <class Ctx>
    auto format(const nexigen::com::WaitSet& waitset, Ctx& ctx) const {
        const std::string text = waitset.to_string();
        return std::formatter<std::string_view>::format(text, ctx);
    }
};
