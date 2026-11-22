// nexigen::examples — quickstart via app::App<Policy> (change
// add-app-runtime-facade; spec examples/app-quickstart).
//
// Receives one Signal on Nexigen/Example/Quickstart/In, doubles it, publishes
// the result on .../out, and records a log, metric and span. The embedded time
// base makes the example runnable without nexigen-chronod or the Execution
// Manager; --app-id is required for telemetry and event attribution.
#include <cstdint>

#include <nexigen/app/app.hpp>
#include <nexigen/app/policies/iceoryx2_policy.hpp>
#include <nexigen/core/result.hpp>
#include <nexigen/tracing/scoped_span.hpp>
#include <nexigen/tracing/span_kind.hpp>
#include <nexigen/tracing/span_status.hpp>

#include "quickstart_signal.hpp"

namespace app = nexigen::app;
namespace tracing = nexigen::tracing;

namespace {

using Signal = nexigen::examples::quickstart::Signal;
using Policy = app::policies::Iceoryx2Policy<'Q', 'S', 'T', 'R'>;

} // namespace

int main(int argc, char** argv) {
    auto created = app::App<Policy>::create(argc, argv, "quickstart", app::TimeBaseMode::Embedded);
    if (!created) return 1;
    if (!*created) return 0;
    auto app = std::move(*created);

    auto processed = app->counter("signals_processed");
    auto output = app->publish<Signal>("Nexigen/Example/Quickstart/Out");
    auto logger = app->logger();
    auto tracer = app->tracer_for("quickstart");
    if (!logger || !tracer) return 1;

    app->subscribe<Signal>("Nexigen/Example/Quickstart/In", [&](const Signal& in) {
        auto span = (*tracer)->start_span("process-signal", tracing::SpanKind::Internal);
        static_cast<void>(output->publish_now(Signal{in.sequence, in.value * 2.0}, 0x2001U));
        processed->inc();
        (*logger)->info("seq={} in={} out={}", in.sequence, in.value, in.value * 2.0);
        static_cast<void>(span.set_status(tracing::SpanStatus::Ok));
    });

    const auto reason = app->run();
    if (!reason) return (static_cast<void>(app->shutdown()), 1);
    const auto status = app->shutdown();
    return status ? app::exit_code(*reason) : 1;
}
