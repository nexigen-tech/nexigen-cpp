#pragma once

#include <format>
#include <atomic>
#include <memory>
#include <span>
#include <string>
#include <string_view>
#include <type_traits>
#include <utility>
#include <vector>

#include <nexigen/app/app.hpp>
#include <nexigen/com/event_header.hpp>
#include <nexigen/core/result.hpp>
#include <nexigen/hw/can/can_channel_config.hpp>
#include <nexigen/hw/can/can_channel_set.hpp>
#include <nexigen/hw/can/can_channel_status.hpp>
#include <nexigen/hw/can/can_message.hpp>
#include <nexigen/hw/can/can_message_batch.hpp>
#include <nexigen/hw/can/com/can_transport.hpp>
#include <nexigen/hw/can/can_service_names.hpp>
#include <nexigen/hw/can/com/can_publisher.hpp>

namespace nexigen::hw::can {

/// @brief Per-channel CAN client counters.
struct CanClientChannelStats {
    std::uint64_t received{0};        ///< Messages delivered to the rx callback.
    std::uint64_t status_received{0}; ///< Status messages delivered to the status callback.
    std::uint64_t sent{0};            ///< Successful tx publications.
    std::uint64_t failed{0};          ///< Rejected or failed tx publications.
    std::uint64_t dropped{0};         ///< Messages rejected by channel binding.
    std::uint64_t losses{0};          ///< Transport loss reports.
};

/// @brief Application-side multi-channel CAN client built on `App<Policy>`.
///
/// One process can subscribe to `rx`/`status` for several channels and publish
/// `tx` to several channels. All callbacks run on the App `run()` thread.
///
/// @tparam Policy Infrastructure policy used by the underlying App.
template <class Policy> class CanClient {
private:
    struct ChannelCounters {
        std::atomic<std::uint64_t> received{0};
        std::atomic<std::uint64_t> status_received{0};
        std::atomic<std::uint64_t> dropped{0};
        std::atomic<std::uint64_t> losses{0};
    };

    [[nodiscard]] ChannelCounters* counter_for(std::string_view channel) const noexcept {
        for (const auto& entry : counters_) {
            if (entry.first == channel) {
                return entry.second.get();
            }
        }
        return nullptr;
    }

public:
    /// @brief Create a client and its App runtime.
    /// @param argc Argument count from `main`.
    /// @param argv Argument vector from `main`.
    /// @param app_name Fallback application name.
    /// @param channels Channel configurations.
    /// @param mode External or Embedded time base.
    /// @return The client, `InvalidArgument` for bad channels/identity, or a
    ///         successful null pointer for `--help`.
    [[nodiscard]] static core::Result<std::unique_ptr<CanClient>>
    create(int argc, const char* const argv[], std::string_view app_name,
           std::span<const CanChannelConfig> channels,
           app::TimeBaseMode mode = app::TimeBaseMode::External) noexcept {
        if (core::Status valid = validate_channels(channels); !valid) {
            return std::unexpected(valid.error());
        }
        auto created = app::App<Policy>::create(argc, argv, app_name, mode);
        if (!created) {
            return std::unexpected(created.error());
        }
        if (!*created) {
            return std::unique_ptr<CanClient>{};
        }
        auto client = std::unique_ptr<CanClient>(new CanClient(std::move(*created)));
        client->channels_.assign(channels.begin(), channels.end());
        client->counters_.reserve(channels.size());
        for (const auto& config : channels) {
            client->counters_.emplace_back(config.name, std::make_unique<ChannelCounters>());
        }
        return client;
    }

    /// @brief Subscribe to one channel's `rx` stream.
    /// @tparam Callback Callable with `(const CanMessage&)` or
    ///         `(const com::EventHeader&, const CanMessage&)`.
    /// @param channel Channel name.
    /// @param callback Callback invoked on the App run thread.
    /// @return `true` when the subscription was attached.
    template <class Callback>
    bool subscribe(std::string_view channel, Callback&& callback) noexcept {
        if (started_ || run_completed_ || shutdown_done_) {
            return false;
        }
        const CanChannelConfig* config = find(channel);
        if (config == nullptr || app_ == nullptr) {
            return false;
        }
        auto service = rx_service(channel);
        if (!service) {
            return false;
        }
        auto* counter = counter_for(channel);
        return app_->template subscribe_with_config<CanMessageBatch>(
            *service,
            [counter, id = config->channel_id, cb = std::forward<Callback>(callback)](
                const com::EventHeader& header, const CanMessageBatch& batch) mutable {
                for (std::size_t index = 0; index < batch.count; ++index) {
                    const CanMessage& message = batch.messages[index];
                    if (message.channel_id != id) {
                        counter->dropped.fetch_add(1U, std::memory_order_relaxed);
                        continue;
                    }
                    counter->received.fetch_add(1U, std::memory_order_relaxed);
                    if constexpr (std::is_invocable_v<Callback&, const com::EventHeader&,
                                                      const CanMessage&>) {
                        cb(header, message);
                    } else {
                        cb(message);
                    }
                }
            },
            batch_channel_config());
    }

    /// @brief Subscribe to one channel's `rx` stream with a loss callback.
    /// @tparam Callback Callable with `(const CanMessage&)` or
    ///         `(const com::EventHeader&, const CanMessage&)`.
    /// @tparam OnLoss Callable with `(const com::LossInfo&)`.
    /// @param channel Channel name.
    /// @param callback Event callback.
    /// @param on_loss Transport-loss callback.
    /// @return `true` when the subscription was attached.
    template <class Callback, class OnLoss>
    bool subscribe_with_loss(std::string_view channel, Callback&& callback,
                             OnLoss&& on_loss) noexcept {
        if (started_ || run_completed_ || shutdown_done_) {
            return false;
        }
        const CanChannelConfig* config = find(channel);
        if (config == nullptr || app_ == nullptr) {
            return false;
        }
        auto service = rx_service(channel);
        if (!service) {
            return false;
        }
        auto* counter = counter_for(channel);
        return app_->template subscribe_with_config<CanMessageBatch>(
            *service,
            [counter, id = config->channel_id, cb = std::forward<Callback>(callback)](
                const com::EventHeader& header, const CanMessageBatch& batch) mutable {
                for (std::size_t index = 0; index < batch.count; ++index) {
                    const CanMessage& message = batch.messages[index];
                    if (message.channel_id != id) {
                        counter->dropped.fetch_add(1U, std::memory_order_relaxed);
                        continue;
                    }
                    counter->received.fetch_add(1U, std::memory_order_relaxed);
                    if constexpr (std::is_invocable_v<Callback&, const com::EventHeader&,
                                                      const CanMessage&>) {
                        cb(header, message);
                    } else {
                        cb(message);
                    }
                }
            },
            [counter, cb = std::forward<OnLoss>(on_loss)](const com::LossInfo& info) mutable {
                counter->losses.fetch_add(1U, std::memory_order_relaxed);
                cb(info);
            },
            batch_channel_config());
    }

    /// @brief Subscribe to one channel's `status` stream.
    /// @tparam Callback Callable with `(const CanChannelStatus&)`.
    /// @param channel Channel name.
    /// @param callback Callback invoked on the App run thread.
    /// @return `true` when the subscription was attached.
    template <class Callback>
    bool subscribe_status(std::string_view channel, Callback&& callback) noexcept {
        if (started_ || run_completed_ || shutdown_done_) {
            return false;
        }
        const CanChannelConfig* config = find(channel);
        if (config == nullptr || app_ == nullptr) {
            return false;
        }
        auto service = status_service(channel);
        if (!service) {
            return false;
        }
        auto* counter = counter_for(channel);
        com::ChannelConfig status_config{};
        status_config.max_publishers = 2U;
        return app_->template subscribe_with_config<CanChannelStatus>(
            *service,
            [counter,
             cb = std::forward<Callback>(callback)](const CanChannelStatus& status) mutable {
                counter->status_received.fetch_add(1U, std::memory_order_relaxed);
                cb(status);
            },
            [counter](const com::LossInfo&) {
                counter->losses.fetch_add(1U, std::memory_order_relaxed);
            },
            status_config);
    }

    /// @brief Create or fetch the publisher for one channel's `tx` stream.
    /// @param channel Channel name.
    /// @return Borrowed publisher owned by the client, or `NotFound`/setup error.
    [[nodiscard]] core::Result<CanPublisher*> publish(std::string_view channel) noexcept {
        if (started_ || run_completed_ || shutdown_done_) {
            return std::unexpected(core::ErrorCode::Conflict);
        }
        const CanChannelConfig* config = find(channel);
        if (config == nullptr || app_ == nullptr) {
            return std::unexpected(core::ErrorCode::NotFound);
        }
        for (auto& entry : publishers_) {
            if (entry.first == channel) {
                return entry.second.get();
            }
        }
        auto service = tx_service(channel);
        if (!service) {
            return std::unexpected(service.error());
        }
        auto* producer = app_->template publish<CanMessageBatch>(*service, batch_channel_config());
        if (producer == nullptr) {
            return std::unexpected(core::ErrorCode::SystemError);
        }
        auto publisher = std::make_unique<CanPublisher>(producer, config->channel_id);
        auto* borrowed = publisher.get();
        publishers_.emplace_back(std::string(channel), std::move(publisher));
        return borrowed;
    }

    /// @brief Run the underlying App until a stop is requested.
    /// @return Stop reason or the App error.
    [[nodiscard]] core::Result<app::InfrastructureStopReason> run() noexcept {
        if (started_ || run_completed_ || shutdown_done_ || app_ == nullptr) {
            return std::unexpected(core::ErrorCode::Conflict);
        }
        started_ = true;
        auto reason = app_->run();
        started_ = false;
        run_completed_ = true;
        return reason;
    }

    /// @brief Drain the App runtime.
    /// @return First shutdown failure.
    [[nodiscard]] core::Status shutdown() noexcept {
        if (shutdown_done_) {
            return {};
        }
        shutdown_done_ = true;
        return app_ != nullptr ? app_->shutdown() : core::Status{};
    }

    /// @brief Collected App and CAN diagnostics.
    /// @return Borrowed diagnostic span.
    [[nodiscard]] std::span<const core::SettingsDiagnostic> diagnostics() const noexcept {
        return app_->diagnostics();
    }

    /// @brief Configured channels.
    /// @return Borrowed channel span.
    [[nodiscard]] std::span<const CanChannelConfig> channels() const noexcept { return channels_; }

    /// @brief Read per-channel counters.
    /// @param channel Channel name.
    /// @return Counter snapshot, or `NotFound`.
    [[nodiscard]] core::Result<CanClientChannelStats>
    stats(std::string_view channel) const noexcept {
        const CanChannelConfig* config = find(channel);
        if (config == nullptr) {
            return std::unexpected(core::ErrorCode::NotFound);
        }
        const auto* counter = counter_for(channel);
        CanClientChannelStats result{};
        if (counter == nullptr) {
            return std::unexpected(core::ErrorCode::NotFound);
        }
        result.received = counter->received.load(std::memory_order_relaxed);
        result.status_received = counter->status_received.load(std::memory_order_relaxed);
        result.dropped = counter->dropped.load(std::memory_order_relaxed);
        result.losses = counter->losses.load(std::memory_order_relaxed);
        for (const auto& entry : publishers_) {
            if (entry.first == channel) {
                const auto stats = entry.second->stats();
                result.sent = stats.sent;
                result.failed = stats.failed;
                break;
            }
        }
        return result;
    }

    /// @brief Render the client for cold-path diagnostics.
    /// @return Human-readable summary.
    [[nodiscard]] std::string to_string() const {
        return std::format("CanClient(channels={}, publishers={})", channels_.size(),
                           publishers_.size());
    }

private:
    explicit CanClient(std::unique_ptr<app::App<Policy>> app) noexcept : app_(std::move(app)) {}

    [[nodiscard]] const CanChannelConfig* find(std::string_view channel) const noexcept {
        for (const auto& config : channels_) {
            if (config.name == channel) {
                return &config;
            }
        }
        return nullptr;
    }

    std::unique_ptr<app::App<Policy>> app_{};
    std::vector<CanChannelConfig> channels_{};
    std::vector<std::pair<std::string, std::unique_ptr<CanPublisher>>> publishers_{};
    std::vector<std::pair<std::string, std::unique_ptr<ChannelCounters>>> counters_{};
    bool started_{false};
    bool run_completed_{false};
    bool shutdown_done_{false};
};

/// @brief Free helper mirroring `CanClient::to_string`.
/// @tparam Policy Infrastructure policy.
/// @param client Client to render.
/// @return Human-readable summary.
template <class Policy>
[[nodiscard]] inline std::string to_string(const CanClient<Policy>& client) {
    return client.to_string();
}

} // namespace nexigen::hw::can

/// @brief `std::formatter` for `CanClient`.
template <class Policy>
struct std::formatter<nexigen::hw::can::CanClient<Policy>> : std::formatter<std::string_view> {
    /// @brief Format the client through `to_string`.
    /// @tparam Ctx Format context.
    /// @param client Client to render.
    /// @param ctx Context.
    /// @return Iterator past the text.
    template <class Ctx>
    auto format(const nexigen::hw::can::CanClient<Policy>& client, Ctx& ctx) const {
        const std::string text = client.to_string();
        return std::formatter<std::string_view>::format(text, ctx);
    }
};
