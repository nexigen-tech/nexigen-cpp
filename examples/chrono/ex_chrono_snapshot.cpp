// nexigen::chrono — shortest provider-to-file snapshot flow (change
// add-chrono-clock-service; task 6.1; updated to the threadless Provider +
// ChronoWorker split by add-clock-authority-core).
//
// Builds a JsonFileSink, wraps a Provider in a ChronoWorker with a 10 ms
// background interval plus one manual snapshot, lets the worker deliver a few
// snapshots, and exits 0 after the worker destructor delivers a final snapshot
// and flushes the sink. The output file is a JSON-lines series of
// {stamp_ns, realtime_offset_ns} objects.
#include <chrono>
#include <cstdio>
#include <string>
#include <thread>

#include <unistd.h>

#include <nexigen/chrono/provider.hpp>
#include <nexigen/chrono/worker.hpp>
#include <nexigen/chrono/json_file_sink.hpp>

int main() {
    using namespace nexigen;

    const std::string path =
        "/tmp/ex_chrono_snapshot-" + std::to_string(static_cast<long>(::getpid())) + ".jsonl";
    chrono::JsonFileSinkConfig sink_config{path, /*append=*/false};
    chrono::JsonFileSink sink{sink_config};
    if (!sink.is_open()) {
        std::fprintf(stderr, "failed to open %s\n", path.c_str());
        return 1;
    }

    chrono::ProviderConfig config{};
    config.collect_interval = std::chrono::milliseconds{10};
    {
        chrono::Provider<> provider{config, sink};
        chrono::ChronoWorker worker{provider, config};
        if (!worker.start()) {
            return 1;
        }
        if (!provider.snapshot_now()) {
            return 1;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds{50});
    } // worker destructor delivers a final snapshot and flushes the sink

    std::printf("snapshots written to %s\n", path.c_str());
    return 0;
}
