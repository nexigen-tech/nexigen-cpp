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

template <concepts::Message T, typename H> class Subscriber;

/// Read-only view of one received payload (+ optional custom header).
template <concepts::Message T, typename H = void> class Sample {
    static_assert(std::is_void_v<H> || concepts::Message<H>,
                  "the custom header must satisfy com::concepts::Message");

public:
    Sample(const Sample&) = delete;
    Sample& operator=(const Sample&) = delete;
    Sample(Sample&&) noexcept = default;
    Sample& operator=(Sample&&) noexcept = default;

    /// The payload the publisher wrote (no copy).
    [[nodiscard]] const T& payload() const noexcept { return inner_.payload(); }

    /// The custom header the publisher attached (only when H is not void).
    template <typename U = H, typename = std::enable_if_t<!std::is_void_v<U>>>
    [[nodiscard]] const U& user_header() const noexcept {
        return inner_.user_header();
    }

    /// Cold-path diagnostic (mandate 9).
    [[nodiscard]] std::string to_string() const { return "Sample()"; }

    /// The underlying transport handle (module-internal surface only).
    [[nodiscard]] iox2::Sample<iox2::ServiceType::Ipc, T, H>& handle() noexcept { return inner_; }

private:
    friend class Subscriber<T, H>;

    explicit Sample(iox2::Sample<iox2::ServiceType::Ipc, T, H> inner) noexcept
        : inner_(std::move(inner)) {}

    iox2::Sample<iox2::ServiceType::Ipc, T, H> inner_;
};

/// Free helper (mandate 9): mirrors the member.
template <concepts::Message T, typename H = void>
[[nodiscard]] inline std::string to_string(const Sample<T, H>& sample) {
    return sample.to_string();
}

} // namespace nexigen::com
