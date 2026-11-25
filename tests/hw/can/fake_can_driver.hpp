#pragma once

#include <string>
#include <vector>

#include <nexigen/hw/can/can_driver.hpp>

namespace nexigen::hw::can::test {

/// @brief Deterministic in-memory CanDriver for unit tests.
class FakeCanDriver final : public CanDriver {
public:
    CanDriverCapabilities caps{};
    bool fail_open{false};
    int fail_open_at{-1};
    int open_attempts{0};
    core::Status submit_status{};
    int fail_submits{0};
    std::vector<CanMessage> submitted{};
    std::vector<CanMessageBatch> batches{};
    std::vector<CanChannelConfig> opened{};
    ICanMessageSink* sink{nullptr};

    [[nodiscard]] core::Result<CanDriverCapabilities> capabilities() noexcept override {
        return caps;
    }

    [[nodiscard]] core::Status open_channel(const CanChannelConfig& config,
                                            ICanMessageSink& target) noexcept override {
        if (fail_open || open_attempts++ == fail_open_at) {
            return std::unexpected(core::ErrorCode::SystemError);
        }
        opened.push_back(config);
        sink = &target;
        return {};
    }

    [[nodiscard]] core::Status submit(std::string_view channel,
                                      const CanMessage& message) noexcept override {
        bool known = false;
        for (const auto& config : opened) {
            if (config.name == channel) {
                known = true;
                break;
            }
        }
        if (!known) {
            return std::unexpected(core::ErrorCode::NotFound);
        }
        if (message.kind == CanMessageKind::FdData && !caps.supports_fd) {
            return std::unexpected(core::ErrorCode::UnsupportedFormat);
        }
        if (message.kind == CanMessageKind::Remote && !caps.supports_remote) {
            return std::unexpected(core::ErrorCode::UnsupportedFormat);
        }
        if (fail_submits > 0) {
            --fail_submits;
            return std::unexpected(core::ErrorCode::BufferFull);
        }
        if (!submit_status.has_value()) {
            return submit_status;
        }
        submitted.push_back(message);
        return {};
    }

    [[nodiscard]] core::Status submit_batch(std::string_view channel,
                                            const CanMessageBatch& batch) noexcept override {
        bool known = false;
        for (const auto& config : opened) {
            if (config.name == channel) {
                known = true;
                break;
            }
        }
        if (!known) {
            return std::unexpected(core::ErrorCode::NotFound);
        }
        if (fail_submits > 0) {
            --fail_submits;
            return std::unexpected(core::ErrorCode::BufferFull);
        }
        if (!submit_status.has_value()) {
            return submit_status;
        }
        batches.push_back(batch);
        for (std::size_t index = 0; index < batch.count; ++index) {
            submitted.push_back(batch.messages[index]);
        }
        return {};
    }

    [[nodiscard]] core::Status close_channel(std::string_view channel) noexcept override {
        for (auto it = opened.begin(); it != opened.end(); ++it) {
            if (it->name == channel) {
                opened.erase(it);
                sink = nullptr;
                return {};
            }
        }
        return std::unexpected(core::ErrorCode::NotFound);
    }

    void emit(const CanMessage& message) noexcept {
        if (sink != nullptr) {
            sink->on_message(message);
        }
    }
};

} // namespace nexigen::hw::can::test
