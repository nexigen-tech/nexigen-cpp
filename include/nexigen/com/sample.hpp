// nexigen::com — read-only view of a shared-memory sample (change
// add-com-module; spec com/publish-subscribe "Subscriber receive").
//
// Sample<T> is the subscriber-side handle to the very bytes the publisher
// wrote: a read-only RAII view over the iceoryx2 shared-memory slot. Dropping
// it returns the slot to the pool.
#pragma once

#include <format>
#include <string>
#include <utility>

#include <iox2/iceoryx2.hpp>

#include <nexigen/com/concepts/message_concept.hpp>

namespace nexigen::com {

template <concepts::Message T> class Subscriber;

/// Read-only view of one received payload.
template <concepts::Message T>
class Sample {
public:
    Sample(const Sample&) = delete;
    Sample& operator=(const Sample&) = delete;
    Sample(Sample&&) noexcept = default;
    Sample& operator=(Sample&&) noexcept = default;

    /// The payload the publisher wrote (no copy).
    [[nodiscard]] const T& payload() const noexcept { return inner_.payload(); }

    /// Cold-path diagnostic (mandate 9).
    [[nodiscard]] std::string to_string() const { return "Sample()"; }

    /// The underlying transport handle (module-internal surface only).
    [[nodiscard]] iox2::Sample<iox2::ServiceType::Ipc, T, void>& handle() noexcept { return inner_; }

private:
    friend class Subscriber<T>;

    explicit Sample(iox2::Sample<iox2::ServiceType::Ipc, T, void> inner) noexcept
        : inner_(std::move(inner)) {}

    iox2::Sample<iox2::ServiceType::Ipc, T, void> inner_;
};

/// Free helper (mandate 9): mirrors the member.
template <concepts::Message T>
[[nodiscard]] inline std::string to_string(const Sample<T>& sample) {
    return sample.to_string();
}

} // namespace nexigen::com
