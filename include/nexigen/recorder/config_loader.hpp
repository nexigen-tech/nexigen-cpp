/// @file config_loader.hpp
/// @brief Cold-path loader for a recorder JSON configuration.
///
/// @note Spec `recorder/raw-storage`, requirement "Embedded schema and channel
///       registry"; config-provided schemas are read once, bounded, verified,
///       and embedded so the running session has no external references.
#pragma once

#include <string>
#include <vector>

#include <nexigen/core/result.hpp>
#include <nexigen/recorder/channel_declare.hpp>
#include <nexigen/recorder/schema_declare.hpp>
#include <nexigen/recorder/storage_settings.hpp>

namespace nexigen::recorder {

/// @brief Loaded recorder configuration with embedded schema bytes.
///
/// @note The returned `schemas` contain the full descriptor bytes; `channels`
///       reference them by id. No path, URL, or digest-only reference survives.
struct RecorderConfig {
    StorageSettings storage{};          ///< Effective session storage settings.
    std::vector<SchemaDeclare> schemas{}; ///< Embedded schema descriptors.
    std::vector<ChannelDeclare> channels{}; ///< Declared channels.
};

/// @brief Load and validate a recorder JSON configuration.
///
/// Schema `path` files are read, bounded to `kMaxSchemaBytes`, checked against
/// their declared SHA-256, and stored as full descriptor bytes. The loader never
/// leaves external schema references in the returned configuration and rejects
/// encodings outside the supported whitelist.
/// @param path Recorder JSON configuration file.
/// @return Parsed configuration, or `NotFound`/`InvalidArgument`/`SecurityError`.
/// @note Spec `recorder/raw-storage`, requirement "Embedded schema and channel
///       registry"; this is a cold-path operation and may allocate.
[[nodiscard]] core::Result<RecorderConfig>
load_recorder_config(const std::string& path) noexcept;

} // namespace nexigen::recorder
