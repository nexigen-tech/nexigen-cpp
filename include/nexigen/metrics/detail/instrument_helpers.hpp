// nexigen::metrics — shared instrument boilerplate (detail).
//
// Every instrument repeats the same classification scaffolding: the
// type-erased destroy hook, the kind/concurrency/access/reset + identity
// prefix of a harvested record, and the kind/concurrency/access/reset
// classification in to_string/to_json. These free helpers centralize that
// boilerplate so it lives in one place.
#pragma once

#include <string>

#include <nlohmann/json.hpp>

#include <nexigen/metrics/metric_snapshot.hpp>
#include <nexigen/metrics/policy_enums.hpp>

namespace nexigen::metrics::detail {

/// @brief Placement-destruct an instrument through its type-erased destroy hook.
///
/// The instrument's destructor is public, so no friendship is required; the
/// registry stores this instantiation next to the erased object pointer.
///
/// @tparam Instrument Concrete instrument type to destroy.
/// @param object Pointer to the fully constructed instrument; must be non-null
///        and must point to an object of exactly `Instrument`.
template <class Instrument> void destroy_instrument(void* object) noexcept {
    static_cast<Instrument*>(object)->~Instrument();
}

/// @brief Build the classification and identity prefix of a harvested record.
///
/// The kind-specific payload (scalar, statistics, histogram, state) is filled
/// separately by the instrument; this helper only stamps the policy enums and
/// copies the identity.
///
/// @param kind Instrument kind stored in the record.
/// @param concurrency Concurrency policy the instrument was created with.
/// @param access Access policy the instrument was created with.
/// @param reset Reset policy the instrument was created with.
/// @param identity Meter/metric identity to copy, or `nullptr` to leave the
///        record identity zeroed.
/// @return A record with classification and identity set and the payload zeroed.
inline MetricRecord make_record(MetricKind kind, ConcurrencyPolicy concurrency, AccessPolicy access,
                                ResetPolicy reset, const MetricIdentity* identity) {
    MetricRecord record{};
    record.kind = kind;
    record.concurrency = concurrency;
    record.access = access;
    record.reset = reset;
    if (identity != nullptr) {
        record.identity = *identity;
    }
    return record;
}

/// @brief Append the `kind/concurrency/access/reset` classification to a diagnostic.
///
/// @param out Destination string, appended in place.
/// @param kind Instrument kind to render.
/// @param concurrency Concurrency policy to render.
/// @param access Access policy to render.
/// @param reset Reset policy to render.
/// @note Uses the constexpr enum `to_string` helpers (project mandate 9);
///       allocates as part of the diagnostic string, so cold path only.
inline void append_classification(std::string& out, MetricKind kind, ConcurrencyPolicy concurrency,
                                  AccessPolicy access, ResetPolicy reset) {
    out += std::string(metrics::to_string(kind));
    out += ", concurrency=" + std::string(metrics::to_string(concurrency));
    out += ", access=" + std::string(metrics::to_string(access));
    out += ", reset=" + std::string(metrics::to_string(reset));
}

/// @brief Render the `kind/concurrency/access/reset` classification as JSON.
///
/// @param kind Instrument kind to render.
/// @param concurrency Concurrency policy to render.
/// @param access Access policy to render.
/// @param reset Reset policy to render.
/// @return Object with the four canonical name strings.
/// @note Project mandate 10 (JSON); allocates, cold path only.
inline nlohmann::json classification_json(MetricKind kind, ConcurrencyPolicy concurrency,
                                          AccessPolicy access, ResetPolicy reset) {
    return nlohmann::json{
        {"kind", std::string(metrics::to_string(kind))},
        {"concurrency", std::string(metrics::to_string(concurrency))},
        {"access", std::string(metrics::to_string(access))},
        {"reset", std::string(metrics::to_string(reset))},
    };
}

} // namespace nexigen::metrics::detail
