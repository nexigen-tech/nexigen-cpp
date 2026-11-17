// nexigen::recorder — cold-path payload decoding for the converter.
#pragma once

#include <cstddef>
#include <cstdint>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include <nexigen/core/result.hpp>
#include <nexigen/recorder/dataset_info.hpp>

namespace nexigen::recorder {

/// @brief Representation produced for one exported payload.
enum class PayloadKind {
    Json,   ///< `text` is a JSON document
    Opaque, ///< `text` is base64 of the raw payload bytes
};

/// @brief Decoded representation of one record payload.
struct PayloadView {
    PayloadKind kind{PayloadKind::Opaque};
    std::string text{};
};

/// @brief True when the converter knows how to represent this schema encoding.
/// @param encoding Declared schema encoding.
/// @return `true` for JSON/POD, Protobuf, FlatBuffers, and ROS 2 schemas.
[[nodiscard]] bool encoding_supported(std::string_view encoding) noexcept;

/// @brief Decode one payload using the schema's embedded declaration.
///
/// The converter is on-demand and never links a producer SDK: opaque encodings
/// are validated against their embedded descriptor and passed through.
///
/// - `jsonschema`/`nexigen.pod.v1` with a field layout: fixed-offset POD decode.
/// - `jsonschema`/`json` without a layout: the payload is a JSON document.
/// - `protobuf`: the `FileDescriptorSet` is validated, payload passes through.
/// - `flatbuffer`: the `.bfbs` schema is validated, payload passes through.
/// - `ros2msg`: schema text only, payload passes through.
/// - Any other encoding: `UnsupportedFormat`.
///
/// @param encoding Declared schema encoding.
/// @param descriptor Embedded schema bytes (may be empty for JSON payloads).
/// @param fields Field layout parsed from the schema, when available.
/// @param payload Raw record payload.
/// @return JSON/opaque representation, or a named decode error.
[[nodiscard]] core::Result<PayloadView>
decode_payload(std::string_view encoding, std::span<const std::uint8_t> descriptor,
               const std::vector<DatasetField>& fields,
               std::span<const std::byte> payload) noexcept;

} // namespace nexigen::recorder
