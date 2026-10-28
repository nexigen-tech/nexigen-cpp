// nexigen::tracing runnable example: a Perfetto-loadable Chrome trace
// (spec tracing/examples "Perfetto Chrome trace example").
//
// Builds a provider with a ChromeTraceSink, records a workload of nested
// and concurrent spans, flushes and shuts down (the provider destructor
// drains every accepted span and writes the closing footer), prints the
// file's path with instructions to open it at ui.perfetto.dev, and sanity-
// checks the file before exit: it must exist, start with the trace header,
// end with the closing bracket, and contain every recorded event.
//
// Self-contained: creates its own configuration and output location; only
// the public tracing API. Run: ./ex_perfetto_chrome_trace
#include <chrono>
#include <cstdio>
#include <cstring>
#include <memory>
#include <new>
#include <string>
#include <thread>

#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>

#include <nexigen/core/result.hpp>
#include <nexigen/tracing/chrome_trace_sink.hpp>
#include <nexigen/tracing/tracer_provider.hpp>

namespace {

namespace tracing = nexigen::tracing;

constexpr const char* output_dir = "/tmp/nexigen-examples-tracing";

/// Read a whole file into a string (the sanity check is cold-path).
[[nodiscard]] std::string read_file(const std::string& path) {
    const int fd = ::open(path.c_str(), O_RDONLY | O_CLOEXEC);
    if (fd < 0) {
        return {};
    }
    std::string out;
    char buffer[4096];
    for (;;) {
        const ssize_t got = ::read(fd, buffer, sizeof(buffer));
        if (got <= 0) {
            break;
        }
        out.append(buffer, static_cast<std::size_t>(got));
    }
    static_cast<void>(::close(fd));
    return out;
}

int report(const char* message) {
    std::fprintf(stderr, "ex_perfetto_chrome_trace: FAIL: %s\n", message);
    return 1;
}

} // namespace

int main() {
    std::printf("== nexigen tracing: Perfetto Chrome trace ==\n");

    static_cast<void>(::mkdir(output_dir, 0755)); // exists is fine
    const std::string trace_path = std::string(output_dir) + "/trace_" +
                                   std::to_string(static_cast<long>(::getpid())) +
                                   ".json";

    // The provider owns ~10 MiB of pre-allocated shards: static STORAGE
    // (never the stack — documented rule) with an explicitly controlled
    // lifetime, so the shutdown drain runs exactly once below.
    tracing::ChromeTraceSink_config sink_config{};
    sink_config.path = trace_path;
    static tracing::ChromeTraceSink chrome_sink{sink_config};
    using Provider = tracing::TracerProvider<1024, 32>;
    alignas(Provider) static unsigned char provider_storage[sizeof(Provider)]{};
    auto* provider = new (static_cast<void*>(provider_storage))
        Provider(tracing::TracerProvider_config{}, chrome_sink);

    if (!provider->initialize().has_value()) {
        return report("provider initialization failed");
    }
    const auto tracer = provider->get_tracer("example");
    if (!tracer.has_value()) {
        return report("tracer creation failed");
    }

    std::printf("-- recording: nested and concurrent span workload\n");
    // Nested same-thread spans: a step containing a solve, the solve
    // containing a prepare.
    {
        auto step = (*tracer)->start_span("simulator-step",
                                          tracing::SpanKind::Internal);
        static_cast<void>(step.set_attribute("rate_hz", 1000));
        {
            auto solve = (*tracer)->start_span("solver-step",
                                               tracing::SpanKind::Server);
            static_cast<void>(solve.set_attribute("iterations", 512));
            {
                auto prepare = (*tracer)->start_span("prepare-matrices",
                                                     tracing::SpanKind::Internal);
                static_cast<void>(prepare);
            }
            std::this_thread::sleep_for(std::chrono::microseconds(300));
        }
    }

    // Concurrent spans on worker threads: two children of one live parent,
    // linked through explicit-parent contexts handed across threads.
    {
        auto parent = (*tracer)->start_span("concurrent-fanout",
                                            tracing::SpanKind::Internal);
        const tracing::SpanContext context = parent.context();
        std::jthread left([tracer = *tracer, &context] {
            auto child = tracer->start_span("worker-left",
                                            tracing::SpanKind::Client, &context);
            std::this_thread::sleep_for(std::chrono::microseconds(200));
            static_cast<void>(child);
        });
        std::jthread right([tracer = *tracer, &context] {
            auto child = tracer->start_span("worker-right",
                                            tracing::SpanKind::Client, &context);
            std::this_thread::sleep_for(std::chrono::microseconds(400));
            static_cast<void>(child);
        });
        left.join();
        right.join();
    }

    // Durability: flush drains everything accepted so far to the file.
    if (!provider->flush().has_value()) {
        return report("flush failed");
    }

    // Shutdown drain: the destructor stops the worker, delivers every
    // accepted record, and the sink's stop() writes the closing bracket.
    std::printf("-- shutting down (drain-on-join writes the trace footer)\n");
    std::destroy_at(provider);

    // -- file sanity check before exit.
    const std::string content = read_file(trace_path);
    if (content.empty()) {
        return report("trace file missing or empty");
    }
    if (content.find("{\"traceEvents\":[") != 0U) {
        return report("trace file does not start with the Chrome trace header");
    }
    if (content.rfind("]}") != content.size() - 3U) {
        return report("trace file does not end with the closing bracket");
    }
    // One complete event per recorded span: 3 nested + 3 fanout = 6.
    std::size_t events = 0;
    for (std::size_t at = content.find("\"ph\":\"X\"", 0U); at != std::string::npos;
         at = content.find("\"ph\":\"X\"", at + 1U)) {
        ++events;
    }
    if (events != 6U) {
        std::fprintf(stderr, "expected 6 complete events, found %zu\n", events);
        return 1;
    }

    std::printf("== trace written: %s ==\n", trace_path.c_str());
    std::printf("Open it at https://ui.perfetto.dev (Upload trace file) to\n"
                "visualize the execution timeline: nested spans under\n"
                "'simulator-step' and the concurrent 'worker-*' pair under\n"
                "'concurrent-fanout', each carrying its ids and attributes.\n");
    return 0;
}
