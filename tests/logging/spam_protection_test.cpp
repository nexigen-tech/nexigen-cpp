// Split from logging_test.cpp: nexigen::logging spam protection.

#include "logging_test_support.hpp"

namespace {

// --- spam_protection suite (change rework-logging-hot-path-engine, task 5.3 /
// spec logging/spam-protection): repeat suppression, token bucket, bypass.
void test_spam_protection() {
    // Repeat suppression: identical messages collapsed.
    {
        detail::SpamProtection sp{SpamProtectionConfig{.enable_repeat_suppression = true}};
        detail::SpamShardState st{};
        const std::uint64_t h1 = detail::message_hash("f.cpp", 1, "msg", nullptr, 0);
        const std::uint64_t h2 = detail::message_hash("f.cpp", 2, "other", nullptr, 0);
        NX_CHECK(sp.check_repeat(st, h1, 1000));  // first: pass
        NX_CHECK(!sp.check_repeat(st, h1, 2000)); // duplicate: suppress
        NX_CHECK(!sp.check_repeat(st, h1, 3000)); // duplicate: suppress
        NX_CHECK(sp.check_repeat(st, h2, 4000));  // different: pass
    }

    // Repeat suppression disabled: all pass.
    {
        detail::SpamProtection sp{SpamProtectionConfig{.enable_repeat_suppression = false}};
        detail::SpamShardState st{};
        const std::uint64_t h = detail::message_hash("f.cpp", 1, "msg", nullptr, 0);
        for (int i = 0; i < 10; ++i) {
            NX_CHECK(sp.check_repeat(st, h, static_cast<std::uint64_t>(i)));
        }
    }

    // Token bucket: messages below bypass_min_level throttled at quota.
    {
        SpamProtectionConfig cfg{};
        cfg.max_messages_per_sec_per_shard = 3;
        cfg.bypass_min_level = LogLevel::Fatal;
        detail::SpamProtection sp{cfg};
        detail::SpamShardState st{};
        // 3 messages pass (Info < Critical).
        for (int i = 0; i < 3; ++i) {
            NX_CHECK(sp.check_rate_limit(st, LogLevel::Info, 1000));
        }
        // 4th is throttled.
        NX_CHECK(!sp.check_rate_limit(st, LogLevel::Info, 1000));
        NX_CHECK(st.throttled_count.load() == 1);
        // bypass_min_level always passes.
        NX_CHECK(sp.check_rate_limit(st, LogLevel::Fatal, 1000));
    }

    // No limit (0): all pass.
    {
        SpamProtectionConfig cfg{};
        cfg.max_messages_per_sec_per_shard = 0;
        detail::SpamProtection sp{cfg};
        detail::SpamShardState st{};
        for (int i = 0; i < 100; ++i) {
            NX_CHECK(sp.check_rate_limit(st, LogLevel::Info, 1000));
        }
    }

    // Runtime reconfiguration: set_config changes quotas.
    {
        SpamProtectionConfig cfg{};
        cfg.max_messages_per_sec_per_shard = 100000;
        detail::SpamProtection sp{cfg};
        detail::SpamShardState st{};
        NX_CHECK(sp.check_rate_limit(st, LogLevel::Info, 1000)); // 1 token used
        // Tighten quota to 1: tokens_used is already 1, so next call throttles.
        cfg.max_messages_per_sec_per_shard = 1;
        sp.set_config(cfg);
        NX_CHECK(!sp.check_rate_limit(st, LogLevel::Info, 1000)); // throttled (tokens_used >= 1)
        // Reset window: new second, tokens reset.
        NX_CHECK(sp.check_rate_limit(st, LogLevel::Info, 2'000'000'000ULL));  // 1st in new window
        NX_CHECK(!sp.check_rate_limit(st, LogLevel::Info, 2'000'000'000ULL)); // 2nd throttled
    }

    std::printf("spam_protection: repeat suppression, token bucket, bypass, reconfig verified\n");
}

// --- spam-config race suite (change fix-p1-races-process-correctness, spec
// logging/spam-protection "Dynamic quota updates are race-free"): one thread
// reconfigures anti-spam quotas while three application threads emit through
// the real worker. The reconfiguration path is
// Producer::set_spam_protection_config → SpamProtection::set_config
// (per-field relaxed stores); producers load the same fields relaxed on
// every emit. TSan (task 3.4) is the verdict; these assertions only check
// the barrier-ordered visible end state.
char g_spam_race_arena[16384]{};

void test_spam_config_race() {
    using P = detail::Producer<4, 128, 4>;
    using RecordT = LogRecord<128>;
    P prod{};
    NX_CHECK(prod.initialize().has_value());

    detail::AsyncWorker<P, LinesSink<RecordT>> worker{};
    NX_CHECK(worker.start(prod, core::ThreadSpec{}, g_spam_race_arena, sizeof g_spam_race_arena)
                 .has_value());
    for (int i = 0; i < 5000 && !worker.writer_started(); ++i) {
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    NX_CHECK(worker.writer_started());

    SpamProtectionConfig quota_cfg{};
    quota_cfg.max_messages_per_sec_per_shard = 5; // tiny quota → throttle waves
    SpamProtectionConfig open_cfg{};
    open_cfg.max_messages_per_sec_per_shard = 0; // no limit

    std::atomic<bool> stop_flag{false};
    std::jthread reconfigurer([&] {
        std::uint64_t flips = 0;
        while (!stop_flag.load(std::memory_order_relaxed)) {
            prod.set_spam_protection_config((flips & 1ULL) == 0ULL ? quota_cfg : open_cfg);
            ++flips;
            std::this_thread::yield();
        }
    });
    auto emitter = [](detail::Producer<4, 128, 4>& producer, std::int32_t lane,
                      std::atomic<bool>& stop) {
        // Wave pacing (2 records per 2 ms): the 16 KiB shared arena must keep
        // capacity for the late flush-time summaries, so a 150 ms run stays
        // inside the render budget while the tiny quota throttles the rest.
        for (int wave = 0; !stop.load(std::memory_order_relaxed); ++wave) {
            for (int i = 0; i < 2; ++i) {
                producer.emit(LogLevel::Info, "spam-race {} {}", static_cast<std::int64_t>(lane),
                              static_cast<std::int64_t>(wave * 2 + i));
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(2));
        }
    };
    std::jthread e0(emitter, std::ref(prod), 0, std::ref(stop_flag));
    std::jthread e1(emitter, std::ref(prod), 1, std::ref(stop_flag));
    std::jthread e2(emitter, std::ref(prod), 2, std::ref(stop_flag));
    std::this_thread::sleep_for(std::chrono::milliseconds(150));
    stop_flag.store(true, std::memory_order_release);
    e0.join();
    e1.join();
    e2.join();
    reconfigurer.join();

    NX_CHECK(worker.flush().has_value()); // final barrier: drained + synced
    // Some first-window records were accepted under whichever quota was
    // published at the time, and the tiny quota made throttling pending —
    // both show up in the sink stream after the barrier.
    const LinesSink<RecordT> probe(g_spam_race_arena, sizeof g_spam_race_arena);
    NX_CHECK(probe.count("spam-race") >= 4);
    NX_CHECK(probe.find("throttled: "));

    std::printf("spam_config_race: reconfiguration vs concurrent emitters "
                "verified\n");
}

} // namespace

void suite_spam_protection() {
    run_suite("spam_protection", test_spam_protection);
}

void suite_spam_config_race() {
    run_suite("spam_config_race", test_spam_config_race);
}
