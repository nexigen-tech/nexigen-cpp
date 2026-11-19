// nexigen::tools — live time-base probe used by the chronod smoke test.
//
// Attaches to a running daemon's published segment and waits until the base
// reports Ready with a usable UTC calibration. Exits 0 only then, so the shell
// test can distinguish "daemon process started" from "base is actually
// consumable by an application".
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <string>
#include <thread>

#include <nexigen/chrono/time_base_client.hpp>
#include <nexigen/chrono/time_base_host_domain.hpp>
#include <nexigen/chrono/time_base_status.hpp>
#include <nexigen/chrono/timestamp.hpp>

namespace chrono = nexigen::chrono;

int main(int argc, char** argv) {
    if (argc < 2) {
        std::fprintf(stderr, "usage: %s <segment> [timeout_ms]\n", argv[0]);
        return 2;
    }
    const std::string segment = argv[1];
    const int timeout_ms = argc > 2 ? std::atoi(argv[2]) : 5000;
    if (timeout_ms <= 0) {
        return 2;
    }

    chrono::TimeBaseClientConfig config{};
    for (int waited = 0; waited < timeout_ms; waited += 10) {
        auto client = chrono::TimeBaseClient::attach(segment, config);
        if (client.has_value()) {
            if (client->status() == chrono::TimeBaseStatus::Ready &&
                client->coefficient(chrono::k_monotonic_raw_domain_id) != nullptr) {
                const auto now = chrono::Timestamp::now();
                if (now.has_value()) {
                    const auto utc =
                        client->utc_from_canonical(static_cast<std::int64_t>(now->nanoseconds()));
                    if (utc.has_value()) {
                        std::printf("ready boot=0x%016llx utc=%lld\n",
                                    static_cast<unsigned long long>(client->block().boot_token),
                                    static_cast<long long>(*utc));
                        return 0;
                    }
                }
            }
        }
        std::this_thread::sleep_for(std::chrono::milliseconds{10});
    }
    std::fprintf(stderr, "time base %s did not become Ready with UTC\n", segment.c_str());
    return 1;
}
