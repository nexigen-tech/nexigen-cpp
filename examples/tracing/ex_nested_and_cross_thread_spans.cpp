// nexigen::tracing runnable example: nested and cross-thread span hierarchies
// (spec tracing/examples "Nested and cross-thread spans example").
//
// Builds a provider with a console sink (the timeline prints to stdout as
// the background consumer drains), records nested same-thread spans, hands
// a LIVE span's context to a worker thread and starts the explicit-parent
// child there, then verifies every drained parent-child edge before exit.
//
// Self-verification: a local recording sink (the public TraceSink concept)
// captures the drained records; every expected edge must be present exactly.
// A deliberately broken check path (--fail-demo) exercises the failure
// branch: an error on stderr and a NON-ZERO exit.
//
// Self-contained: creates its own configuration and inputs; only the public
// tracing API. Run: ./ex_nested_and_cross_thread_spans [--fail-demo]
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <string_view>
#include <thread>
#include <vector>

#include <nexigen/core/result.hpp>
#include <nexigen/core/spsc_ring_buffer.hpp>
#include <nexigen/tracing/composite_trace_sink.hpp>
#include <nexigen/tracing/console_trace_sink.hpp>
#include <nexigen/tracing/tracer_provider.hpp>

namespace {

namespace tracing = nexigen::tracing;
namespace core = nexigen::core;

/// Records every drained span for the final verification pass (the public
/// TraceSink contract; runs on the provider's background consumer).
class Timeline_sink {
public:
    core::Status start() noexcept { return {}; }
    core::Status on_span(const tracing::SpanRecord& record) noexcept {
        records_.push_back(record);
        return {};
    }
    core::Status flush() noexcept { return {}; }
    void stop() noexcept {}
    std::string to_string() const {
        return "Timeline_sink(records=" + std::to_string(records_.size()) + ")";
    }

    /// The drained records (read after a flush barrier).
    [[nodiscard]] const std::vector<tracing::SpanRecord>& records() const noexcept {
        return records_;
    }

private:
    std::vector<tracing::SpanRecord> records_{};
};

/// One expected parent->child edge in the drained timeline.
struct Edge {
    std::string_view parent;
    std::string_view child;
};

/// Find the record for a span name (exactly one expected).
[[nodiscard]] const tracing::SpanRecord* find_span(
    const std::vector<tracing::SpanRecord>& records, std::string_view name) {
    const tracing::SpanRecord* found = nullptr;
    for (const tracing::SpanRecord& record : records) {
        if (record.name_view() == name) {
            if (found != nullptr) {
                return nullptr; // duplicate: not exactly one
            }
            found = &record;
        }
    }
    return found;
}

int report(const char* message) {
    std::fprintf(stderr, "ex_nested_and_cross_thread_spans: FAIL: %s\n", message);
    return 1;
}

} // namespace

int main(int argc, char** argv) {
    const bool fail_demo = argc > 1 && std::strcmp(argv[1], "--fail-demo") == 0;
    std::printf("== nexigen tracing: nested and cross-thread spans ==\n");

    // The provider owns ~10 MiB of pre-allocated shards (default template
    // parameters): static storage, never the stack (documented rule).
    static tracing::CompositeTraceSink<tracing::ConsoleTraceSink, Timeline_sink> sink{
        tracing::ConsoleTraceSink{1}, Timeline_sink{}};
    static tracing::TracerProvider<1024, 32> provider{
        tracing::TracerProvider_config{}, sink};

    if (!provider.initialize().has_value()) {
        return report("provider initialization failed");
    }
    const auto tracer = provider.get_tracer("example");
    if (!tracer.has_value()) {
        return report("tracer creation failed");
    }

    std::printf("-- recording: nested same-thread spans + a cross-thread child\n");
    {
        // Root: the whole simulation step.
        auto root = (*tracer)->start_span("simulator-step", tracing::SpanKind::Internal);
        static_cast<void>(root.set_attribute("step", 1));

        // Nested: the physics subsystem inside the step...
        {
            auto physics = (*tracer)->start_span("physics-step",
                                                 tracing::SpanKind::Server);
            static_cast<void>(physics.set_attribute("bodies", 4096));
            // ...and the I/O flush inside physics.
            {
                auto io = (*tracer)->start_span("io-flush",
                                                tracing::SpanKind::Producer);
                static_cast<void>(io.set_status(tracing::SpanStatus::Ok));
            }
        }

        // Cross-thread: hand the LIVE root's context to a worker; the child
        // there links through the explicit parent, and ITS child auto-links
        // to it locally on the worker thread.
        const tracing::SpanContext root_context = root.context();
        core::SpscRingBuffer<tracing::SpanContext, 4> handoff{};
        if (!handoff.try_push(root_context).has_value()) {
            return report("context handoff enqueue failed");
        }
        std::jthread worker([tracer = *tracer, &handoff] {
            tracing::SpanContext received{};
            for (int spins = 0; spins < 1'000; ++spins) {
                if (handoff.try_pop(received)) {
                    break;
                }
                std::this_thread::sleep_for(std::chrono::microseconds(100));
            }
            auto remote = tracer->start_span("physics-worker",
                                             tracing::SpanKind::Client, &received);
            static_cast<void>(remote.set_attribute("core", 3));
            {
                auto deeper = tracer->start_span("worker-batch",
                                                 tracing::SpanKind::Consumer);
                static_cast<void>(deeper);
            }
        });
        worker.join();
    }

    // Durability before verification: flush drains + flushes every sink.
    if (!provider.flush().has_value()) {
        return report("flush failed");
    }

    // -- self-verification: every expected parent-child edge must hold.
    const auto& records = sink.get<1>().records();
    std::printf("-- verifying %zu drained span(s)\n", records.size());

    const std::vector<Edge> edges{
        {"simulator-step", "physics-step"},
        {"physics-step", "io-flush"},
        {"simulator-step", "physics-worker"}, // the cross-thread child
        {"physics-worker", "worker-batch"},   // the worker-local grandchild
    };
    for (const Edge& edge : edges) {
        const tracing::SpanRecord* parent = find_span(records, edge.parent);
        const tracing::SpanRecord* child = find_span(records, edge.child);
        if (parent == nullptr || child == nullptr) {
            return report("expected span missing or duplicated in the timeline");
        }
        const bool edge_ok = fail_demo ? false : (child->parent_span_id ==
                                                  parent->span_id);
        if (!edge_ok) {
            std::fprintf(stderr,
                         "ex_nested_and_cross_thread_spans: FAIL: edge %.*s -> %.*s "
                         "broken (child parent %s)\n",
                         static_cast<int>(edge.parent.size()), edge.parent.data(),
                         static_cast<int>(edge.child.size()), edge.child.data(),
                         tracing::to_string(child->parent_span_id).c_str());
            return 1;
        }
        // The causal chain shares one trace id.
        if (child->trace_id != parent->trace_id) {
            return report("edge spans do not share a trace id");
        }
        std::printf("   ok: %-14s -> %-14s [trace %s]\n",
                    std::string(edge.parent).c_str(), std::string(edge.child).c_str(),
                    tracing::to_string(child->trace_id).c_str());
    }
    if (records.size() != edges.size() + 1) {
        // Every drained record must be part of the verified tree (5 spans).
        std::fprintf(stderr, "unexpected drained record count: %zu\n", records.size());
        return 1;
    }

    std::printf("== timeline verified: every parent-child edge holds ==\n");
    std::printf("tracing: %s\n", provider.to_string().c_str());
    return 0;
}
