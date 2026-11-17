/// @file builtin_schemas.hpp
/// @brief Built-in schema descriptors for platform frames.
///
/// @note Spec `recorder/raw-storage`, requirement "Embedded schema and channel
///       registry"; the descriptors are embedded like any config-provided
///       schema, so the recorder does not link logging/metrics/tracing.
#pragma once

#include <vector>

#include <nexigen/recorder/schema_declare.hpp>

namespace nexigen::recorder {

/// @brief Schemas for the platform frame types and video metadata records.
///
/// The descriptors are self-contained JSON layout documents; the recorder can
/// embed them without linking logging/metrics/tracing. Tests pin the layouts
/// against the actual frame headers.
/// @return One descriptor per built-in platform record type.
/// @note Spec `recorder/raw-storage`, scenario "A segment is self-describing";
///       spec `recorder/export`, scenario "Built-in frame decodes".
[[nodiscard]] std::vector<SchemaDeclare> builtin_schemas();

} // namespace nexigen::recorder
