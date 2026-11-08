// Split from logging_test.cpp: nexigen::logging AsyncWorker drop visibility and shutdown.

#include "logging_test_support.hpp"

namespace {

// --- flush_summary race suite (change fix-p0-critical-defects): the owner
// thread floods its own shard (buffer-full drops pile up) while three
// application threads hammer flush() concurrently — every summary comes from
// the worker (consumer) thread, so the SPSC "only the owner pushes" invariant
// holds while owners keep emitting. TSan (task 4.2) is the verdict; these
// assertions only check the final, barrier-ordered visible state.
char g_flush_race_arena[16384]{};

void test_flush_summary_race() {
    using P = detail::Producer<4, 128, 4>;
    using RecordT = LogRecord<128>;
    P prod{};
    NX_CHECK(prod.initialize().has_value());

    detail::AsyncWorker<P, LinesSink<RecordT>> worker{};
    NX_CHECK(worker.start(prod, core::ThreadSpec{}, g_flush_race_arena, sizeof g_flush_race_arena)
                 .has_value());
    for (int i = 0; i < 5000 && !worker.writer_started(); ++i) {
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    NX_CHECK(worker.writer_started());

    std::atomic<bool> stop_flag{false};
    std::jthread owner([&] {
        // Waves of 8 emits: the first 4 land in the empty ring, the rest hit
        // the full ring and are counted as buffer-full drops while flush
        // loops hammer the barrier.
        for (int wave = 0; !stop_flag.load(std::memory_order_relaxed); ++wave) {
            for (int i = 0; i < 8; ++i) {
                prod.emit(LogLevel::Info, "flood {} {}", wave, i);
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
    });
    auto flusher = [&] {
        while (!stop_flag.load(std::memory_order_relaxed)) {
            static_cast<void>(worker.flush()); // barriers complete or escape-hatch
        }
    };
    std::jthread f1(flusher);
    std::jthread f2(flusher);
    std::jthread f3(flusher);
    std::this_thread::sleep_for(std::chrono::milliseconds(150));
    stop_flag.store(true, std::memory_order_release);
    owner.join();
    f1.join();
    f2.join();
    f3.join();

    NX_CHECK(worker.flush().has_value()); // final barrier: drained + synced
    // The race kept buffer_full pending through whole waves, so the worker
    // synthesized summaries into the shared arena — and no thread ever
    // pushed them into a foreign ring (TSan-clean under task 4.2).
    const LinesSink<RecordT> probe(g_flush_race_arena, sizeof g_flush_race_arena);
    NX_CHECK(probe.count("Dropped") >= 1);
    NX_CHECK(probe.find("buffer-full: "));
    NX_CHECK(probe.find("flood")); // user records flowed from the flooded owner
    std::printf("flush_summary_race: concurrent flush vs flooding owner verified\n");
}

// --- shutdown drain race (analysis #6 test-gap "destruction-while-running"):
// the AsyncWorker is destroyed (jthread stop-request → join → final drain)
// while producer threads are STILL emitting through the pool. The producer
// workload is finite (bounded per-lane counts), so the shutdown drain stays
// finite even with emitters racing it, and the destruction order (worker
// scope exits BEFORE the producer pool's jthreads are joined) makes the
// stop-under-load real. Consumers reference only the alive producer object;
// the worker never outlives its own join — TSan is the true verdict, the
// assertions below check the conservation of accepted + dropped counts.

void test_shutdown_drain_race() {
    using P = detail::Producer<4, 128, 4>;
    using RecordT = LogRecord<128>;
    P prod{};
    NX_CHECK(prod.initialize().has_value());

    constexpr int producers = 3;
    constexpr int per_producer = 12'000;
    std::atomic<int> finished{0};
    std::size_t drained = 0;
    struct CountSink {
        std::atomic<std::uint64_t>* received{nullptr};
        [[nodiscard]] core::Status start() noexcept { return {}; }
        void handle(RecordT&) noexcept { received->fetch_add(1, std::memory_order_relaxed); }
        [[nodiscard]] core::Status synchronize() noexcept { return {}; }
        void stop() noexcept {}
    };
    std::atomic<std::uint64_t> sink_received{0};

    // Declared OUTSIDE the worker block on purpose: the producers outlive
    // the worker's scope exit, so the destructor's stop-request/join/
    // final-drain sequence happens while emitters still push.
    std::vector<std::jthread> pool;
    pool.reserve(producers);
    {
        detail::AsyncWorker<P, CountSink> worker{};
        NX_CHECK(worker.start(prod, core::ThreadSpec{}, &sink_received).has_value());
        for (int i = 0; i < 5000 && !worker.writer_started(); ++i) {
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
        NX_CHECK(worker.writer_started());

        for (int p = 0; p < producers; ++p) {
            pool.emplace_back([&, p] {
                for (int i = 0; i < per_producer; ++i) {
                    prod.emit(LogLevel::Info, "shutdown-race {} {}", p, i);
                }
                finished.fetch_add(1, std::memory_order_relaxed);
            });
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
        // Overlap window: let the producers saturate the small shards while
        // the main thread leaves the scope — the destructor races real
        // emission.
        while (finished.load(std::memory_order_relaxed) < 1) {
            std::this_thread::yield();
        }
    } // AsyncWorker destroyed HERE, producers still hammering
    pool.clear(); // join remaining producers (all finite)

    // All producers must have completed (the join inside the destructor is
    // not allowed to hang: their workload is finite by construction).
    NX_CHECK(finished.load() == producers);

    // After the worker is gone the main thread is the only consumer: the
    // queue tail (records emitted after the final drain observed empty)
    // drains synchronously and without loss beyond counted drops.
    RecordT rec{};
    while (prod.pop(rec)) {
        ++drained;
    }

    // Conservation of every emitted record: worker-delivered + main-tail
    // drained + counted buffer-full drops == total emitted. Nothing else
    // may appear beside those three outcomes.
    const std::uint64_t total = static_cast<std::uint64_t>(producers * per_producer);
    NX_CHECK(sink_received.load() + drained + prod.dropped().buffer_full == total);
    NX_CHECK(sink_received.load() >= 1);
    NX_CHECK(prod.dropped().rate_limited == 0); // throttle config untouched

    std::printf("shutdown_drain_race: destructor join+drain under load "
                "verified\n");
}

// --- drop_visibility suite (change ensure-drop-visibility, reworked by
// fix-p0-critical-defects): worker-synthesized flush-time summary, all four
// causes in summary, counters reset after a summary carries them.
void test_drop_visibility() {
    // Flush-time summary with buffer_full, CROSS-THREAD: the owner thread
    // filled its shard while the flushing thread is a different one. The
    // worker synthesizes the summary straight into the sink (no ring push);
    // exact counts; reported cells reset after the summary.
    {
        using P = detail::Producer<4, 128, 1>;
        static P prod{};
        NX_CHECK(prod.initialize().has_value());
        std::jthread t1([&] {
            // Fill the ring (4 slots), then 2 more are dropped.
            for (int i = 0; i < 6; ++i) {
                prod.emit(LogLevel::Info, "v {}", static_cast<std::uint64_t>(i));
            }
        });
        t1.join();
        NX_CHECK(prod.dropped().buffer_full == 2);

        char arena[4096]{};
        detail::AsyncWorker<P, LinesSink<LogRecord<128>>> worker{};
        NX_CHECK(worker.start(prod, core::ThreadSpec{}, arena, sizeof arena).has_value());
        for (int i = 0; i < 5000 && !worker.writer_started(); ++i) {
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
        NX_CHECK(worker.writer_started());
        NX_CHECK(worker.flush().has_value()); // synthesize + drain + sync
        NX_CHECK(prod.ring_empty());          // all accepted records handled

        const LinesSink<LogRecord<128>> probe(arena, sizeof arena);
        NX_CHECK(probe.count("v ") == 4);                  // 4 user records
        NX_CHECK(probe.count("Dropped 2 record(s)") == 1); // exactly one summary
        NX_CHECK(probe.find("buffer-full: 2"));
        NX_CHECK(probe.find("slot-exhausted: 0"));
        NX_CHECK(probe.find("throttled: 0"));
        NX_CHECK(prod.dropped().buffer_full == 0); // reported cells reset
        // A summary-less second flush does not re-report the same counts.
        NX_CHECK(worker.flush().has_value());
        NX_CHECK(probe.count("Dropped 2 record(s)") == 1);
    }

    // Reported cells reset after synthesis; fresh drops accumulate into the
    // next summary (the previous report is not double-counted).
    {
        static detail::Producer<2, 128, 1> prod2{};
        NX_CHECK(prod2.initialize().has_value());
        prod2.emit(LogLevel::Info, "a");
        prod2.emit(LogLevel::Info, "b");
        NX_CHECK(prod2.dropped().buffer_full == 0);
        prod2.emit(LogLevel::Info, "c");
        prod2.emit(LogLevel::Info, "d");
        NX_CHECK(prod2.dropped().buffer_full == 2);

        char arena[4096]{};
        detail::AsyncWorker<detail::Producer<2, 128, 1>, LinesSink<LogRecord<128>>> worker{};
        NX_CHECK(worker.start(prod2, core::ThreadSpec{}, arena, sizeof arena).has_value());
        for (int i = 0; i < 5000 && !worker.writer_started(); ++i) {
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
        NX_CHECK(worker.writer_started());
        NX_CHECK(worker.flush().has_value());
        const LinesSink<LogRecord<128>> probe(arena, sizeof arena);
        NX_CHECK(probe.count("Dropped 2 record(s)") == 1);
        NX_CHECK(prod2.dropped().buffer_full == 0);

        // The worker keeps draining, so burst extra records through the
        // small ring (main thread racing the worker): TSan scheduling can
        // free slots mid-loop, so the staleness bound is a range, not an
        // exact 2 — whatever pending buffer-full count accumulated (0..~30)
        // must surface as exactly ONE additional summary at the barrier
        // below and reset.
        for (int i = 4; prod2.dropped().buffer_full < 2 && i < 32; ++i) {
            prod2.emit(LogLevel::Info, "extra {}", i);
        }
        const std::uint64_t pending =
            prod2.dropped().buffer_full; // old 2 drops were already reported
        NX_CHECK(worker.flush().has_value());
        if (pending != 0) {
            // One new summary carries exactly one cause group: "Dropped N
            // record(s)" prefixed line; no dual-counting ("Dropped 4") may
            // appear because the first summary's cells were reset.
            NX_CHECK(probe.count("Dropped") == 2);
            NX_CHECK(probe.count("Dropped 4") == 0);
        }
        NX_CHECK(prod2.dropped().buffer_full == 0);
    }

    // Summary includes slot_exhausted: register beyond the pool, then flush.
    {
        static detail::Producer<4, 128, 2> prod3{};
        NX_CHECK(prod3.initialize().has_value());
        prod3.emit(LogLevel::Info, "slot0");
        std::jthread t2([&] { prod3.emit(LogLevel::Info, "slot1"); });
        t2.join();
        // Third thread: slot exhausted (global counter stays cumulative).
        std::jthread t3([&] { prod3.emit(LogLevel::Info, "overflow"); });
        t3.join();
        NX_CHECK(prod3.dropped().slot_exhausted == 1);

        char arena[4096]{};
        detail::AsyncWorker<detail::Producer<4, 128, 2>, LinesSink<LogRecord<128>>> worker{};
        NX_CHECK(worker.start(prod3, core::ThreadSpec{}, arena, sizeof arena).has_value());
        for (int i = 0; i < 5000 && !worker.writer_started(); ++i) {
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
        NX_CHECK(worker.writer_started());
        NX_CHECK(worker.flush().has_value());
        const LinesSink<LogRecord<128>> probe(arena, sizeof arena);
        NX_CHECK(probe.count("Dropped") == 1);
        NX_CHECK(probe.find("slot-exhausted: 1"));
    }

    std::printf("drop_visibility: worker-synthesized summary, all causes, resets verified\n");
}

} // namespace

void suite_flush_summary_race() {
    run_suite("flush_summary_race", test_flush_summary_race);
}

void suite_shutdown_drain_race() {
    run_suite("shutdown_drain_race", test_shutdown_drain_race);
}

void suite_drop_visibility() {
    run_suite("drop_visibility", test_drop_visibility);
}
