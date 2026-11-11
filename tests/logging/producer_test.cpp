// Split from logging_test.cpp: nexigen::logging producer hot path and edge cases.

#include "logging_test_support.hpp"

namespace {

// --- producer suite (tasks 5.1/5.2 / design D5/D6/D9 / spec
// logging/multi-producer, logging/log-record): filter short-circuit, pack+push
// field integrity, per-shard drops, FIFO, registration isolation, slot
// exhaustion, and aggregation — the drain/render round-trips stay here.

[[nodiscard]] std::uint64_t rendered_value(const LogRecord<128>& rec) {
    // "v {N}" records from concurrent drains: parse the number back out.
    char buf[64];
    const std::size_t n = const_cast<LogRecord<128>&>(rec).render_into(buf, sizeof buf);
    std::uint64_t v = 0;
    std::from_chars(buf + 2, buf + n, v); // skip "v "
    return v;
}

void test_producer_filter_and_fields() {
    detail::Producer<8, 128> prod{};
    NX_CHECK(prod.initialize().has_value());

    // Below threshold: silent return + zero ring traffic (short-circuit).
    prod.set_level(LogLevel::Warning);
    prod.emit(LogLevel::Info, "filtered {}", 1);
    NX_CHECK(prod.ring_size() == 0);

    // Off silences everything, even Fatal.
    prod.set_level(LogLevel::Off);
    prod.emit(LogLevel::Fatal, "silence");
    NX_CHECK(prod.ring_size() == 0);
    prod.set_level(LogLevel::Info);

    // Accepted: full field integrity across the push/pop round trip, incl.
    // the exact caller location (FormatWithLocation capture) and rendering.
    const int probe_line = __LINE__ + 1;
    prod.emit(LogLevel::Warning, "hello world");
    NX_CHECK(prod.ring_size() == 1);
    LogRecord<128> out{};
    NX_CHECK(prod.pop(out).has_value());
    NX_CHECK(out.level == LogLevel::Warning);
    NX_CHECK(out.fmt != nullptr && std::string_view(out.fmt) == std::string_view("hello world"));
    char msg[256];
    NX_CHECK(out.render_into(msg, sizeof msg) == 11U);
    NX_CHECK(std::string_view(msg, 11U) == "hello world");
    NX_CHECK(!out.args_truncated() && !out.render_truncated());
    NX_CHECK(out.thread_id != 0U);
    NX_CHECK(out.thread_id == detail::cached_thread_id());
    NX_CHECK(out.stamp.nanoseconds() > 0U);
    NX_CHECK(std::string_view(out.file_name) == std::string_view(__FILE__));
    NX_CHECK(std::string_view(out.function_name).find("test_producer") != std::string_view::npos);
    NX_CHECK(out.line == static_cast<std::uint32_t>(probe_line));

    // Float/string pack round-trip through the real pipeline (Debug needs the
    // threshold lowered first: filtering is silent and correct).
    prod.set_level(LogLevel::Debug);
    prod.emit(LogLevel::Debug, "Прямой вызов метода debug. Давление: {:.4f}", 0.0025);
    prod.set_level(LogLevel::Info);
    NX_CHECK(prod.pop(out).has_value());
    const std::size_t ru_len = out.render_into(msg, sizeof msg);
    NX_CHECK(std::string_view(msg, ru_len) == "Прямой вызов метода debug. Давление: 0.0025");
}

void test_producer_capacity_and_fifo() {
    detail::Producer<8, 128> prod{};
    NX_CHECK(prod.initialize().has_value());
    LogRecord<128> out{};

    // Fill to capacity (8): next push drops + counts; stored elements intact;
    // a FILTERED call never probes the full ring (spec log-level scenario).
    for (int i = 0; i < 8; ++i) {
        prod.emit(LogLevel::Info, "v {}", static_cast<std::uint64_t>(i));
    }
    NX_CHECK(prod.ring_size() == 8);
    prod.emit(LogLevel::Info, "overflow");
    NX_CHECK(prod.dropped().buffer_full == 1);
    NX_CHECK(prod.ring_size() == 8);
    prod.set_level(LogLevel::Fatal);
    prod.emit(LogLevel::Error, "while-full"); // filtered path
    NX_CHECK(prod.ring_size() == 8);          // untouched by the filtered call
    prod.set_level(LogLevel::Info);

    // Drain preserves FIFO order (v0..v7 as written).
    int order_bad = 0;
    for (int i = 0; i < 8; ++i) {
        if (prod.pop(out)) {
            if (rendered_value(out) != static_cast<std::uint64_t>(i)) {
                ++order_bad;
            }
        } else {
            ++order_bad;
        }
    }
    NX_CHECK(order_bad == 0);
    NX_CHECK(prod.ring_empty());
}

void test_producer_zero_allocation() {
    LogRecord<128> out{};

    // Zero-allocation window: 4000 accepted emits + full drain, both counters
    // flat across the whole sequence (heap allocator sees nothing).
    static detail::Producer<4096, 128> bulk{};
    NX_CHECK(bulk.initialize().has_value());
    const std::size_t arena_before = g_alloc_used;
    const std::size_t heap_before = heap_in_use();
    for (int i = 0; i < 4000; ++i) {
        bulk.emit(LogLevel::Info, "v {}", static_cast<std::uint64_t>(i));
    }
    int drained = 0;
    while (bulk.pop(out)) {
        ++drained;
    }
    NX_CHECK(g_alloc_used == arena_before);
    NX_CHECK(heap_in_use() == heap_before);
    NX_CHECK(drained == 4000); // ring big enough: no drops
    NX_CHECK(bulk.dropped().total() == 0);
}

void test_producer_concurrent_wrap() {
    // Concurrent wrap: 50k emits while the consumer drains; values strictly
    // increasing, every accepted record accounted (wait-free under pressure).
    alignas(64) static detail::Producer<64, 128> wrap{};
    NX_CHECK(wrap.initialize().has_value());
    std::atomic<bool> stop{false};
    std::uint64_t popped = 0;
    int seq_bad = 0;
    std::jthread consumer([&stop, &popped, &seq_bad] { // wrap is static: referenced
        LogRecord<128> rec{};
        std::uint64_t last = 0;
        bool first = true;
        while (!stop.load(std::memory_order_relaxed) || !wrap.ring_empty()) {
            if (wrap.pop(rec)) {
                const std::uint64_t v = rendered_value(rec);
                if (!first && v <= last) {
                    ++seq_bad;
                }
                last = v;
                first = false;
                ++popped;
            } else {
                std::this_thread::yield();
            }
        }
    });
    for (int i = 0; i < 50'000; ++i) {
        wrap.emit(LogLevel::Info, "v {}", static_cast<std::uint64_t>(i));
    }
    const std::uint64_t wrap_drops = wrap.dropped().buffer_full;
    stop.store(true, std::memory_order_relaxed);
    consumer.join();
    const std::uint64_t accept_count = static_cast<std::uint64_t>(50'000) - wrap_drops;
    NX_CHECK(seq_bad == 0);
    NX_CHECK(popped == accept_count);
    NX_CHECK(accept_count + wrap.dropped().buffer_full == 50'000U);
    std::printf("producer: 50k concurrent wrap, accepted=%llu dropped=%llu seq_bad=%d\n",
                static_cast<unsigned long long>(accept_count),
                static_cast<unsigned long long>(wrap.dropped().buffer_full), seq_bad);
}

/// Per-value-range bookkeeping for the shard-exclusivity check: every value of
/// one producer range must arrive with one thread id, in non-decreasing order.
struct PerThreadOrder {
    std::uint32_t tid{0};
    std::size_t count{0};
    std::uint64_t last{0};
    bool order_ok{true};

    void observe(const LogRecord<128>& rec, std::uint64_t value) {
        if (tid == 0U) {
            tid = rec.thread_id;
        }
        if (rec.thread_id != tid || value < last) {
            order_ok = false;
        }
        last = value;
        ++count;
    }
};

void test_producer_multi_producer_isolation() {
    LogRecord<128> out{};

    // --- multi-producer isolation (spec logging/multi-producer): thread A
    // emits values 0..4, thread B emits 100..104. Records draining from the
    // pool must split by distinct thread ids with each value range landing on
    // exactly one tid (shard exclusivity) — and per-thread FIFO holds inside
    // each range because a shard is SPSC.
    static detail::Producer<8, 128> mp{};
    NX_CHECK(mp.initialize().has_value());
    std::atomic<bool> go{false};
    std::jthread a([&] {
        while (!go.load(std::memory_order_relaxed)) {}
        for (int i = 0; i < 5; ++i)
            mp.emit(LogLevel::Info, "v {}", static_cast<std::uint64_t>(i));
    });
    std::jthread b([&] {
        while (!go.load(std::memory_order_relaxed)) {}
        for (int i = 100; i < 105; ++i)
            mp.emit(LogLevel::Info, "v {}", static_cast<std::uint64_t>(i));
    });
    go.store(true, std::memory_order_relaxed);
    a.join();
    b.join();
    PerThreadOrder low_range;
    PerThreadOrder high_range;
    std::size_t total_popped = 0;
    while (mp.pop(out)) {
        const std::uint64_t v = rendered_value(out);
        ++total_popped;
        (v < 50U ? low_range : high_range).observe(out, v);
    }
    NX_CHECK(total_popped == 10U);
    NX_CHECK(low_range.count == 5U && high_range.count == 5U);
    NX_CHECK(low_range.tid != high_range.tid && low_range.tid != 0U && high_range.tid != 0U);
    NX_CHECK(low_range.order_ok && high_range.order_ok);
    NX_CHECK(mp.dropped().total() == 0); // nobody starved another thread
}

void test_producer_shard_limits() {
    // Full shard drops ONLY its own producer (spec scenario): main fills slot,
    // a fresh thread's own shard still accepts.
    static detail::Producer<4, 128> isolation{};
    NX_CHECK(isolation.initialize().has_value());
    for (int i = 0; i < 6; ++i) {
        isolation.emit(LogLevel::Info, "v {}", static_cast<std::uint64_t>(i));
    }
    NX_CHECK(isolation.dropped().buffer_full == 2U); // main thread: 4 accepted, 2 dropped
    std::uint64_t other_accepted = 0;
    std::thread helper([&] {
        const std::uint64_t before = isolation.dropped().buffer_full;
        static_cast<void>(before);
        for (int i = 0; i < 3; ++i) {
            isolation.emit(LogLevel::Info, "v {}", static_cast<std::uint64_t>(200 + i));
        }
        other_accepted = isolation.ring_size();
    });
    helper.join();
    NX_CHECK(other_accepted == 7U);                  // 4 (main) + 3 (helper shard)
    NX_CHECK(isolation.dropped().buffer_full == 2U); // helper dropped nothing
    LogRecord<128> tmp{};
    while (isolation.pop(tmp)) {}

    // Slot exhaustion beyond MaxThreads (spec scenario): 33rd-style thread on
    // a 2-slot pool is dropped-and-counted, never blocks, never corrupts.
    static detail::Producer<4, 128, 2> tiny{};
    NX_CHECK(tiny.initialize().has_value());
    tiny.emit(LogLevel::Info, "slot0");
    std::jthread t2([&] { tiny.emit(LogLevel::Info, "slot1"); });
    t2.join();
    std::jthread t3([&] { tiny.emit(LogLevel::Info, "overflow-thread"); });
    t3.join();
    NX_CHECK(tiny.dropped().slot_exhausted == 1U);
    std::size_t tiny_popped = 0;
    while (tiny.pop(tmp)) {
        ++tiny_popped;
    }
    NX_CHECK(tiny_popped == 2U); // both registered producers' records intact
    std::printf("producer: multi-thread isolation + exhaustion counting verified\n");
}

/// Producer hot path: filtering, FIFO, zero-allocation, concurrent wrap,
/// per-thread shard isolation and slot exhaustion (spec logging/multi-producer).
void test_producer() {
    test_producer_filter_and_fields();
    test_producer_capacity_and_fifo();
    test_producer_zero_allocation();
    test_producer_concurrent_wrap();
    test_producer_multi_producer_isolation();
    test_producer_shard_limits();
}

// --- producer_edge_cases suite (coverage gaps in producer.hpp):
// summary cause combinations, OverwriteOldest in emit, TLS map full,
// flush with no drops, double flush.
void test_producer_edge_cases() {
    // Summary with rate_limited only (throttle, no buffer_full): synthesized
    // by the worker at the flush barrier (no ring push from the flusher).
    {
        static detail::Producer<64, 127, 1> prod{};
        NX_CHECK(prod.initialize().has_value());
        prod.configure_throttle(ThrottleConfig{true, 2ULL, 1'000'000'000ULL}); // 2 per second
        for (int i = 0; i < 5; ++i) {
            prod.emit(LogLevel::Info, "v {}", static_cast<std::uint64_t>(i));
        }
        NX_CHECK(prod.dropped().rate_limited == 3);
        char arena[2048]{};
        detail::AsyncWorker<detail::Producer<64, 127, 1>, LinesSink<LogRecord<127>>> worker{};
        NX_CHECK(worker.start(prod, core::ThreadSpec{}, arena, sizeof arena).has_value());
        for (int i = 0; i < 5000 && !worker.writer_started(); ++i) {
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
        NX_CHECK(worker.writer_started());
        NX_CHECK(worker.flush().has_value());
        const LinesSink<LogRecord<127>> probe(arena, sizeof arena);
        NX_CHECK(probe.count("Dropped 3 record(s)") == 1);
        NX_CHECK(probe.find("rate-limited: 3"));
        NX_CHECK(probe.find("buffer-full: 0"));
        NX_CHECK(prod.dropped().rate_limited == 0); // reported cell reset
        prod.configure_throttle(ThrottleConfig{});  // reset
    }

    // Summary with both rate_limited and buffer_full causes in one line.
    {
        static detail::Producer<4, 128, 1> prod2{};
        NX_CHECK(prod2.initialize().has_value());
        prod2.configure_throttle(ThrottleConfig{true, 2ULL, 1'000'000'000ULL});
        // 2 accepted (throttle budget), the remaining 4 are rate_limited
        // before the ring push (the throttle owns the excess, so buffer_full
        // stays zero for this producer).
        for (int i = 0; i < 6; ++i) {
            prod2.emit(LogLevel::Info, "v {}", static_cast<std::uint64_t>(i));
        }
        NX_CHECK(prod2.dropped().rate_limited == 4);
        NX_CHECK(prod2.dropped().buffer_full == 0);
        char arena[2048]{};
        detail::AsyncWorker<detail::Producer<4, 128, 1>, LinesSink<LogRecord<128>>> worker{};
        NX_CHECK(worker.start(prod2, core::ThreadSpec{}, arena, sizeof arena).has_value());
        for (int i = 0; i < 5000 && !worker.writer_started(); ++i) {
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
        NX_CHECK(worker.writer_started());
        NX_CHECK(worker.flush().has_value());
        const LinesSink<LogRecord<128>> probe(arena, sizeof arena);
        NX_CHECK(probe.count("Dropped 4 record(s)") == 1);
        NX_CHECK(probe.find("rate-limited: 4"));
        NX_CHECK(probe.find("buffer-full: 0"));
        prod2.configure_throttle(ThrottleConfig{});
    }

    // Flush with no drops: no summary emitted.
    {
        static detail::Producer<16, 128, 1> prod3{};
        NX_CHECK(prod3.initialize().has_value());
        prod3.emit(LogLevel::Info, "clean");
        char arena[2048]{};
        detail::AsyncWorker<detail::Producer<16, 128, 1>, LinesSink<LogRecord<128>>> worker{};
        NX_CHECK(worker.start(prod3, core::ThreadSpec{}, arena, sizeof arena).has_value());
        for (int i = 0; i < 5000 && !worker.writer_started(); ++i) {
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
        NX_CHECK(worker.writer_started());
        NX_CHECK(worker.flush().has_value());
        const LinesSink<LogRecord<128>> probe(arena, sizeof arena);
        NX_CHECK(probe.count("Dropped") == 0);
        NX_CHECK(probe.count("clean") == 1);
    }

    // Double flush: both complete without deadlock or data loss; no summary
    // repeats for a clean producer.
    {
        static detail::Producer<16, 128, 1> prod4{};
        NX_CHECK(prod4.initialize().has_value());
        prod4.emit(LogLevel::Info, "before flush 1");
        char arena[2048]{};
        detail::AsyncWorker<detail::Producer<16, 128, 1>, LinesSink<LogRecord<128>>> worker{};
        NX_CHECK(worker.start(prod4, core::ThreadSpec{}, arena, sizeof arena).has_value());
        for (int i = 0; i < 5000 && !worker.writer_started(); ++i) {
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
        NX_CHECK(worker.writer_started());
        NX_CHECK(worker.flush().has_value());
        NX_CHECK(worker.flush().has_value()); // second barrier is a no-op
        const LinesSink<LogRecord<128>> probe(arena, sizeof arena);
        NX_CHECK(probe.count("before flush 1") == 1);
        NX_CHECK(probe.count("Dropped") == 0);
    }

    // Overflow in emit: the engine's fixed policy is DropNewest; ring-full
    // records are counted in buffer_full. Per-shard OverwriteOldest lands with
    // the SpscShardRing wiring in the follow-up change.
    {
        static detail::Producer<4, 128, 1> prod5{};
        NX_CHECK(prod5.initialize().has_value());
        // Fill to capacity.
        for (int i = 0; i < 4; ++i) {
            prod5.emit(LogLevel::Info, "v {}", static_cast<std::uint64_t>(i));
        }
        NX_CHECK(prod5.shard(0).is_full());
        // 2 more emits are dropped by the full ring.
        prod5.emit(LogLevel::Info, "new0");
        prod5.emit(LogLevel::Info, "new1");
        NX_CHECK(prod5.dropped().buffer_full == 2); // dropped newest counted
        // Drain: the four original records remain.
        LogRecord<128> rec{};
        int count = 0;
        while (prod5.pop(rec)) {
            ++count;
        }
        NX_CHECK(count == 4);
    }

    // TLS map full: 5+ loggers on one thread — documented misconfiguration,
    // still wait-free, drops counted.
    {
        static detail::Producer<8, 128, 2> p1{}, p2{}, p3{}, p4{}, p5{};
        NX_CHECK(p1.initialize().has_value());
        NX_CHECK(p2.initialize().has_value());
        NX_CHECK(p3.initialize().has_value());
        NX_CHECK(p4.initialize().has_value());
        NX_CHECK(p5.initialize().has_value());
        // All 5 on main thread: TLS map has 4 entries, 5th re-registers per call.
        p1.emit(LogLevel::Info, "a");
        p2.emit(LogLevel::Info, "b");
        p3.emit(LogLevel::Info, "c");
        p4.emit(LogLevel::Info, "d");
        p5.emit(LogLevel::Info, "e"); // TLS map full → re-registers
        NX_CHECK(p1.ring_size() == 1);
        NX_CHECK(p5.ring_size() == 1);
    }

    std::printf("producer_edge_cases: summary combinations, OverwriteOldest, "
                "TLS map full, double flush verified\n");
}

// 6 distinct threads log known distinct payloads to ONE ConsoleLogger, ONE
// FileLogger, and a CompositeLogger simultaneously; every record must reach
// the sink exactly once, per-thread order must be FIFO, flush must complete
// across all shards, and no thread may be starved by another's flooding.

} // namespace

void suite_producer() {
    run_suite("producer", test_producer);
}

void suite_producer_edge_cases() {
    run_suite("producer_edge_cases", test_producer_edge_cases);
}
