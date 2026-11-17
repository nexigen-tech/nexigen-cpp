// nexigen::recorder — self-contained schema declaration.
#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include <nlohmann/json.hpp>

#include <nexigen/core/result.hpp>

namespace nexigen::recorder {

/// @brief A schema descriptor embedded in the session.
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
    /// @return `Ok`, or a named validation error.
    [[nodiscard]] core::Status validate() const noexcept;

    /// @brief Cold-path diagnostic rendering.
    [[nodiscard]] std::string to_string() const;
};

/// @brief Free helper mirroring `SchemaDeclare::to_string`.
/// @param value Value to render.
/// @return Human-readable summary.
[[nodiscard]] inline std::string to_string(const SchemaDeclare& schema) {
    return schema.to_string();
}

void to_json(nlohmann::json& j, const SchemaDeclare& schema);
void from_json(const nlohmann::json& j, SchemaDeclare& schema) noexcept;

} // namespace nexigen::recorder
