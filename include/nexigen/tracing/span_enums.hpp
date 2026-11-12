/// @file nexigen::tracing — span-vocabulary umbrella (compatibility surface).
/// @brief Compatibility umbrella for the whole tracing span vocabulary.
///
/// Includes every per-entity header of the span vocabulary, so a consumer that
/// needs all three (sampling decision, span kind and span status) gets them
/// with one include. Each enum keeps its own header, its own `EnumTraits`
/// vocabulary table and its own documentation; this file only forwards.
///
/// @note Spec `tracing/span-enums`, requirement "Span enum catalogue" — the
///       three span enums and their vocabulary tables.
///
/// @note Entity headers forwarded here:
///       - `sampling_decision.hpp` — whether a span is sampled in or dropped.
///       - `span_kind.hpp` — the kind vocabulary of a span.
///       - `span_status.hpp` — the outcome vocabulary of a span.
///
/// @note Spec `tracing/span-enums`, requirement "Constexpr span enum names" —
///       every span enum has a compile-time lowercase name over static storage.
///
/// @note Spec `tracing/span-enums`, requirement "Non-throwing span enum JSON
///       serialization" — each span enum has a non-throwing ADL JSON pair.
///
/// @note Prefer including the specific entity header when a consumer needs
///       one enum only: the umbrella pulls all three.
#pragma once

#include <nexigen/tracing/sampling_decision.hpp>
#include <nexigen/tracing/span_kind.hpp>
#include <nexigen/tracing/span_status.hpp>
