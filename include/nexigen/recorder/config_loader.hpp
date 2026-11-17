// nexigen::recorder — cold-path recorder configuration loader.
#pragma once

#include <string>
#include <vector>

#include <nexigen/core/result.hpp>
#include <nexigen/recorder/channel_declare.hpp>
#include <nexigen/recorder/schema_declare.hpp>
#include <nexigen/recorder/storage_settings.hpp>

namespace nexigen::recorder {

/// @brief Loaded recorder configuration with embedded schema bytes.
struct RecorderConfig {
    StorageSettings storage{};
    std::vector<SchemaDeclare> schemas{};
    std::vector<ChannelDeclare> channels{};
};

/// @brief Load and validate a recorder JSON configuration.
///
/// Schema `path` files are read, bounded, and checked against their declared
/// SHA-256 before being embedded. The loader never leaves external schema
/// references in the returned configuration.
/// @param path Recorder JSON configuration file.
/// @return Parsed configuration, or `NotFound`/`InvalidArgument`/`SecurityError`.
[[nodiscard]] core::Result<RecorderConfig>
load_recorder_config(const std::string& path) noexcept;

} // namespace nexigen::recorder
