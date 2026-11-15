// nexigen::phm test double — in-memory watchdog device (change
// close-phm-safety-loop). Never touches a kernel device, so CI cannot reset.
#pragma once

#include <algorithm>
#include <cstdint>
#include <string>
#include <string_view>

#include <nexigen/core/result.hpp>
#include <nexigen/phm/watchdog_device.hpp>

namespace nexigen::test {

/// @brief Records every device call and optionally caps the granted timeout.
class FakeWatchdogDevice final : public nexigen::phm::WatchdogDevice {
public:
    [[nodiscard]] core::Status open(std::string_view path) noexcept override {
        if (fail_open) {
            return std::unexpected(core::ErrorCode::SystemError);
        }
        if (opened_) {
            return std::unexpected(core::ErrorCode::Conflict);
        }
        path_ = std::string(path);
        opened_ = true;
        ++opens;
        return {};
    }

    [[nodiscard]] core::Result<std::uint32_t>
    set_timeout(std::uint32_t requested_ms) noexcept override {
        if (!opened_) {
            return std::unexpected(core::ErrorCode::NotInitialized);
        }
        requested = requested_ms;
        granted = max_granted == 0U ? requested_ms : std::min(requested_ms, max_granted);
        ++timeouts;
        return granted;
    }

    [[nodiscard]] core::Status feed() noexcept override {
        if (!opened_) {
            return std::unexpected(core::ErrorCode::NotInitialized);
        }
        ++feeds;
        return {};
    }

    [[nodiscard]] core::Status disarm() noexcept override {
        ++disarms;
        opened_ = false;
        return {};
    }

    [[nodiscard]] bool is_open() const noexcept { return opened_; }
    [[nodiscard]] const std::string& path() const noexcept { return path_; }

    int opens{0};           ///< Successful open calls.
    int timeouts{0};        ///< set_timeout calls.
    int feeds{0};           ///< Feed writes.
    int disarms{0};         ///< Disarm calls.
    std::uint32_t requested{0}; ///< Last requested timeout.
    std::uint32_t granted{0};   ///< Last granted timeout.
    std::uint32_t max_granted{0}; ///< Simulated driver cap; 0 = grant the request.
    bool fail_open{false};        ///< Make `open` fail with a system error.

private:
    bool opened_{false};
    std::string path_{};
};

} // namespace nexigen::test
