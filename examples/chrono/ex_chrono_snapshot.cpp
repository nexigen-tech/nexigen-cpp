// nexigen::chrono — shortest provider-to-file snapshot flow (change
// add-chrono-clock-service; task 6.1).
//
// Builds a JsonFileChronoSink, runs a ChronoProvider with a 10 ms background
// interval plus one manual snapshot, lets the worker deliver a few snapshots,
// and exits 0 after the provider destructor flushes the sink. The output file
// is a JSON-lines series of {stamp_ns, realtime_offset_ns} objects.
#include <chrono>
#include <cstdio>
#include <string>
#include <thread>

#include <nexigen/chrono/chrono_provider.hpp>
#include <nexigen/chrono/json_file_chrono_sink.hpp>

int main() {
    using namespace nexigen;

    const std::string path = "/tmp/ex_chrono_snapshot.jsonl";
    chrono::JsonFileChronoSink_config sink_config{path, /*append=*/false};
    chrono::JsonFileChronoSink sink{sink_config};
    if (!sink.is_open()) {
        std::fprintf(stderr, "failed to open %s\n", path.c_str());
        return 1;
    }

    chrono::ChronoProvider_config config{};
    config.collect_interval = std::chrono::milliseconds{10};
    {
        chrono::ChronoProvider provider{config, sink};
        if (!provider.snapshot_now()) {
            return 1;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds{50});
    } // provider destructor delivers a final snapshot and flushes the sink

    std::printf("snapshots written to %s\n", path.c_str());
    return 0;
}
