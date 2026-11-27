// nexigen::canopen — one decoded SDO event.
#pragma once

#include <cstddef>
#include <cstdint>
#include <format>
#include <string>
#include <string_view>

#include <nexigen/canopen/sdo_event_kind.hpp>

namespace nexigen::canopen {

/// @brief One structured SDO event produced by the assembler.
struct SdoEvent {
    SdoEventKind kind{SdoEventKind::None}; ///< Event classification.
    bool response{false};                  ///< Frame came from the server direction.
    bool segmented{false};                 ///< Event is part of a segmented transfer.
    bool last{false};                      ///< Last segment of a segmented transfer.
    std::uint16_t index{0};                ///< Object index.
    std::uint8_t subindex{0};              ///< Object subindex.
    std::uint32_t abort_code{0};           ///< Abort code for `Abort`.
    std::uint32_t value{0};                ///< Expedited value.
    std::uint8_t value_bytes{0};           ///< Valid bytes in `value`.
    std::size_t size{0};                   ///< Completed payload size.

    /// @brief Cold-path diagnostic rendering.
    /// @return One-line event summary.
    [[nodiscard]] std::string to_string() const {
        return std::format("SdoEvent(kind={}, index=0x{:04x}, sub={}, response={}, size={})",
                           nexigen::canopen::to_string(kind), index, subindex, response, size);
    }
};

/// @brief Free helper mirroring `SdoEvent::to_string`.
[[nodiscard]] inline std::string to_string(const SdoEvent& event) {
    return event.to_string();
}

/// @brief Serialize one SDO event.
void to_json(nlohmann::json& j, const SdoEvent& event);
/// @brief Parse one SDO event without throwing.
void from_json(const nlohmann::json& j, SdoEvent& event) noexcept;

} // namespace nexigen::canopen
