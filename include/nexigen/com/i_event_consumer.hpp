// nexigen::com — type-erased event consumer for a consumer set.
#pragma once

#include <cstdint>
#include <string>

#include <nexigen/com/error.hpp>
#include <nexigen/com/event.hpp>

namespace nexigen::com {

/// @brief Non-template consumer interface used by `EventConsumerSet`.
///
/// The interface exposes only the listener needed for multiplexing and the
/// non-blocking drain. Payload typing stays in `EventConsumer<T, ...>`.
///
/// @note Spec `com/event-system`, requirement "Consumer waiting and
///       multiplexing".
class IEventConsumer {
public:
    virtual ~IEventConsumer() = default;

    /// @brief Drain every currently queued event.
    /// @return Empty status, or the first receive/callback failure.
    [[nodiscard]] virtual Status drain() noexcept = 0;

    /// @brief Consume pending listener activations without blocking.
    ///
    /// A wait set reports that an attachment fired; the consumer must clear
    /// that listener activation before draining its sample queue. Otherwise
    /// the transport readiness stays set and the wait loop spins.
    ///
    /// @return Empty status, or the mapped listener error.
    [[nodiscard]] virtual Status consume_activation() noexcept = 0;

    /// @brief The listener attached to a wait set.
    /// @return Borrowed listener; it must outlive the attachment.
    [[nodiscard]] virtual const EventListener& listener() const noexcept = 0;

    /// @brief Whether this consumer is currently attached to a consumer set.
    /// @return `true` while set-attached.
    [[nodiscard]] virtual bool attached_to_set() const noexcept = 0;

    /// @brief Mark or clear the set-attached state.
    /// @param attached New attachment state.
    virtual void set_attached_to_set(bool attached) noexcept = 0;

    /// @brief Number of delivered events.
    /// @return Cumulative delivered count.
    [[nodiscard]] virtual std::uint64_t received() const noexcept = 0;

    /// @brief Render the cold-path diagnostic string.
    /// @return Human-readable consumer state.
    [[nodiscard]] virtual std::string to_string() const { return "IEventConsumer()"; }
};

} // namespace nexigen::com
