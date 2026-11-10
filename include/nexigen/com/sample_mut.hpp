// nexigen::com — mutable views of a loaned shared-memory slot (change
// add-com-module; spec com/publish-subscribe "Zero-copy publication").
//
// SampleMutUninit<T> is the slot borrowed by loan_uninit() before the payload
// is written; write_payload(T) constructs the payload in place and returns the
// initialized SampleMut<T>; Publisher::send() hands it over. Dropping either
// without sending returns the slot to the pool.
#pragma once

#include <format>
#include <string>
#include <utility>

#include <iox2/iceoryx2.hpp>

#include <nexigen/com/concepts/message_concept.hpp>

namespace nexigen::com {

template <concepts::Message T> class Publisher;
template <concepts::Message T> class SampleMutUninit;

/// Initialized, writable view of one loaned slot (ready to send).
template <concepts::Message T>
class SampleMut {
public:
    SampleMut(const SampleMut&) = delete;
    SampleMut& operator=(const SampleMut&) = delete;
    SampleMut(SampleMut&&) noexcept = default;
    SampleMut& operator=(SampleMut&&) noexcept = default;

    /// Mutable payload access before send.
    [[nodiscard]] T& payload() noexcept { return inner_.payload_mut(); }
    [[nodiscard]] const T& payload() const noexcept { return inner_.payload(); }

    /// The underlying transport handle (module-internal surface only).
    [[nodiscard]] iox2::SampleMut<iox2::ServiceType::Ipc, T, void>& handle() noexcept {
        return inner_;
    }

private:
    friend class Publisher<T>;
    friend class SampleMutUninit<T>;

    explicit SampleMut(iox2::SampleMut<iox2::ServiceType::Ipc, T, void> inner) noexcept
        : inner_(std::move(inner)) {}

    iox2::SampleMut<iox2::ServiceType::Ipc, T, void> inner_;
};

/// Uninitialized loaned slot: construct the payload with write_payload.
template <concepts::Message T>
class SampleMutUninit {
public:
    SampleMutUninit(const SampleMutUninit&) = delete;
    SampleMutUninit& operator=(const SampleMutUninit&) = delete;
    SampleMutUninit(SampleMutUninit&&) noexcept = default;
    SampleMutUninit& operator=(SampleMutUninit&&) noexcept = default;

    /// Construct the payload in place and produce the sendable sample.
    [[nodiscard]] SampleMut<T> write_payload(T value) noexcept {
        auto initialized = inner_.write_payload(std::move(value));
        return SampleMut<T>{std::move(initialized)};
    }

private:
    friend class Publisher<T>;

    explicit SampleMutUninit(iox2::SampleMutUninit<iox2::ServiceType::Ipc, T, void> inner) noexcept
        : inner_(std::move(inner)) {}

    iox2::SampleMutUninit<iox2::ServiceType::Ipc, T, void> inner_;
};

} // namespace nexigen::com
