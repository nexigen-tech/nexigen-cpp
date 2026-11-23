#pragma once

#include <string>
#include <string_view>

#include <nexigen/core/result.hpp>

namespace nexigen::hw::can {

/// @brief Build `nexigen/hw/can/<channel>/rx`.
/// @param channel Validated channel name.
/// @return Service name, or `InvalidArgument`.
[[nodiscard]] core::Result<std::string> rx_service(std::string_view channel) noexcept;

/// @brief Build `nexigen/hw/can/<channel>/tx`.
/// @param channel Validated channel name.
/// @return Service name, or `InvalidArgument`.
[[nodiscard]] core::Result<std::string> tx_service(std::string_view channel) noexcept;

/// @brief Build `nexigen/hw/can/<channel>/status`.
/// @param channel Validated channel name.
/// @return Service name, or `InvalidArgument`.
[[nodiscard]] core::Result<std::string> status_service(std::string_view channel) noexcept;

/// @brief Build `nexigen/hw/can/<channel>/vbus/rx`.
/// @param channel Validated channel name.
/// @return Service name, or `InvalidArgument`.
[[nodiscard]] core::Result<std::string> vbus_rx_service(std::string_view channel) noexcept;

/// @brief Build `nexigen/hw/can/<channel>/vbus/tx`.
/// @param channel Validated channel name.
/// @return Service name, or `InvalidArgument`.
[[nodiscard]] core::Result<std::string> vbus_tx_service(std::string_view channel) noexcept;

} // namespace nexigen::hw::can
