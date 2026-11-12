// nexigen::com — mutable views of a loaned shared-memory slot (change
// add-com-module; change add-trace-propagation adds the custom header H;
// spec com/publish-subscribe "Zero-copy publication").
//
// SampleMutUninit<T, H> is the slot borrowed by loan_uninit() before the
// payload is written; write_payload(T) constructs the payload in place and
// returns the initialized SampleMut<T, H>; Publisher::send() hands it over.
// With H != void, user_header_mut() writes the custom header before send().
#pragma once

#include <format>
#include <string>
#include <type_traits>
#include <utility>

#include <iox2/iceoryx2.hpp>

#include <nexigen/com/concepts/message_concept.hpp>

namespace nexigen::com {

/// @brief Forward declaration of the publisher that loans samples.
template <concepts::Message T, typename H> class Publisher;

/// @brief Forward declaration of the uninitialized loan handle.
template <concepts::Message T, typename H> class SampleMutUninit;

/// @brief Initialized, writable view of one loaned slot, ready to send.
///
/// Produced by `SampleMutUninit::write_payload()`, which constructs the
/// payload in place inside the shared-memory slot. `payload()` and
/// `user_header_mut()` allow last-minute edits before `Publisher::send()`
/// hands the slot to the transport. Dropping the view without sending returns
/// the slot to the pool.
///
/// The view is move-only, owns the loan, and is confined to the publishing
/// thread; it must not outlive its `Publisher`.
///
/// @tparam T Payload type; constrained by `com::concepts::Message`, i.e.
///         `std::is_trivially_copyable_v<T>`.
/// @tparam H Optional custom header type; `void` (the default) means no
///         header, otherwise `H` must also satisfy `com::concepts::Message`.
/// @note Spec `com/publish-subscribe`, requirement "Zero-copy publication".
template <concepts::Message T, typename H = void> class SampleMut {
    static_assert(std::is_void_v<H> || concepts::Message<H>,
                  "the custom header must satisfy com::concepts::Message");

public:
    /// @brief Copying is disabled: the view owns the loan.
    SampleMut(const SampleMut&) = delete;

    /// @brief Copy assignment is disabled: the view owns the loan.
    SampleMut& operator=(const SampleMut&) = delete;

    /// @brief Move-construct a view, transferring the loan.
    ///
    /// The moved-from view no longer releases the slot on destruction.
    SampleMut(SampleMut&&) noexcept = default;

    /// @brief Move-assign a view, transferring the loan.
    ///
    /// The destination's previous loan is released first.
    /// @return `*this`.
    SampleMut& operator=(SampleMut&&) noexcept = default;

    /// @brief Mutable payload access before send.
    ///
    /// @return Mutable reference to the slot; writes land in shared memory
    ///         directly and are visible to subscribers after `send()`.
    /// @note Spec `com/publish-subscribe`, requirement "Zero-copy
    ///       publication". Single-writer: the calling thread owns the loan.
    [[nodiscard]] T& payload() noexcept { return inner_.payload_mut(); }

    /// @brief Const payload access before send.
    ///
    /// @return Const reference to the slot's current contents.
    [[nodiscard]] const T& payload() const noexcept { return inner_.payload(); }

    /// @brief Mutable custom-header access before send.
    ///
    /// @tparam U Effective header type; defaults to `H` and only participates
    ///         when `H` is not `void`.
    /// @return Mutable reference to the header that `send()` attaches to the
    ///         payload.
    /// @note Spec `com/publish-subscribe`, requirement "Zero-copy
    ///       publication": the header must be written before `send()`.
    template <typename U = H, typename = std::enable_if_t<!std::is_void_v<U>>>
    [[nodiscard]] U& user_header_mut() noexcept {
        return inner_.user_header_mut();
    }

    /// @brief The underlying transport handle.
    ///
    /// @return Mutable reference to the wrapped `iox2::SampleMut`.
    /// @note Module-internal surface only: business code must stay
    ///       transport-agnostic (spec `com/service-management`, requirement
    ///       "Protocol-independent boundary").
    [[nodiscard]] iox2::SampleMut<iox2::ServiceType::Ipc, T, H>& handle() noexcept {
        return inner_;
    }

private:
    friend class Publisher<T, H>;
    friend class SampleMutUninit<T, H>;

    /// @brief Adopt an initialized transport sample.
    ///
    /// @param inner Transport sample returned by `write_payload()`.
    /// @note Private by design: only the publisher pipeline produces views.
    explicit SampleMut(iox2::SampleMut<iox2::ServiceType::Ipc, T, H> inner) noexcept
        : inner_(std::move(inner)) {}

    iox2::SampleMut<iox2::ServiceType::Ipc, T, H> inner_; ///< Owned loan.
};

/// @brief Uninitialized loaned slot: construct the payload with `write_payload`.
///
/// Returned by `Publisher::loan_uninit()`. The slot is raw shared memory, so
/// the payload must be constructed exactly once before it can be sent; the
/// handle itself is not otherwise usable.
///
/// The handle is move-only, holds one exclusive loan, and is confined to the
/// publishing thread. Dropping it without writing returns the slot to the
/// pool without exhausting it.
///
/// @tparam T Payload type; constrained by `com::concepts::Message`.
/// @tparam H Optional custom header type; `void` (the default) means no
///         header.
/// @note Spec `com/publish-subscribe`, requirements "Zero-copy publication"
///       and "Loan without send returns the slot".
template <concepts::Message T, typename H = void> class SampleMutUninit {
    static_assert(std::is_void_v<H> || concepts::Message<H>,
                  "the custom header must satisfy com::concepts::Message");

public:
    /// @brief Copying is disabled: the handle owns the loan.
    SampleMutUninit(const SampleMutUninit&) = delete;

    /// @brief Copy assignment is disabled: the handle owns the loan.
    SampleMutUninit& operator=(const SampleMutUninit&) = delete;

    /// @brief Move-construct a loan, transferring slot ownership.
    ///
    /// The moved-from handle no longer releases the slot on destruction.
    SampleMutUninit(SampleMutUninit&&) noexcept = default;

    /// @brief Move-assign a loan, transferring slot ownership.
    ///
    /// The destination's previous loan is released first.
    /// @return `*this`.
    SampleMutUninit& operator=(SampleMutUninit&&) noexcept = default;

    /// @brief Construct the payload in place and produce the sendable sample.
    ///
    /// @param value Payload value moved into the shared-memory slot; no
    ///        serialization or intermediate buffer is involved.
    /// @return The initialized `SampleMut<T, H>`, ready for
    ///         `Publisher::send()`.
    /// @note Spec `com/publish-subscribe`, requirement "Zero-copy
    ///       publication". Single-writer: the calling thread owns the loan.
    [[nodiscard]] SampleMut<T, H> write_payload(T value) noexcept {
        auto initialized = inner_.write_payload(std::move(value));
        return SampleMut<T, H>{std::move(initialized)};
    }

private:
    friend class Publisher<T, H>;

    /// @brief Adopt an uninitialized transport loan.
    ///
    /// @param inner Transport loan returned by `Publisher::loan_uninit()`.
    /// @note Private by design: only the publisher pipeline produces loans.
    explicit SampleMutUninit(iox2::SampleMutUninit<iox2::ServiceType::Ipc, T, H> inner) noexcept
        : inner_(std::move(inner)) {}

    iox2::SampleMutUninit<iox2::ServiceType::Ipc, T, H> inner_; ///< Owned loan.
};

} // namespace nexigen::com
