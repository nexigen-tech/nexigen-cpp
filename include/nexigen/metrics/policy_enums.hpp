/// @file nexigen::metrics — policy-vocabulary umbrella (compatibility surface).
/// @brief Compatibility umbrella for the whole metrics policy vocabulary.
///
/// Includes every per-entity header of the policy vocabulary, so a consumer
/// that needs all policies (access, concurrency, metric kind and reset) gets
/// them with one include. Each enum keeps its own header, its own
/// `EnumTraits` vocabulary table and its own documentation; this file only
/// forwards.
///
/// @note Spec `metrics/policy-enums`, requirement "Policy enum catalogue" —
///       the four policy enumerators and their strictly increasing order.
///
/// @note Entity headers forwarded here:
///       - `access_policy.hpp` — how a metric may be accessed concurrently.
///       - `concurrency_policy.hpp` — single-threaded vs multi-threaded storage.
///       - `metric_kind.hpp` — the metric kind vocabulary.
///       - `reset_policy.hpp` — how a metric resets between epochs.
///
/// @note Spec `metrics/policy-enums`, requirement "Constexpr policy names" —
///       every policy has a compile-time lowercase name over static storage.
///
/// @note Spec `metrics/policy-enums`, requirement "Non-throwing policy JSON
///       serialization" — each policy has a non-throwing ADL JSON pair.
///
/// @note Prefer including the specific entity header when a consumer needs
///       one policy only: the umbrella pulls all four.
#pragma once

#include <nexigen/metrics/access_policy.hpp>
#include <nexigen/metrics/concurrency_policy.hpp>
#include <nexigen/metrics/metric_kind.hpp>
#include <nexigen/metrics/reset_policy.hpp>
