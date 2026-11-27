// nexigen::canopen — bit-exact PDO unpacking.
#pragma once

#include <nexigen/canopen/eds_dictionary.hpp>
#include <nexigen/canopen/eds_pdo_mapping.hpp>
#include <nexigen/canopen/pdo_decode_result.hpp>
#include <nexigen/hw/can/can_message.hpp>

namespace nexigen::canopen {

/// @brief Unpack one PDO payload using a mapping snapshot.
/// @param message Frame carrying the payload.
/// @param mapping Mapping for this PDO.
/// @param dictionary Bound dictionary, or `nullptr` for raw extraction.
/// @return Structured decode result; no presentation text is produced.
[[nodiscard]] PdoDecodeResult decode_pdo(const nexigen::hw::can::CanMessage& message,
                                         const EdsPdoMapping& mapping,
                                         const EdsDictionary* dictionary) noexcept;

} // namespace nexigen::canopen
