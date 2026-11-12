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

/// @brief Multiplexes several attachments and blocks until one fires.
///
/// Thin wrapper over `iox2::WaitSet<Ipc>`: it attaches event listeners
/// (optionally with a deadline or a periodic interval), blocks the calling
/// thread in the kernel until one of them fires, and dispatches the fired
/// source to a callback whose signature uses only `nexigen::com` types, so no
/// `iox2::` name leaks into business code and no busy polling occurs.
///
/// Each attach call returns a `WaitSetGuard` that owns the attachment; the
/// guard keeps the source attached exactly as long as it lives.
///
/// The wait set owns the transport handle, so copying is disabled and moving
/// transfers ownership. Waiting and attaching must come from one thread; the
/// instance is not internally synchronized.
///
/// @note Implements `com/waitset`: "Multiplexed blocking wait", "Event
///       notification attachment", "Deadline attachment", "Interval
///       attachment", "Callback dispatch and source identity", and "WaitSet
///       diagnostics".
class WaitSet {
public:
    /// @brief Create a wait set on the local IPC transport.
    ///
    /// The transport installs its own SIGINT/SIGTERM handlers; use
    /// `create_without_signal_handling()` when the caller owns signal handling.
    ///
    /// @return The constructed wait set, or a mapped `com::Error`
    ///         (`CreationFailed`, ...) when the transport cannot create it.
    /// @note Spec `com/waitset`, requirement "Multiplexed blocking wait".
    [[nodiscard]] static Result<WaitSet> create() noexcept {
        auto created = iox2::WaitSetBuilder().create<iox2::ServiceType::Ipc>();
        if (!created) {
            return std::unexpected(detail::map_error(created.error()));
        }
        return WaitSet{std::move(*created)};
    }

    /// @brief Create a wait set without transport-installed signal handlers.
    ///
    /// The underlying transport does not install its own SIGINT/SIGTERM
    /// handlers (`SignalHandlingMode::Disabled`): the caller installs and owns
    /// signal handling. Needed by the PHM daemon, which must stop its loop on
    /// its own terms.
    ///
    /// @return The constructed wait set, or a mapped `com::Error`
    ///         (`CreationFailed`, ...) when the transport cannot create it.
    /// @note Spec `com/waitset`, requirement "Multiplexed blocking wait".
    [[nodiscard]] static Result<WaitSet> create_without_signal_handling() noexcept {
        auto created = iox2::WaitSetBuilder()
                           .signal_handling_mode(iox2::SignalHandlingMode::Disabled)
                           .create<iox2::ServiceType::Ipc>();
        if (!created) {
            return std::unexpected(detail::map_error(created.error()));
        }
        return WaitSet{std::move(*created)};
    }

    /// @brief Copying is disabled: the wait set owns the transport handle.
    WaitSet(const WaitSet&) = delete;

    /// @brief Copy assignment is disabled: the wait set owns the transport
    ///        handle.
    WaitSet& operator=(const WaitSet&) = delete;

    /// @brief Move-construct a wait set, transferring the transport handle.
    ///
    /// Attachments move with the handle; guards stay valid against the new
    /// owner.
    WaitSet(WaitSet&&) noexcept = default;

    /// @brief Move-assign a wait set, transferring the transport handle.
    ///
    /// The destination's previous handle is released; self-assignment is a
    /// no-op.
    /// @return `*this`.
    WaitSet& operator=(WaitSet&&) noexcept = default;

    /// @brief Attach an event listener; it fires when the paired notifier emits.
    ///
    /// @param listener Listener to attach; must outlive the returned guard.
    /// @return The attachment guard, or a mapped `com::Error`
    ///         (`OutOfSamples` when capacity is exhausted, `InvalidArgument`
    ///         when already attached, ...) when the attach fails.
    /// @note Spec `com/waitset`, requirements "Event notification attachment"
    ///       and "Attachment lifecycle".
    [[nodiscard]] Result<WaitSetGuard> attach_notification(const EventListener& listener) noexcept {
        auto guard = inner_.attach_notification(listener.handle());
        if (!guard) {
            return std::unexpected(detail::map_error(guard.error()));
        }
        return WaitSetGuard{std::move(*guard)};
    }

    /// @brief Attach an event listener plus a deadline.
    ///
    /// The source fires on the event or when the deadline elapses; the
    /// attachment id distinguishes the two via `has_event_from()` and
    /// `has_missed_deadline()`. The deadline resets on every event.
    ///
    /// @param listener Listener to attach; must outlive the returned guard.
    /// @param deadline Relative time after which a missed deadline fires.
    /// @return The attachment guard, or a mapped `com::Error` when the attach
    ///         fails.
    /// @note Spec `com/waitset`, requirement "Deadline attachment".
    [[nodiscard]] Result<WaitSetGuard>
    attach_deadline(const EventListener& listener, std::chrono::milliseconds deadline) noexcept {
        auto guard = inner_.attach_deadline(listener.handle(),
                                            iox2::bb::Duration::from_millis(deadline.count()));
        if (!guard) {
            return std::unexpected(detail::map_error(guard.error()));
        }
        return WaitSetGuard{std::move(*guard)};
    }

    /// @brief Attach a periodic interval source.
    ///
    /// The source fires every `interval` even when no notifier event occurs.
    ///
    /// @param interval Period between successive activations; must be positive.
    /// @return The attachment guard, or a mapped `com::Error` when the attach
    ///         fails.
    /// @note Spec `com/waitset`, requirement "Interval attachment".
    [[nodiscard]] Result<WaitSetGuard>
    attach_interval(std::chrono::milliseconds interval) noexcept {
        auto guard = inner_.attach_interval(iox2::bb::Duration::from_millis(interval.count()));
        if (!guard) {
            return std::unexpected(detail::map_error(guard.error()));
        }
        return WaitSetGuard{std::move(*guard)};
    }

    /// @brief Block until one source fires and process fired sources.
    ///
    /// The callback runs synchronously on the calling thread, once per fired
    /// source. Returning `CallbackProgression::Continue` handles the next
    /// source; `CallbackProgression::Stop` ends the loop and leaves the
    /// remaining sources unprocessed. The thread sleeps in the kernel while
    /// waiting, so the loop does not spin.
    ///
    /// @tparam Callback Callable invocable as
    ///         `CallbackProgression(WaitSetAttachmentId)`.
    /// @param callback Callback invoked for each fired attachment.
    /// @return Empty `Status` on a normal stop, or a mapped `com::Error` when
    ///         the run fails or nothing is attached.
    /// @note Spec `com/waitset`, requirements "Multiplexed blocking wait" and
    ///       "Callback dispatch and source identity".
    template <class Callback> [[nodiscard]] Status wait_and_process(Callback&& callback) noexcept {
        auto result = inner_.wait_and_process(make_adapter(callback));
        return to_status(result);
    }

    /// @brief Block until one source fires and process a single activation.
    ///
    /// Like `wait_and_process()`, but returns after the first callback
    /// invocation regardless of the progression it returns.
    ///
    /// @tparam Callback Callable invocable as
    ///         `CallbackProgression(WaitSetAttachmentId)`.
    /// @param callback Callback invoked for the fired attachment.
    /// @return Empty `Status` on a normal stop, or a mapped `com::Error` when
    ///         the run fails or nothing is attached.
    /// @note Spec `com/waitset`, requirement "Multiplexed blocking wait".
    template <class Callback>
    [[nodiscard]] Status wait_and_process_once(Callback&& callback) noexcept {
        auto result = inner_.wait_and_process_once(make_adapter(callback));
        return to_status(result);
    }

    /// @brief Block until one source fires or `timeout` elapses.
    ///
    /// A timeout on an empty wait set returns promptly with an error rather
    /// than blocking for the full timeout or spinning.
    ///
    /// @tparam Callback Callable invocable as
    ///         `CallbackProgression(WaitSetAttachmentId)`.
    /// @param callback Callback invoked for the fired attachment.
    /// @param timeout Maximum time to wait; `0` makes the call non-blocking.
    /// @return Empty `Status` on a normal stop, or a mapped `com::Error`
    ///         (`InvalidArgument` for an empty wait set, ...) when the run
    ///         fails.
    /// @note Spec `com/waitset`, requirement "Multiplexed blocking wait",
    ///       including the empty-wait-set scenario.
    template <class Callback>
    [[nodiscard]] Status
    wait_and_process_once_with_timeout(Callback&& callback,
                                       std::chrono::milliseconds timeout) noexcept {
        auto result = inner_.wait_and_process_once_with_timeout(
            make_adapter(callback), iox2::bb::Duration::from_millis(timeout.count()));
        return to_status(result);
    }

    /// @brief Maximum number of attachments this wait set can hold.
    ///
    /// @return The transport-reported capacity.
    [[nodiscard]] std::uint64_t capacity() const noexcept { return inner_.capacity(); }

    /// @brief Current number of attachments.
    ///
    /// @return The number of live attachment guards.
    [[nodiscard]] std::uint64_t len() const noexcept { return inner_.len(); }

    /// @brief True when nothing is attached.
    ///
    /// @return `true` when the attachment count is zero.
    [[nodiscard]] bool is_empty() const noexcept { return inner_.is_empty(); }

    /// @brief Render the cold-path diagnostic string.
    ///
    /// @return `WaitSet(attachments=<n>)`.
    /// @note Mandate 9 (universal `to_string`); `com/waitset`, requirement
    ///       "WaitSet diagnostics". Allocates, so cold path only.
    [[nodiscard]] std::string to_string() const {
        return std::format("WaitSet(attachments={})", len());
    }

private:
    /// @brief Adopt an already-created transport wait set.
    ///
    /// @param inner Transport wait set returned by the iceoryx2 builder.
    /// @note Private by design: the supported construction paths are `create()`
    ///       and `create_without_signal_handling()`.
    explicit WaitSet(iox2::WaitSet<iox2::ServiceType::Ipc> inner) noexcept
        : inner_(std::move(inner)) {}

    /// @brief Build the transport callback that adapts the com-typed callback.
    ///
    /// Translates `com::CallbackProgression` to `iox2::CallbackProgression` so
    /// the caller's signature stays transport-agnostic (design D2).
    ///
    /// @tparam Callback Callable invocable as
    ///         `CallbackProgression(WaitSetAttachmentId)`.
    /// @param callback Callback to wrap; must outlive the returned adapter.
    /// @return Adapter invocable by the transport per activation.
    template <class Callback> auto make_adapter(Callback& callback) {
        return [&callback](iox2::WaitSetAttachmentId<iox2::ServiceType::Ipc> id)
                   -> iox2::CallbackProgression {
            const CallbackProgression progression = callback(WaitSetAttachmentId{std::move(id)});
            return progression == CallbackProgression::Stop ? iox2::CallbackProgression::Stop
                                                            : iox2::CallbackProgression::Continue;
        };
    }

    /// @brief Collapse a run result/error into a `Status`.
    ///
    /// Signal-driven termination and interrupts are normal stops, not
    /// failures (design D5): an `Ok` mapping yields an empty success.
    ///
    /// @tparam RunResult Transport result type exposing `operator bool()` and
    ///         `error()`.
    /// @param result Transport run result.
    /// @return Empty `Status` on success or a mapped `com::Error`.
    /// @note Spec `com/waitset`, requirement "Multiplexed blocking wait".
    template <class RunResult> static Status to_status(const RunResult& result) noexcept {
        if (!result) {
            const Error error = detail::map_error(result.error());
            if (error == Error::Ok) {
                return {};
            }
            return std::unexpected(error);
        }
        return {};
    }

    iox2::WaitSet<iox2::ServiceType::Ipc> inner_; ///< Owned transport handle.
};

/// @brief Free helper mirroring `WaitSet::to_string()`.
///
/// @param waitset Wait set to render.
/// @return The wait set's diagnostic string.
/// @note Mandate 9 (universal `to_string`); allocates, so cold path only.
[[nodiscard]] inline std::string to_string(const WaitSet& waitset) {
    return waitset.to_string();
}

} // namespace nexigen::com

/// @brief Formats a `WaitSet` as its diagnostic string.
///
/// @note Mandate 11 (universal `std::formatter`); delegates to
///       `WaitSet::to_string()`, which allocates, so cold path only.
template <> struct std::formatter<nexigen::com::WaitSet> : std::formatter<std::string_view> {
    /// @brief Write the wait set's diagnostic text into the format context.
    ///
    /// @tparam Ctx `std::format` context type.
    /// @param waitset Wait set to format.
    /// @param ctx Output context supplied by `std::format`.
    /// @return Iterator one past the last written character.
    template <class Ctx> auto format(const nexigen::com::WaitSet& waitset, Ctx& ctx) const {
        const std::string text = waitset.to_string();
        return std::formatter<std::string_view>::format(text, ctx);
    }
};
