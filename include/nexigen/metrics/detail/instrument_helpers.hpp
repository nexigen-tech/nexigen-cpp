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

/// Placement-destruct an instrument (the type-erased destroy hook). The
/// instrument's destructor is public, so no friendship is required.
template <class Instrument>
void destroy_instrument(void* object) noexcept {
    static_cast<Instrument*>(object)->~Instrument();
}

/// Build the classification + identity prefix of a harvested record. The
/// kind-specific payload is filled separately by the instrument.
inline MetricRecord make_record(MetricKind kind, ConcurrencyPolicy concurrency,
                                AccessPolicy access, ResetPolicy reset,
                                const MetricIdentity* identity) {
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

/// Append the "kind/concurrency/access/reset" classification to a diagnostic.
inline void append_classification(std::string& out, MetricKind kind,
                                  ConcurrencyPolicy concurrency, AccessPolicy access,
                                  ResetPolicy reset) {
    out += std::string(metrics::to_string(kind));
    out += ", concurrency=" + std::string(metrics::to_string(concurrency));
    out += ", access=" + std::string(metrics::to_string(access));
    out += ", reset=" + std::string(metrics::to_string(reset));
}

/// The "kind/concurrency/access/reset" classification as a JSON object.
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
