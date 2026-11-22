// Negative-compile probe for the InfrastructurePolicy contract.
//
// DELIBERATELY NOT part of any build target: tests/CMakeLists.txt registers it
// through tests/core/negative_compile.sh, which requires the compile to fail
// with a static-assert diagnostic. BadPolicy has no TraceSink and no hooks.
#include <nexigen/app/infrastructure.hpp>
#include <nexigen/logging/null_logger.hpp>
#include <nexigen/metrics/null_metric_sink.hpp>

struct BadPolicy {
    using Logger = nexigen::logging::NullLogger;
    using MetricsSink = nexigen::metrics::NullMetricSink;
    // TraceSink and every emplacement hook are intentionally absent.
};

static_assert(nexigen::app::InfrastructurePolicy<BadPolicy>);
