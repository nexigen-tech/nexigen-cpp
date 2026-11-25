// nexigen-canview — CANopen decode orchestration.
#pragma once

#include <array>
#include <cstdint>
#include <optional>

#include "can_record_view.hpp"
#include "canopen_registry.hpp"
#include "co_decoded.hpp"
#include "co_runtime.hpp"
#include "cob_id_router.hpp"
#include "sdo_decoder.hpp"

namespace nexigen::canview {

/// @brief Routes one CAN frame through NMT/EMCY/SDO/PDO decoders.
class CanOpenDecoder {
public:
    /// @brief Bind the decoder to the registry and router.
    /// @param registry Bindings.
    /// @param router Routes built from the registry.
    CanOpenDecoder(CanOpenRegistry& registry, CobIdRouter& router);

    /// @brief Ensure every binding has a runtime.
    void sync();

    /// @brief Decode one record.
    /// @param record Record to decode.
    /// @param now_ns Host monotonic time.
    /// @return Decoded presentation.
    CoDecoded decode(const CanRecordView& record, std::uint64_t now_ns);

    /// @brief Runtime of one binding.
    /// @param slot Registry slot.
    /// @return Runtime or `nullptr`.
    [[nodiscard]] const CoNodeRuntime* runtime(std::uint16_t slot) const noexcept;

    /// @brief Colour policy.
    /// @return Current policy.
    [[nodiscard]] NodeColourMode colour_mode() const noexcept { return colour_mode_; }
    /// @brief Change the colour policy.
    /// @param mode New policy.
    void set_colour_mode(NodeColourMode mode) noexcept { colour_mode_ = mode; }
    /// @brief Colour for one decoded row.
    /// @param decoded Decoded frame.
    /// @return RGBA colour.
    [[nodiscard]] std::uint32_t colour(const CoDecoded& decoded) const noexcept;

private:
    /// @brief Create or refresh the runtime for one slot.
    /// @return Runtime or `nullptr` when the binding has no dictionary.
    CoNodeRuntime* ensure_runtime(std::uint16_t slot);
    /// @brief Decode SDO traffic and apply successful writes.
    void decode_sdo(const CanRecordView& record, const CobIdRoute& route, CoDecoded& out,
                    std::uint64_t now_ns);
    /// @brief Decode a TPDO/RPDO payload.
    void decode_pdo_record(const CanRecordView& record, const CobIdRoute& route, CoDecoded& out);

    CanOpenRegistry* registry_{nullptr}; ///< Binding registry.
    CobIdRouter* router_{nullptr};       ///< COB-ID router.
    std::array<std::optional<CoNodeRuntime>, CanOpenRegistry::capacity> runtimes_{};
    SdoDecoder sdo_{};                                 ///< SDO assembler.
    NodeColourMode colour_mode_{NodeColourMode::Node}; ///< Colour policy.
};

} // namespace nexigen::canview
