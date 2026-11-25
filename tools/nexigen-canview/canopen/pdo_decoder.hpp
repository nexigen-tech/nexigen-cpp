// nexigen-canview — PDO mapping unpacking.
#pragma once

#include <cstdint>

#include <nexigen/hw/can/can_message.hpp>

#include "co_decoded.hpp"
#include "eds_dictionary.hpp"

namespace nexigen::canview {

/// @brief Unpack one PDO payload using a mapping snapshot.
/// @param message Frame.
/// @param mapping Mapping for this PDO.
/// @param dictionary Bound dictionary, or `nullptr` for raw decode.
/// @param out Decoded output.
/// @return `true` when at least one signal was decoded.
bool decode_pdo(const nexigen::hw::can::CanMessage& message, const EdsPdoMapping& mapping,
                const EdsDictionary* dictionary, CoDecoded& out) noexcept;

} // namespace nexigen::canview
