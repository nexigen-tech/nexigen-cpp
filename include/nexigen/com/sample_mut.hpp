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

template <concepts::Message T, typename H> class Publisher;
template <concepts::Message T, typename H> class SampleMutUninit;

/// Initialized, writable view of one loaned slot (ready to send).
template <concepts::Message T, typename H = void>
class SampleMut {
    static_assert(std::is_void_v<H> || concepts::Message<H>,
                  "the custom header must satisfy com::concepts::Message");

public:
    SampleMut(const SampleMut&) = delete;
    SampleMut& operator=(const SampleMut&) = delete;
    SampleMut(SampleMut&&) noexcept = default;
    SampleMut& operator=(SampleMut&&) noexcept = default;

    /// Mutable payload access before send.
    [[nodiscard]] T& payload() noexcept { return inner_.payload_mut(); }
    [[nodiscard]] const T& payload() const noexcept { return inner_.payload(); }

    /// Mutable custom header access before send (only when H is not void).
    template <typename U = H, typename = std::enable_if_t<!std::is_void_v<U>>>
    [[nodiscard]] U& user_header_mut() noexcept {
        return inner_.user_header_mut();
    }

    /// The underlying transport handle (module-internal surface only).
    [[nodiscard]] iox2::SampleMut<iox2::ServiceType::Ipc, T, H>& handle() noexcept {
        return inner_;
    }

private:
    friend class Publisher<T, H>;
    friend class SampleMutUninit<T, H>;

    explicit SampleMut(iox2::SampleMut<iox2::ServiceType::Ipc, T, H> inner) noexcept
        : inner_(std::move(inner)) {}

    iox2::SampleMut<iox2::ServiceType::Ipc, T, H> inner_;
};

/// Uninitialized loaned slot: construct the payload with write_payload.
template <concepts::Message T, typename H = void>
class SampleMutUninit {
    static_assert(std::is_void_v<H> || concepts::Message<H>,
                  "the custom header must satisfy com::concepts::Message");

public:
    SampleMutUninit(const SampleMutUninit&) = delete;
    SampleMutUninit& operator=(const SampleMutUninit&) = delete;
    SampleMutUninit(SampleMutUninit&&) noexcept = default;
    SampleMutUninit& operator=(SampleMutUninit&&) noexcept = default;

    /// Construct the payload in place and produce the sendable sample.
    [[nodiscard]] SampleMut<T, H> write_payload(T value) noexcept {
        auto initialized = inner_.write_payload(std::move(value));
        return SampleMut<T, H>{std::move(initialized)};
    }

private:
    friend class Publisher<T, H>;

    explicit SampleMutUninit(iox2::SampleMutUninit<iox2::ServiceType::Ipc, T, H> inner) noexcept
        : inner_(std::move(inner)) {}

    iox2::SampleMutUninit<iox2::ServiceType::Ipc, T, H> inner_;
};

} // namespace nexigen::com
