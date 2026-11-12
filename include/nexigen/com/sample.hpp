// nexigen::com — read-only view of a shared-memory sample (change
// add-com-module; change add-trace-propagation adds the custom header H;
// spec com/publish-subscribe "Subscriber receive").
//
// Sample<T, H> is the subscriber-side handle to the very bytes the publisher
// wrote: a read-only RAII view over the iceoryx2 shared-memory slot. When H is
// not void, user_header() exposes the custom header the publisher attached.
// Dropping it returns the slot to the pool.
#pragma once

#include <format>
#include <string>
#include <type_traits>
#include <utility>

#include <iox2/iceoryx2.hpp>

#include <nexigen/com/concepts/message_concept.hpp>

namespace nexigen::com {

/// @brief Forward declaration of the subscriber that produces `Sample` views.
template <concepts::Message T, typename H> class Subscriber;

/// @brief Read-only view of one received payload plus an optional custom header.
///
/// `Sample` is the subscriber-side handle to the very bytes the publisher
/// wrote: a read-only RAII view over one shared-memory slot, with no copy and
/// no serialization involved. Dropping the sample returns the slot to the
/// pool, which lowers the producer's queue fill level.
///
/// The view is move-only, mirrors the transport handle, and must not outlive
/// the `Subscriber` that produced it. It is a read path: access is a plain
/// shared-memory load, not a synchronization point.
///
/// @tparam T Payload type; constrained by `com::concepts::Message`, i.e.
///         `std::is_trivially_copyable_v<T>`.
/// @tparam H Optional custom header type attached by the publisher; `void`
///         (the default) means no header, otherwise `H` must also satisfy
///         `com::concepts::Message`.
/// @note Spec `com/publish-subscribe`, requirement "Subscriber receive" and
///       "Zero-copy publication".
template <concepts::Message T, typename H = void> class Sample {
    static_assert(std::is_void_v<H> || concepts::Message<H>,
                  "the custom header must satisfy com::concepts::Message");

public:
    /// @brief Copying is disabled: the view owns the slot borrow.
    Sample(const Sample&) = delete;

    /// @brief Copy assignment is disabled: the view owns the slot borrow.
    Sample& operator=(const Sample&) = delete;

    /// @brief Move-construct a view, transferring the slot borrow.
    ///
    /// The moved-from view no longer releases the slot on destruction.
    Sample(Sample&&) noexcept = default;

    /// @brief Move-assign a view, transferring the slot borrow.
    ///
    /// The destination's previous slot borrow is released first.
    /// @return `*this`.
    Sample& operator=(Sample&&) noexcept = default;

    /// @brief The payload the publisher wrote.
    ///
    /// @return Const reference into the shared-memory slot; valid until the
    ///         sample is dropped.
    /// @note Spec `com/publish-subscribe`, requirement "Subscriber receive":
    ///       the returned reference is the producer's bytes, never a copy.
    [[nodiscard]] const T& payload() const noexcept { return inner_.payload(); }

    /// @brief The custom header the publisher attached to this sample.
    ///
    /// @tparam U Effective header type; defaults to `H` and only participates
    ///         when `H` is not `void`.
    /// @return Const reference into the shared-memory slot; valid until the
    ///         sample is dropped.
    /// @note Spec `com/publish-subscribe`, requirement "Zero-copy
    ///       publication": the header travels beside the payload on the same
    ///       loaned slot.
    template <typename U = H, typename = std::enable_if_t<!std::is_void_v<U>>>
    [[nodiscard]] const U& user_header() const noexcept {
        return inner_.user_header();
    }

    /// @brief Render the cold-path diagnostic string.
    ///
    /// @return The constant marker `Sample()`; per-sample state lives in the
    ///         shared slot and is intentionally not part of the diagnostic.
    /// @note Mandate 9 (universal `to_string`); allocates, so cold path only.
    [[nodiscard]] std::string to_string() const { return "Sample()"; }

    /// @brief The underlying transport handle.
    ///
    /// @return Mutable reference to the wrapped `iox2::Sample`.
    /// @note Module-internal surface only: business code must stay
    ///       transport-agnostic (spec `com/service-management`, requirement
    ///       "Protocol-independent boundary").
    [[nodiscard]] iox2::Sample<iox2::ServiceType::Ipc, T, H>& handle() noexcept { return inner_; }

private:
    friend class Subscriber<T, H>;

    /// @brief Adopt a received transport sample.
    ///
    /// @param inner Transport sample returned by the subscriber.
    /// @note Private by design: only `Subscriber::receive()` produces views.
    explicit Sample(iox2::Sample<iox2::ServiceType::Ipc, T, H> inner) noexcept
        : inner_(std::move(inner)) {}

    iox2::Sample<iox2::ServiceType::Ipc, T, H> inner_; ///< Owned slot borrow.
};

/// @brief Free helper mirroring `Sample::to_string()`.
///
/// @tparam T Payload type; constrained by `com::concepts::Message`.
/// @tparam H Optional custom header type; `void` means no header.
/// @param sample Sample to render.
/// @return The diagnostic string.
/// @note Mandate 9 (universal `to_string`); allocates, so cold path only.
template <concepts::Message T, typename H = void>
[[nodiscard]] inline std::string to_string(const Sample<T, H>& sample) {
    return sample.to_string();
}

} // namespace nexigen::com
