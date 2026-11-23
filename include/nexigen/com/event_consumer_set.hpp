// nexigen::com — multiplexed event consumer set.
#pragma once

#include <array>
#include <atomic>
#include <chrono>
#include <cstddef>
#include <format>
#include <optional>
#include <string>
#include <string_view>
#include <utility>

#include <unistd.h>

#include <nexigen/com/callback_progression.hpp>
#include <nexigen/com/channel_config.hpp>
#include <nexigen/com/detail/event_time_source.hpp>
#include <nexigen/com/error.hpp>
#include <nexigen/com/event.hpp>
#include <nexigen/com/event_options.hpp>
#include <nexigen/com/i_event_consumer.hpp>
#include <nexigen/com/node.hpp>
#include <nexigen/com/service_name.hpp>
#include <nexigen/com/wait_outcome.hpp>
#include <nexigen/com/wait_set_attachment_id.hpp>
#include <nexigen/com/wait_set_guard.hpp>
#include <nexigen/com/waitset.hpp>

namespace nexigen::com {

/// @brief Fixed-capacity multiplexer for heterogeneous event consumers.
///
/// Owns one wait set and one private wake pair; attaches consumer listeners
/// and drains the fired consumer. `wake_up()` is intended for orderly shutdown;
/// attach/detach/wait must run on the waiting thread.
///
/// @tparam MaxConsumers Fixed capacity; v1 allows 1..16.
/// @note Spec `com/event-system`, requirement "Consumer waiting and
///       multiplexing".
template <std::size_t MaxConsumers = 16> class EventConsumerSet {
    static_assert(MaxConsumers >= 1U, "EventConsumerSet needs at least one slot");
    static_assert(MaxConsumers <= ChannelConfig::max_supported_consumers,
                  "EventConsumerSet exceeds the v1 consumer limit");

public:
    /// @brief Create the wait set and wake pair.
    /// @param node Node used to create the private wake event service.
    explicit EventConsumerSet(Node& node) noexcept { initialize(node); }

    EventConsumerSet(const EventConsumerSet&) = delete;
    EventConsumerSet& operator=(const EventConsumerSet&) = delete;
    /// @brief Moving is disabled: the set owns transport handles and an atomic wake flag.
    EventConsumerSet(EventConsumerSet&&) = delete;
    /// @brief Move assignment is disabled.
    /// @return This set.
    EventConsumerSet& operator=(EventConsumerSet&&) = delete;
    ~EventConsumerSet() = default;

    /// @brief Creation status.
    /// @return Empty status when the set is usable.
    [[nodiscard]] const Status& status() const noexcept { return status_; }

    /// @brief Whether the set was created successfully.
    /// @return `true` when `status()` is successful.
    explicit operator bool() const noexcept { return status_.has_value(); }

    /// @brief Attach one consumer listener.
    /// @param consumer Consumer that must outlive the attachment.
    /// @return Empty status, or a mapped `com::Error`.
    [[nodiscard]] Status attach(IEventConsumer& consumer) noexcept {
        if (!status_.has_value()) {
            return std::unexpected(status_.error());
        }
        if (consumer.attached_to_set()) {
            return std::unexpected(Error::InvalidArgument);
        }
        Slot* free_slot = nullptr;
        for (auto& slot : slots_) {
            if (slot.consumer == nullptr) {
                free_slot = &slot;
                break;
            }
        }
        if (free_slot == nullptr) {
            return std::unexpected(Error::OutOfSamples);
        }
        auto guard = waitset_->attach_notification(consumer.listener());
        if (!guard.has_value()) {
            return std::unexpected(guard.error());
        }
        free_slot->consumer = &consumer;
        free_slot->guard.emplace(std::move(*guard));
        consumer.set_attached_to_set(true);
        ++count_;
        return {};
    }

    /// @brief Drain every attached consumer once.
    ///
    /// Pending events may predate the first wait notification, so callers that
    /// multiplex several consumers should drain before waiting.
    ///
    /// @return Empty status, or the first drain failure.
    [[nodiscard]] Status drain_all() noexcept {
        if (!status_.has_value()) {
            return std::unexpected(status_.error());
        }
        for (auto& slot : slots_) {
            if (slot.consumer == nullptr) {
                continue;
            }
            if (auto drained = slot.consumer->drain(); !drained.has_value()) {
                return drained;
            }
        }
        return {};
    }

    /// @brief Detach one consumer listener.
    /// @param consumer Consumer to detach.
    /// @return Empty status, or `InvalidArgument` when not attached.
    [[nodiscard]] Status detach(IEventConsumer& consumer) noexcept {
        if (!status_.has_value()) {
            return std::unexpected(status_.error());
        }
        for (auto& slot : slots_) {
            if (slot.consumer == &consumer) {
                slot.guard.reset();
                slot.consumer = nullptr;
                consumer.set_attached_to_set(false);
                --count_;
                return {};
            }
        }
        return std::unexpected(Error::InvalidArgument);
    }

    /// @brief Block until an event or wake-up.
    /// @return `Events` or `WokenUp`, or a wait/drain error.
    [[nodiscard]] Result<WaitOutcome> wait() noexcept {
        auto early = prepare_wait();
        if (!early.has_value()) {
            return std::unexpected(early.error());
        }
        if (early->has_value()) {
            return **early;
        }
        WaitState state{};
        auto callback = [this, &state](WaitSetAttachmentId id) {
            return process_attachment(std::move(id), state);
        };
        const auto waited = waitset_->wait_and_process_once(callback);
        if (!waited.has_value()) {
            return std::unexpected(waited.error());
        }
        return finish_wait(state, WaitOutcome::WokenUp);
    }

    /// @brief Block until an event, wake-up, or timeout.
    /// @param timeout Maximum wait duration.
    /// @return `Events`, `Timeout`, or `WokenUp`, or a wait/drain error.
    [[nodiscard]] Result<WaitOutcome> wait_for(std::chrono::milliseconds timeout) noexcept {
        auto early = prepare_wait();
        if (!early.has_value()) {
            return std::unexpected(early.error());
        }
        if (early->has_value()) {
            return **early;
        }
        WaitState state{};
        auto callback = [this, &state](WaitSetAttachmentId id) {
            return process_attachment(std::move(id), state);
        };
        const auto waited = waitset_->wait_and_process_once_with_timeout(callback, timeout);
        if (!waited.has_value()) {
            if (waited.error() == Error::Timeout) {
                return WaitOutcome::Timeout;
            }
            return std::unexpected(waited.error());
        }
        return finish_wait(state, WaitOutcome::Timeout);
    }

    /// @brief Interrupt a waiting thread for orderly shutdown.
    ///
    /// May be called from another thread. The wake is coalesced; pending data
    /// remains available for a later drain.
    void wake_up() noexcept {
        wake_requested_.store(true, std::memory_order_release);
        if (wake_notifier_.has_value()) {
            static_cast<void>(wake_notifier_->notify());
        }
    }

    /// @brief Fixed consumer capacity.
    /// @return `MaxConsumers`.
    [[nodiscard]] static constexpr std::size_t capacity() noexcept { return MaxConsumers; }

    /// @brief Current attachment count.
    /// @return Number of attached consumers.
    [[nodiscard]] std::size_t size() const noexcept { return count_; }

    /// @brief Render the cold-path diagnostic string.
    /// @return Human-readable set state.
    [[nodiscard]] std::string to_string() const {
        return std::format("EventConsumerSet(attached={}, capacity={}, valid={})", count_,
                           MaxConsumers, status_.has_value());
    }

private:
    struct Slot {
        IEventConsumer* consumer{nullptr};
        std::optional<WaitSetGuard> guard{};
    };

    struct WaitState {
        bool events{false};
        bool woken{false};
        Status drain_status{};
    };

    [[nodiscard]] Result<std::optional<WaitOutcome>> prepare_wait() noexcept {
        if (!status_.has_value()) {
            return std::unexpected(status_.error());
        }
        if (wake_requested_.exchange(false, std::memory_order_acq_rel)) {
            return std::optional<WaitOutcome>{WaitOutcome::WokenUp};
        }
        const auto pending = drain_pending();
        if (!pending.has_value()) {
            return std::unexpected(pending.error());
        }
        if (*pending) {
            return std::optional<WaitOutcome>{WaitOutcome::Events};
        }
        return std::optional<WaitOutcome>{};
    }

    [[nodiscard]] CallbackProgression process_attachment(WaitSetAttachmentId id,
                                                         WaitState& state) noexcept {
        if (wake_guard_.has_value() && id.has_event_from(*wake_guard_)) {
            wake_requested_.store(false, std::memory_order_release);
            state.woken = true;
            return CallbackProgression::Stop;
        }
        for (auto& slot : slots_) {
            if (slot.consumer != nullptr && slot.guard.has_value() &&
                id.has_event_from(*slot.guard)) {
                const auto drained = slot.consumer->drain();
                if (!drained.has_value()) {
                    state.drain_status = drained;
                    return CallbackProgression::Stop;
                }
                state.events = true;
                return CallbackProgression::Stop;
            }
        }
        return CallbackProgression::Continue;
    }

    [[nodiscard]] Result<WaitOutcome> finish_wait(const WaitState& state,
                                                  WaitOutcome fallback) noexcept {
        if (!state.drain_status.has_value()) {
            return std::unexpected(state.drain_status.error());
        }
        if (state.woken) {
            return WaitOutcome::WokenUp;
        }
        if (state.events) {
            return WaitOutcome::Events;
        }
        return fallback;
    }

    [[nodiscard]] Result<bool> drain_pending() noexcept {
        std::uint64_t before = 0;
        std::uint64_t after = 0;
        for (auto& slot : slots_) {
            if (slot.consumer == nullptr) {
                continue;
            }
            before += slot.consumer->received();
            const auto drained = slot.consumer->drain();
            if (!drained.has_value()) {
                return std::unexpected(drained.error());
            }
            after += slot.consumer->received();
        }
        return after > before;
    }

    void initialize(Node& node) noexcept {
        auto waitset = WaitSet::create_without_signal_handling();
        if (!waitset.has_value()) {
            status_ = std::unexpected(waitset.error());
            return;
        }
        waitset_.emplace(std::move(*waitset));
        if (waitset_->capacity() < MaxConsumers + 1U) {
            status_ = std::unexpected(Error::OutOfSamples);
            return;
        }

        const auto sequence = detail::next_event_set_id();
        const std::string name = std::format("Nexigen/EventSet/Wake/{}/{}", ::getpid(), sequence);
        auto service_name = ServiceName::create(name.c_str());
        if (!service_name.has_value()) {
            status_ = std::unexpected(service_name.error());
            return;
        }
        const EventOptions options{1U, 1U, 1U};
        auto notifier = EventNotifier::create(node, *service_name, options);
        if (!notifier.has_value()) {
            status_ = std::unexpected(notifier.error());
            return;
        }
        auto listener = EventListener::create(node, *service_name, options);
        if (!listener.has_value()) {
            status_ = std::unexpected(listener.error());
            return;
        }
        auto guard = waitset_->attach_notification(*listener);
        if (!guard.has_value()) {
            status_ = std::unexpected(guard.error());
            return;
        }
        wake_notifier_.emplace(std::move(*notifier));
        wake_listener_.emplace(std::move(*listener));
        wake_guard_.emplace(std::move(*guard));
        status_ = {};
    }

    std::optional<WaitSet> waitset_{};
    std::optional<EventNotifier> wake_notifier_{};
    std::optional<EventListener> wake_listener_{};
    std::optional<WaitSetGuard> wake_guard_{};
    std::array<Slot, MaxConsumers> slots_{};
    std::size_t count_{0};
    std::atomic<bool> wake_requested_{false};
    Status status_{};
};

} // namespace nexigen::com

/// @brief Formats an event consumer set as its diagnostic string.
template <std::size_t MaxConsumers>
struct std::formatter<nexigen::com::EventConsumerSet<MaxConsumers>>
    : std::formatter<std::string_view> {
    /// @brief Write the set diagnostic into the format context.
    /// @tparam Ctx Format context type.
    /// @param set Set to format.
    /// @param ctx Output context.
    /// @return Iterator past the last written character.
    template <class Ctx>
    auto format(const nexigen::com::EventConsumerSet<MaxConsumers>& set, Ctx& ctx) const {
        return std::formatter<std::string_view>::format(set.to_string(), ctx);
    }
};
