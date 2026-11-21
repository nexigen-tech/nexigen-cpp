// nexigen::recorder — cold-path recorder configuration loader.
#include <nexigen/recorder/config_loader.hpp>

#include <cctype>
#include <fstream>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include <nlohmann/json.hpp>

#include <nexigen/core/integrity/crc32c.hpp>
#include <nexigen/core/integrity/sha256.hpp>
#include <nexigen/recorder/channel_declare.hpp>
#include <nexigen/recorder/schema_declare.hpp>
#include <nexigen/recorder/storage_settings.hpp>
#include <nexigen/recorder/wire_types.hpp>

namespace nexigen::recorder {

namespace {

[[nodiscard]] bool read_file(const std::string& path, std::vector<std::uint8_t>& out) {
    std::ifstream input(path, std::ios::binary | std::ios::ate);
    if (!input) {
        return false;
    }
    const auto size = input.tellg();
    if (size < 0 || static_cast<std::uint64_t>(size) > kMaxSchemaBytes) {
        return false;
    }
    out.resize(static_cast<std::size_t>(size));
    input.seekg(0, std::ios::beg);
    if (!out.empty()) {
        input.read(reinterpret_cast<char*>(out.data()), static_cast<std::streamsize>(out.size()));
        if (input.gcount() != static_cast<std::streamsize>(out.size())) {
            return false;
        }
    }
    return true;
}

[[nodiscard]] bool digest_matches(std::span<const std::uint8_t> bytes, std::string_view expected) {
    if (expected.empty()) {
        return true;
    }
    const std::string actual = core::integrity::sha256_hex(bytes);
    if (actual.size() != expected.size()) {
        return false;
    }
    for (std::size_t i = 0; i < actual.size(); ++i) {
        const auto lhs = static_cast<unsigned char>(actual[i]);
        const auto rhs = static_cast<unsigned char>(expected[i]);
        if (std::tolower(lhs) != std::tolower(rhs)) {
            return false;
        }
    }
    return true;
}

/// @brief Load one schema entry, embedding and digest-checking its file bytes.
[[nodiscard]] core::Status load_schema_entry(const nlohmann::json& entry,
                                             std::vector<SchemaDeclare>& out) {
    if (!entry.is_object()) {
        return std::unexpected(core::ErrorCode::InvalidArgument);
    }
    SchemaDeclare schema{};
    from_json(entry, schema);
    const auto schema_path = entry.find("path");
    if (schema_path == entry.end() || !schema_path->is_string()) {
        return std::unexpected(core::ErrorCode::InvalidArgument);
    }
    std::vector<std::uint8_t> data;
    if (!read_file(schema_path->get<std::string>(), data)) {
        return std::unexpected(core::ErrorCode::InvalidArgument);
    }
    const auto schema_hash = entry.find("sha256");
    const std::string expected = schema_hash != entry.end() && schema_hash->is_string()
                                     ? schema_hash->get<std::string>()
                                     : std::string{};
    if (!digest_matches(data, expected)) {
        return std::unexpected(core::ErrorCode::SecurityError);
    }
    schema.data = std::move(data);
    schema.data_crc32c = core::integrity::crc32c(schema.data);
    if (!schema.validate().has_value()) {
        return std::unexpected(core::ErrorCode::InvalidArgument);
    }
    out.push_back(std::move(schema));
    return {};
}

[[nodiscard]] core::Status load_schemas(const nlohmann::json& document,
                                        std::vector<SchemaDeclare>& out) {
    const auto schemas = document.find("schemas");
    if (schemas == document.end() || !schemas->is_array()) {
        return {};
    }
    for (const auto& entry : *schemas) {
        const auto status = load_schema_entry(entry, out);
        if (!status.has_value()) {
            return status;
        }
    }
    return {};
}

[[nodiscard]] core::Status load_channels(const nlohmann::json& document,
                                         std::vector<ChannelDeclare>& out) {
    const auto channels = document.find("channels");
    if (channels == document.end() || !channels->is_array()) {
        return {};
    }
    for (const auto& entry : *channels) {
        if (!entry.is_object()) {
            return std::unexpected(core::ErrorCode::InvalidArgument);
        }
        ChannelDeclare channel{};
        from_json(entry, channel);
        if (!channel.validate().has_value()) {
            return std::unexpected(core::ErrorCode::InvalidArgument);
        }
        out.push_back(std::move(channel));
    }
    return {};
}

} // namespace

core::Result<RecorderConfig> load_recorder_config(const std::string& path) noexcept {
    std::ifstream input(path);
    if (!input) {
        return std::unexpected(core::ErrorCode::NotFound);
    }
    const auto document = nlohmann::json::parse(input, nullptr, false);
    if (document.is_discarded() || !document.is_object()) {
        return std::unexpected(core::ErrorCode::InvalidArgument);
    }
    RecorderConfig config{};
    const auto storage = document.find("storage");
    if (storage == document.end() || !storage->is_object()) {
        return std::unexpected(core::ErrorCode::InvalidArgument);
    }
    from_json(*storage, config.storage);

    const auto schemas_status = load_schemas(document, config.schemas);
    if (!schemas_status.has_value()) {
        return std::unexpected(schemas_status.error());
    }
    const auto channels_status = load_channels(document, config.channels);
    if (!channels_status.has_value()) {
        return std::unexpected(channels_status.error());
    }
    if (!config.storage.validate().has_value() || config.schemas.size() > kMaxSchemas ||
        config.channels.size() > kMaxChannels) {
        return std::unexpected(core::ErrorCode::InvalidArgument);
    }
    return config;
}

} // namespace nexigen::recorder
