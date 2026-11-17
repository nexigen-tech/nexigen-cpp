// nexigen::recorder — built-in schema descriptors for platform frames.
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
[[nodiscard]] std::vector<SchemaDeclare> builtin_schemas();

} // namespace nexigen::recorder
