/// @file schema_declare.hpp
/// @brief Self-contained schema descriptor embedded in a session.
///
/// @note Spec `recorder/raw-storage`, requirements "Embedded schema and channel
///       registry" and "Storage type serialization"; the full descriptor bytes
///       travel with the session and no external path or SDK is required.
#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include <nlohmann/json.hpp>

#include <nexigen/core/result.hpp>

namespace nexigen::recorder {

/// @brief A schema descriptor embedded in the session.
///
/// `data` carries the complete JSON Schema, Protobuf `FileDescriptorSet`,
/// FlatBuffers `.bfbs`, or ROS 2 message text. `data_crc32c` lets the reader
/// validate the bytes without any external reference.
///
/// @note Spec `recorder/raw-storage`, requirement "Embedded schema and channel
///       registry"; `encoding` is restricted to the converter's supported set.
struct SchemaDeclare {
    std::uint32_t id{0};             ///< session-scoped schema id
    std::string name{};              ///< e.g. "nexigen.log.LogFrame"
    std::uint32_t version{1};        ///< schema version
    std::string encoding{};          ///< jsonschema | protobuf | flatbuffer | nexigen.pod.v1
    std::string units{};             ///< optional unit hint
    std::string endianness{"little"};///< payload byte order
    std::vector<std::uint8_t> data{};///< full embedded descriptor bytes
    std::uint32_t data_crc32c{0};    ///< CRC-32C of data

    /// @brief Validate id, bounds, encoding, and descriptor size.
    /// @return `Ok`, or `InvalidArgument`/`ResourceExhausted` for a rejected value.
    /// @note The descriptor is bounded by `kMaxSchemaBytes` (256 KiB).
    [[nodiscard]] core::Status validate() const noexcept;

    /// @brief Cold-path diagnostic rendering.
    /// @return Human-readable schema summary.
    [[nodiscard]] std::string to_string() const;
};

/// @brief Free helper mirroring `SchemaDeclare::to_string`.
/// @param schema Value to render.
/// @return Human-readable summary.
[[nodiscard]] inline std::string to_string(const SchemaDeclare& schema) {
    return schema.to_string();
}

/// @brief Serialize a schema declaration to `session.json`.
/// @param j Destination JSON object.
/// @param schema Schema declaration to serialize.
void to_json(nlohmann::json& j, const SchemaDeclare& schema);

/// @brief Parse a schema declaration from `session.json`.
/// @param j Source JSON object.
/// @param schema Destination declaration.
void from_json(const nlohmann::json& j, SchemaDeclare& schema) noexcept;

} // namespace nexigen::recorder
