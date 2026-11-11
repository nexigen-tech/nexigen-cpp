// Split from tracing_test.cpp: nexigen::tracing provider.

#include "tracing_test_support.hpp"

namespace {

void provider_lifecycle() {
    using Provider = tracing::TracerProvider<64, 32>;
    using BigProvider = tracing::TracerProvider<1024, 32>;

    // Compile-time shape pins: default footprint is the ~10 MiB one-block
    // budget (design D5) and the object is cache-line aligned (never stack).
    static_assert(sizeof(BigProvider) > 10 * 1024 * 1024);
    static_assert(sizeof(BigProvider) % 64 == 0);
    static_assert(alignof(BigProvider) == 64);
    static_assert(BigProvider::capacity() == 1024);
    static_assert(BigProvider::max_threads() == 32);

    tracing::NullTraceSink null_sink{};

    // Zero heap allocation during construction AND tracer creation: the
    // provider is placement-constructed into static storage.
    {
        alignas(Provider) static std::byte storage[sizeof(Provider)];
        const std::size_t arena_before = arena_used();
        const std::size_t heap_before = heap_in_use();
        auto* provider =
            new (static_cast<void*>(storage)) Provider(tracing::TracerProviderConfig{}, null_sink);
        NX_CHECK(arena_used() == arena_before);
        NX_CHECK(heap_in_use() == heap_before);

        const auto first = provider->get_tracer("engine");
        const auto again = provider->get_tracer("engine");
        const auto other = provider->get_tracer("physics");
        NX_CHECK(first.has_value() && again.has_value() && other.has_value());
        NX_CHECK(*again == *first);             // repeated name → same handle
        NX_CHECK(*other != *first);             // distinct name → distinct tracer
        NX_CHECK(arena_used() == arena_before); // tracer creation: zero alloc
        NX_CHECK(heap_in_use() == heap_before);

        // The diagnostic surface (spec "Handles and snapshots render
        // diagnostics").
        NX_CHECK((*first)->to_string() == "Tracer(scope=engine)");
        NX_CHECK(tracing::to_string(**first) == (*first)->to_string());
        NX_CHECK(std::format("{}", **first) == (*first)->to_string());
        NX_CHECK(provider->to_string().find("TracerProvider(") == 0U);
        NX_CHECK(std::format("{}", *provider) == provider->to_string());

        // initialize: idempotent, and the worker launches.
        NX_CHECK(provider->initialize().has_value());
        NX_CHECK(provider->initialize().has_value()); // second call: no-op Ok
        for (int spins = 0; spins < 2'000 && !provider->worker_started(); ++spins) {
            std::this_thread::sleep_for(std::chrono::microseconds(500));
        }
        NX_CHECK(provider->worker_started());
        NX_CHECK(provider->worker_start_code() == core::ErrorCode::Ok);

        // Control plane round-trips.
        NX_CHECK(provider->is_enabled());
        NX_CHECK(provider->sampling() == tracing::SamplingDecision::RecordAndSample);
        provider->set_enabled(false);
        NX_CHECK(!provider->is_enabled());
        provider->set_sampling(tracing::SamplingDecision::Drop);
        NX_CHECK(provider->sampling() == tracing::SamplingDecision::Drop);
        provider->set_enabled(true);
        provider->set_sampling(tracing::SamplingDecision::RecordAndSample);
        NX_CHECK(provider->is_enabled());
        NX_CHECK(provider->sampling() == tracing::SamplingDecision::RecordAndSample);

        // Dead handles (spec "Dead handles report NotInitialized"): the
        // provider's static storage stays mapped, destruction flips the
        // liveness flag, and handle use fails cleanly.
        const auto doomed = provider->get_tracer("doomed");
        NX_CHECK(doomed.has_value());
        std::destroy_at(provider);
        const auto stale = provider->get_tracer("engine");
        NX_CHECK(!stale.has_value());
        NX_CHECK(stale.error() == core::ErrorCode::NotInitialized);
        {
            auto span = (*doomed)->start_span("after-death", tracing::SpanKind::Internal);
            NX_CHECK(span.error() == core::ErrorCode::NotInitialized);
            NX_CHECK(!span.active());
            static_cast<void>(span.end()); // no-op, no crash
        }
    }
}

void provider_pool_exhaustion() {
    using Provider = tracing::TracerProvider<64, 32>;
    tracing::NullTraceSink null_sink{};

    // Pool exhaustion: BufferFull at the configured limit, existing usable.
    {
        alignas(Provider) static std::byte storage[sizeof(Provider)];
        tracing::TracerProviderConfig config{};
        config.max_tracers = 2;
        auto* provider = new (static_cast<void*>(storage)) Provider(config, null_sink);
        const auto a = provider->get_tracer("a");
        const auto b = provider->get_tracer("b");
        const auto c = provider->get_tracer("c");
        NX_CHECK(a.has_value() && b.has_value());
        NX_CHECK(!c.has_value() && c.error() == core::ErrorCode::BufferFull);
        // Previously created tracers remain usable.
        {
            auto span = (*a)->start_span("still-works", tracing::SpanKind::Server);
            NX_CHECK(span.active());
        }
        // Oversized scope names are rejected (bounded slot storage).
        const auto too_long = provider->get_tracer(
            "a-scope-name-that-is-longer-than-the-forty-seven-byte-bound-for-sure");
        NX_CHECK(!too_long.has_value() && too_long.error() == core::ErrorCode::InvalidArgument);
        std::destroy_at(provider);
    }
}

void provider_serialization() {
    // Config + DroppedStats trios (spec "Provider serialization surfaces").
    {
        tracing::TracerProviderConfig config{};
        config.worker_thread.set_cpus(core::CpuSet::from_core_mask(0x5));
        config.max_tracers = 8;
        config.max_spans_per_window = 1000;
        config.window_ns = 500'000'000;
        NX_CHECK(config.to_string() == tracing::to_string(config));
        NX_CHECK(std::format("{}", config) == config.to_string());
        const nlohmann::json j = config;
        tracing::TracerProviderConfig back{};
        nlohmann::from_json(j, back);
        NX_CHECK(back.worker_thread.cpus() == core::CpuSet::from_core_mask(0x5) &&
                 back.max_tracers == 8 && back.max_spans_per_window == 1000 &&
                 back.window_ns == 500'000'000);
        NX_CHECK(back.worker_poll_interval_us == 1000U); // default round-trips
        tracing::TracerProviderConfig tuned = config;
        tuned.worker_poll_interval_us = 7500U;
        NX_CHECK(tuned.to_string().find("worker_poll_interval_us=7500") != std::string::npos);
        tracing::TracerProviderConfig tuned_back{};
        nlohmann::from_json(nlohmann::json(tuned), tuned_back);
        NX_CHECK(tuned_back.worker_poll_interval_us == 7500U);
        // Hostile JSON leaves values unchanged.
        const tracing::TracerProviderConfig before = back;
        for (const nlohmann::json& bad :
             {nlohmann::json(1), nlohmann::json{1, 2}, nlohmann::json(nullptr),
              nlohmann::json{{"core_mask", "x"}, {"max_tracers", false}}}) {
            nlohmann::from_json(bad, back);
            NX_CHECK(std::memcmp(&back, &before, sizeof(back)) == 0);
        }

        const tracing::DroppedStats stats{3, 5, 7};
        NX_CHECK(stats.total() == 15);
        NX_CHECK(stats.to_string() == tracing::to_string(stats));
        NX_CHECK(std::format("{}", stats) == stats.to_string());
        const nlohmann::json j_stats = stats;
        tracing::DroppedStats stats_back{};
        nlohmann::from_json(j_stats, stats_back);
        NX_CHECK(stats_back.rate_limited == 3 && stats_back.buffer_full == 5 &&
                 stats_back.slot_exhausted == 7);
        const tracing::DroppedStats stats_before = stats_back;
        for (const nlohmann::json& bad :
             {nlohmann::json(1), nlohmann::json{1, 2}, nlohmann::json(nullptr)}) {
            nlohmann::from_json(bad, stats_back);
            NX_CHECK(std::memcmp(&stats_back, &stats_before, sizeof(stats_back)) == 0);
        }
    }
}

void provider_idle_wakeups() {
    using Provider = tracing::TracerProvider<64, 32>;

    // Worker idle behaviour: the poll interval bounds idle wake-ups, and a
    // flush is still answered at the next wake-up.
    {
        auto process_tids = [] {
            std::vector<long> tids;
            if (DIR* dir = ::opendir("/proc/self/task")) {
                while (const dirent* entry = ::readdir(dir)) {
                    if (entry->d_name[0] != '.') {
                        tids.push_back(std::strtol(entry->d_name, nullptr, 10));
                    }
                }
                ::closedir(dir);
            }
            return tids;
        };
        auto voluntary_switches = [](long tid) {
            std::ifstream status("/proc/self/task/" + std::to_string(tid) + "/status");
            std::string line;
            while (std::getline(status, line)) {
                if (line.rfind("voluntary_ctxt_switches:", 0) == 0) {
                    return std::strtol(line.c_str() + 24, nullptr, 10);
                }
            }
            return -1L;
        };
        auto idle_wakeups = [&](std::uint32_t poll_us, int window_ms) {
            tracing::NullTraceSink idle_sink{};
            tracing::TracerProviderConfig config{};
            config.worker_poll_interval_us = poll_us;
            alignas(Provider) static std::byte storage[sizeof(Provider)];
            const std::vector<long> before = process_tids();
            auto* provider = new (static_cast<void*>(storage)) Provider(config, idle_sink);
            NX_CHECK(provider->initialize().has_value());
            for (int spins = 0; spins < 2'000 && !provider->worker_started(); ++spins) {
                std::this_thread::sleep_for(std::chrono::microseconds(500));
            }
            long worker = -1;
            for (const long tid : process_tids()) {
                if (std::find(before.begin(), before.end(), tid) == before.end()) {
                    worker = tid;
                }
            }
            NX_CHECK(worker > 0);
            std::this_thread::sleep_for(std::chrono::milliseconds(100)); // backoff settles
            const long start = voluntary_switches(worker);
            std::this_thread::sleep_for(std::chrono::milliseconds(window_ms));
            const long end = voluntary_switches(worker);
            const auto t0 = std::chrono::steady_clock::now();
            NX_CHECK(provider->flush().has_value());
            NX_CHECK(std::chrono::steady_clock::now() - t0 < std::chrono::seconds(2));
            std::destroy_at(provider);
            return (start < 0 || end < 0) ? -1L : end - start;
        };
        const long dflt = idle_wakeups(1000U, 500);
        const long tuned = idle_wakeups(20'000U, 500);
        std::printf(
            "tracing worker idle: default poll -> %ld, 20 ms poll -> %ld wake-ups / 500 ms\n", dflt,
            tuned);
        NX_CHECK(dflt >= 0 && dflt < 1200); // former 200 us poll: ~2000
        NX_CHECK(tuned >= 0 && tuned < 120);
    }
}

void provider_affinity() {
    using Provider = tracing::TracerProvider<64, 32>;

    // Worker affinity round-trip (spec "Worker affinity matches the
    // configured mask"): a valid single allowed core, the mask the WORKER
    // observed, and the affinity at delivery time.
    {
        std::uint64_t allowed = 0ULL;
        NX_CHECK(tracing::detail::self_affinity(allowed).has_value());
        std::uint64_t core = 0ULL;
        for (std::size_t bit = 0; bit < 64U && core == 0ULL; ++bit) {
            if ((allowed & (1ULL << bit)) != 0ULL) {
                core = 1ULL << bit;
            }
        }
        RecordingSink sink{};
        tracing::TracerProviderConfig config{};
        config.worker_thread.set_cpus(core::CpuSet::from_core_mask(core));
        alignas(Provider) static std::byte storage[sizeof(Provider)];
        auto* provider = new (static_cast<void*>(storage)) Provider(config, sink);
        NX_CHECK(provider->initialize().has_value());
        const auto tracer = provider->get_tracer("affinity");
        NX_CHECK(tracer.has_value());
        { auto span = (*tracer)->start_span("pinned", tracing::SpanKind::Internal); }
        NX_CHECK(provider->flush().has_value());
        NX_CHECK(provider->worker_started());
        NX_CHECK(provider->worker_start_code() == core::ErrorCode::Ok);
        NX_CHECK(provider->worker_mask() == core); // the worker's own reading
        NX_CHECK((provider->worker_thread_report().applied() &
                  core::ThreadApplyReport::affinity_mask) != 0U);
        NX_CHECK(provider->worker_thread_report().failed() == 0U);
        NX_CHECK(sink.count == 1);
        NX_CHECK(sink.deliverer_tid.load(std::memory_order_relaxed) != 0U);
        NX_CHECK(sink.deliverer_tid.load(std::memory_order_relaxed) !=
                 static_cast<std::uint32_t>(::syscall(SYS_gettid))); // not main
        NX_CHECK(sink.deliverer_mask.load(std::memory_order_relaxed) ==
                 core); // pthread_getaffinity_np ON the worker at delivery
        sink.reset();
        std::destroy_at(provider);
    }

    // Zero-mask inheritance (spec: "a zero mask inherits OS placement"): the
    // worker's mask equals the creating thread's mask at construction time.
    {
        std::uint64_t main_mask = 0ULL;
        NX_CHECK(tracing::detail::self_affinity(main_mask).has_value());
        RecordingSink sink{};
        alignas(Provider) static std::byte storage[sizeof(Provider)];
        auto* provider =
            new (static_cast<void*>(storage)) Provider(tracing::TracerProviderConfig{}, sink);
        NX_CHECK(provider->initialize().has_value());
        for (int spins = 0; spins < 2'000 && !provider->worker_started(); ++spins) {
            std::this_thread::sleep_for(std::chrono::microseconds(500));
        }
        NX_CHECK(provider->worker_started());
        NX_CHECK(provider->worker_mask() == main_mask);
        std::destroy_at(provider);
    }
}

void provider_pin_failure() {
    using Provider = tracing::TracerProvider<64, 32>;

    // A failing mask surfaces SystemError in the startup snapshot WITHOUT
    // stopping the pipeline (spec "Pin failures are visible"): probe every
    // single-bit mask on a probe thread and use the first the MACHINE
    // rejects (an offline core or a cgroup-restricted box). On a fully
    // available 64-CPU machine no uint64 mask can fail — the assertion then
    // runs the identical code path on the positive side with a printed note
    // (only the input differs; the error handling is unreachable there).
    {
        std::atomic<std::uint64_t> failing{0ULL};
        {
            std::jthread probe([&failing] {
                for (std::size_t bit = 63; bit > 0; --bit) {
                    if (!tracing::detail::pin_self(1ULL << bit).has_value()) {
                        failing.store(1ULL << bit, std::memory_order_relaxed);
                        return;
                    }
                }
            });
            probe.join();
        }
        RecordingSink sink{};
        tracing::TracerProviderConfig config{};
        const std::uint64_t rejected = failing.load(std::memory_order_relaxed);
        config.worker_thread.set_cpus(
            core::CpuSet::from_core_mask(rejected != 0ULL ? rejected : 1ULL)); // pinnable fallback
        alignas(Provider) static std::byte storage[sizeof(Provider)];
        auto* provider = new (static_cast<void*>(storage)) Provider(config, sink);
        NX_CHECK(provider->initialize().has_value());
        const auto tracer = provider->get_tracer("pin-probe");
        NX_CHECK(tracer.has_value());
        { auto span = (*tracer)->start_span("still-records", tracing::SpanKind::Internal); }
        NX_CHECK(provider->flush().has_value());
        NX_CHECK(provider->worker_started());
        NX_CHECK(sink.count == 1); // spans still record and drain either way
        if (rejected != 0ULL) {
            std::printf("provider: failing mask 0x%llx rejected as expected\n",
                        static_cast<unsigned long long>(rejected));
            NX_CHECK(provider->worker_start_code() == core::ErrorCode::SystemError);
        } else {
            std::printf("provider: all 64 single-bit masks pin on this machine "
                        "(64 online CPUs) — SystemError snapshot assertion run "
                        "on the positive path only\n");
            NX_CHECK(provider->worker_start_code() == core::ErrorCode::Ok);
        }
        sink.reset();
        std::destroy_at(provider);
    }
}

void provider_stress_fifo() {
    // Per-shard FIFO and fair drain under 32-thread stress (spec): every
    // thread's spans drain in that thread's start order, no thread starved.
    // ShardCapacity 1024 ≥ per-thread span count: drops are impossible by
    // construction, so the assertions check ORDER, not queueing luck.
    {
        using StressProvider = tracing::TracerProvider<1024, 32>;
        RecordingSink sink{};
        alignas(StressProvider) static std::byte storage[sizeof(StressProvider)];
        auto* provider =
            new (static_cast<void*>(storage)) StressProvider(tracing::TracerProviderConfig{}, sink);
        NX_CHECK(provider->initialize().has_value());
        const auto tracer = provider->get_tracer("stress");
        NX_CHECK(tracer.has_value());

        constexpr int threads = 32;
        constexpr int per_thread = 300;
        std::atomic<int> ready{0};
        std::atomic<bool> go{false};
        std::vector<std::jthread> workers;
        workers.reserve(threads);
        for (int t = 0; t < threads; ++t) {
            workers.emplace_back([tracer = *tracer, &ready, &go] {
                ready.fetch_add(1, std::memory_order_relaxed);
                while (!go.load(std::memory_order_relaxed)) {
                    std::this_thread::yield();
                }
                for (int i = 0; i < per_thread; ++i) {
                    auto span = tracer->start_span("stress-span", tracing::SpanKind::Internal);
                    static_cast<void>(span.set_attribute("seq", i));
                }
            });
        }
        while (ready.load(std::memory_order_relaxed) < threads) {
            std::this_thread::yield();
        }
        go.store(true, std::memory_order_relaxed);
        for (auto& worker : workers) {
            worker.join();
        }
        NX_CHECK(provider->flush().has_value());

        NX_CHECK(sink.count == static_cast<std::size_t>(threads) * per_thread);
        NX_CHECK(sink.over_capacity.load(std::memory_order_relaxed) == 0);
        NX_CHECK(provider->dropped().total() == 0);
        // Per-thread FIFO: sequence attributes strictly increasing per tid;
        // every producer thread represented.
        std::unordered_map<std::uint32_t, int> last_seq;
        std::unordered_map<std::uint32_t, int> counts;
        for (std::size_t i = 0; i < sink.count; ++i) {
            const tracing::SpanRecord& record = sink.view()[i];
            NX_CHECK(record.attribute_count == 1);
            const std::int64_t seq = record.attributes[0].value.i64;
            const auto inserted = last_seq.emplace(record.thread_id, -1);
            NX_CHECK(seq > inserted.first->second); // in this thread's order
            inserted.first->second = static_cast<int>(seq);
            ++counts[record.thread_id];
        }
        NX_CHECK(last_seq.size() == static_cast<std::size_t>(threads)); // no starvation
        for (const auto& [tid, count] : counts) {
            NX_CHECK(count == per_thread);
            static_cast<void>(tid);
        }
        sink.reset();
        std::destroy_at(provider);
    }
}

void provider_thread_pool() {
    using Provider = tracing::TracerProvider<64, 32>;

    // The 33rd distinct thread drops visibly (spec "Threads beyond the pool
    // drop visibly"): sequential short-lived threads claim slots 0..31, the
    // 33rd lands in slot_exhausted.
    {
        RecordingSink sink{};
        alignas(Provider) static std::byte storage[sizeof(Provider)];
        auto* provider =
            new (static_cast<void*>(storage)) Provider(tracing::TracerProviderConfig{}, sink);
        NX_CHECK(provider->initialize().has_value());
        const auto tracer = provider->get_tracer("pool");
        NX_CHECK(tracer.has_value());
        for (int t = 0; t < 33; ++t) {
            std::jthread ephemeral([tracer = *tracer] {
                auto span = tracer->start_span("ephemeral", tracing::SpanKind::Consumer);
                static_cast<void>(span);
            });
            ephemeral.join();
        }
        NX_CHECK(provider->flush().has_value());
        NX_CHECK(provider->dropped().slot_exhausted == 1); // exactly the 33rd
        NX_CHECK(provider->dropped().buffer_full == 0);
        NX_CHECK(provider->dropped().rate_limited == 0);
        NX_CHECK(sink.count == 32); // the first 32 threads' spans drained
        sink.reset();
        std::destroy_at(provider);
    }
}

void provider_drop_counters() {
    using Provider = tracing::TracerProvider<64, 32>;

    // Exact drop counters, deterministic (never-initialized provider: no
    // consumer exists, so capacity accounting is exact):
    //   buffer_full: capacity 64 → 70 spans, 64 accepted, 6 dropped;
    //   destruction then drains exactly the 64 accepted records inline.
    // The spans run on a DEDICATED thread: this suite constructs many
    // providers on the main thread, and the 4-entry TLS slot map fills —
    // a 5th provider would re-claim a fresh slot (and fresh throttle
    // window) per span. One producer thread per provider is the deployment
    // model the design assumes.
    {
        RecordingSink sink{};
        alignas(Provider) static std::byte storage[sizeof(Provider)];
        auto* provider =
            new (static_cast<void*>(storage)) Provider(tracing::TracerProviderConfig{}, sink);
        const auto tracer = provider->get_tracer("fill");
        NX_CHECK(tracer.has_value());
        std::jthread producer([tracer = *tracer] {
            for (int i = 0; i < 70; ++i) {
                auto span = tracer->start_span("filler", tracing::SpanKind::Internal);
                NX_CHECK(span.active()); // the push drops, the span is still valid
                static_cast<void>(span.set_attribute("i", i));
            }
        });
        producer.join();
        NX_CHECK(provider->dropped().buffer_full == 6);
        NX_CHECK(provider->dropped().rate_limited == 0);
        NX_CHECK(provider->dropped().slot_exhausted == 0);
        std::destroy_at(provider);  // inline drain of the never-initialized
        NX_CHECK(sink.count == 64); // every ACCEPTED record delivered
        NX_CHECK(sink.started.load(std::memory_order_relaxed));
        NX_CHECK(sink.stopped.load(std::memory_order_relaxed));
        sink.reset();
    }
}

void provider_rate_budget() {
    using Provider = tracing::TracerProvider<64, 32>;

    // The span-rate budget counts rate_limited exactly (spec "The span-rate
    // budget counts rate_limited"): budget 2 per 1 s window, on a dedicated
    // producer thread (same TLS-slot reasoning as above).
    {
        RecordingSink sink{};
        tracing::TracerProviderConfig config{};
        config.max_spans_per_window = 2;
        config.window_ns = 1'000'000'000;
        alignas(Provider) static std::byte storage[sizeof(Provider)];
        auto* provider = new (static_cast<void*>(storage)) Provider(config, sink);
        NX_CHECK(provider->initialize().has_value());
        const auto tracer = provider->get_tracer("budget");
        NX_CHECK(tracer.has_value());
        std::atomic<int> active_count{0};
        std::jthread producer([tracer = *tracer, &active_count] {
            for (int i = 0; i < 3; ++i) {
                auto span = tracer->start_span("budgeted", tracing::SpanKind::Producer);
                NX_CHECK(span.active() == (i < 2)); // the third is rejected
                if (span.active()) {
                    active_count.fetch_add(1, std::memory_order_relaxed);
                }
            }
            auto span4 = tracer->start_span("rejected", tracing::SpanKind::Producer);
            NX_CHECK(!span4.active()); // rejected BEFORE queue interaction
        });
        producer.join();
        NX_CHECK(active_count.load(std::memory_order_relaxed) == 2);
        NX_CHECK(provider->dropped().rate_limited == 2);
        NX_CHECK(provider->dropped().buffer_full == 0);
        NX_CHECK(provider->flush().has_value());
        NX_CHECK(sink.count == 2);
        sink.reset();
        std::destroy_at(provider);
    }
}

void provider_flush() {
    using Provider = tracing::TracerProvider<64, 32>;

    // Flush barrier: returns only after drain durability, and sink errors
    // propagate (spec "Flush waits for durability").
    {
        // Durability: spans ended, flush, sink saw every record AND its own
        // flush() ran before flush() returned.
        RecordingSink sink{};
        alignas(Provider) static std::byte storage[sizeof(Provider)];
        auto* provider =
            new (static_cast<void*>(storage)) Provider(tracing::TracerProviderConfig{}, sink);
        NX_CHECK(provider->initialize().has_value());
        const auto tracer = provider->get_tracer("barrier");
        NX_CHECK(tracer.has_value());
        for (int i = 0; i < 10; ++i) {
            auto span = (*tracer)->start_span("durability", tracing::SpanKind::Internal);
            static_cast<void>(span);
        }
        NX_CHECK(provider->flush().has_value());
        NX_CHECK(sink.count == 10); // drained before flush() returned
        NX_CHECK(sink.flush_calls.load(std::memory_order_relaxed) >= 1);
        sink.reset();
        std::destroy_at(provider);

        // Error propagation: a failing sink flush surfaces as SystemError.
        FailingFlushSink failing{};
        alignas(Provider) static std::byte storage2[sizeof(Provider)];
        auto* provider2 =
            new (static_cast<void*>(storage2)) Provider(tracing::TracerProviderConfig{}, failing);
        NX_CHECK(provider2->initialize().has_value());
        const auto tracer2 = provider2->get_tracer("failing");
        NX_CHECK(tracer2.has_value());
        { auto span = (*tracer2)->start_span("x", tracing::SpanKind::Internal); }
        const core::Status flushed = provider2->flush();
        NX_CHECK(!flushed.has_value());
        NX_CHECK(flushed.error() == core::ErrorCode::SystemError);
        provider2->~TracerProvider();
    }
}

void provider_shutdown() {
    using Provider = tracing::TracerProvider<64, 32>;
    tracing::NullTraceSink null_sink{};

    // Shutdown drain: destruction loses no accepted record (spec "Destruction
    // does not lose accepted records") — no flush() call before destruction.
    {
        RecordingSink sink{};
        alignas(Provider) static std::byte storage[sizeof(Provider)];
        auto* provider =
            new (static_cast<void*>(storage)) Provider(tracing::TracerProviderConfig{}, sink);
        NX_CHECK(provider->initialize().has_value());
        const auto tracer = provider->get_tracer("shutdown");
        NX_CHECK(tracer.has_value());
        for (int i = 0; i < 25; ++i) {
            auto span = (*tracer)->start_span("last-words", tracing::SpanKind::Internal);
            static_cast<void>(span);
        }
        std::destroy_at(provider); // drain-on-join
        NX_CHECK(sink.count == 25);
        NX_CHECK(sink.stopped.load(std::memory_order_relaxed));
        sink.reset();
    }

    // flush() before initialize() reports NotInitialized (no worker exists).
    {
        alignas(Provider) static std::byte storage[sizeof(Provider)];
        auto* provider =
            new (static_cast<void*>(storage)) Provider(tracing::TracerProviderConfig{}, null_sink);
        const core::Status flushed = provider->flush();
        NX_CHECK(!flushed.has_value());
        NX_CHECK(flushed.error() == core::ErrorCode::NotInitialized);
        std::destroy_at(provider);
    }
}

void test_provider() {
    provider_lifecycle();
    provider_pool_exhaustion();
    provider_serialization();
    provider_idle_wakeups();
    provider_affinity();
    provider_pin_failure();
    provider_stress_fifo();
    provider_thread_pool();
    provider_drop_counters();
    provider_rate_budget();
    provider_flush();
    provider_shutdown();
}

} // namespace

void suite_provider() {
    run_suite("provider", test_provider);
}
