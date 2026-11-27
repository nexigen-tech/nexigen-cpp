// nexigen-canview — SDO presentation adapter over the shared core.
#pragma once

#include <cstddef>
#include <cstdint>
#include <optional>

#include <nexigen/canopen/sdo_assembler.hpp>
#include <nexigen/canopen/sdo_completed_write.hpp>

#include "co_decoded.hpp"
#include "eds_dictionary.hpp"

namespace nexigen::canview {

using SdoCompletedWrite = canopen::SdoCompletedWrite;

/// @brief Decodes SDO traffic and renders the viewer presentation.
class SdoDecoder {
public:
    /// @brief Assembly buffer size.
    static constexpr std::size_t max_transfer_bytes = canopen::SdoAssembler::max_transfer_bytes;
    /// @brief Segmented-transfer timeout.
    static constexpr std::uint64_t timeout_ns = canopen::SdoAssembler::timeout_ns;

    /// @brief Consume one SDO frame.
    /// @param message Frame.
    /// @param response Whether the frame came from `0x580 + node`.
    /// @param out Decoded output.
    /// @param now_ns Host monotonic time.
    /// @param dictionary Optional dictionary for object names.
    /// @return `true` when the frame looked like SDO traffic.
    bool feed(const nexigen::hw::can::CanMessage& message, bool response, CoDecoded& out,
              std::uint64_t now_ns, const EdsDictionary* dictionary = nullptr);

    /// @brief Take the next completed write.
    /// @return Completed write or `std::nullopt`.
    std::optional<SdoCompletedWrite> take_completed();

    /// @brief Drop assemblers older than the timeout.
    /// @param now_ns Host monotonic time.
    void expire(std::uint64_t now_ns) noexcept;

    /// @brief Drop every assembler and pending write.
    void reset() noexcept;

    /// @brief Number of in-flight assemblies.
    /// @return Assembly count.
    [[nodiscard]] std::size_t active_assemblies() const noexcept;

private:
    canopen::SdoAssembler assembler_{}; ///< Shared protocol assembler.
};

} // namespace nexigen::canview
