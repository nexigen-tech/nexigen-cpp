#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <format>
#include <string>
#include <string_view>
#include <type_traits>

#include <nlohmann/json.hpp>

#include <nexigen/core/result.hpp>
#include <nexigen/hw/can/can_message.hpp>

namespace nexigen::hw::can {

/// @brief Bounded batch of CAN driver messages.
///
/// The envelope is trivially copyable and carries a per-link logical sequence
/// for loss accounting. Every contained `CanMessage` keeps its own timestamp,
/// hardware sequence, kind, flags, origin and payload; batching never rewrites
/// those values.
struct alignas(8) CanMessageBatch {
    /// @brief Maximum messages in one batch.
    static constexpr std::size_t max_messages{100U};

    std::uint64_t first_sequence{0};                 ///< Logical sequence of the first message.
    std::uint16_t channel_id{0};                     ///< Channel shared by every message.
    std::uint16_t count{0};                          ///< Number of valid entries in `messages`.
    std::uint32_t reserved{0};                       ///< Reserved; must be zero on publish.
    std::array<CanMessage, max_messages> messages{}; ///< Message storage.

    /// @brief Validate the envelope and every contained message.
    /// @return Empty status, or `InvalidArgument` for the first invalid field.
    [[nodiscard]] core::Status validate() const noexcept;

    /// @brief Clear the envelope.
    void clear() noexcept;

    /// @brief Render the batch for cold-path diagnostics.
    /// @return Human-readable summary.
    [[nodiscard]] std::string to_string() const;
};

static_assert(sizeof(CanMessageBatch) == 11216U, "CanMessageBatch layout must stay pinned");
static_assert(alignof(CanMessageBatch) == 8U, "CanMessageBatch must stay 8-byte aligned");
static_assert(std::is_trivially_copyable_v<CanMessageBatch>);

/// @brief Free helper mirroring `CanMessageBatch::to_string`.
/// @param batch Batch to render.
/// @return Human-readable summary.
[[nodiscard]] inline std::string to_string(const CanMessageBatch& batch) {
    return batch.to_string();
}

/// @brief Serialize a batch for diagnostic JSON.
/// @param j Destination.
/// @param batch Batch to serialize.
void to_json(nlohmann::json& j, const CanMessageBatch& batch);

/// @brief Parse a batch from diagnostic JSON without throwing.
/// @param j Source.
/// @param batch Destination, unchanged when the input is unusable.
void from_json(const nlohmann::json& j, CanMessageBatch& batch) noexcept;

} // namespace nexigen::hw::can

/// @brief Formats a batch through its diagnostic string.
template <>
struct std::formatter<nexigen::hw::can::CanMessageBatch> : std::formatter<std::string_view> {
    /// @brief Write the batch diagnostic into the format context.
    /// @tparam Ctx Format context type.
    /// @param batch Batch to format.
    /// @param ctx Output context.
    /// @return Iterator past the last written character.
    template <class Ctx>
    auto format(const nexigen::hw::can::CanMessageBatch& batch, Ctx& ctx) const {
        return std::formatter<std::string_view>::format(batch.to_string(), ctx);
    }
};
