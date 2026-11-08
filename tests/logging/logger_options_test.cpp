// Split from logging_test.cpp: nexigen::logging LoggerOptions.

#include "logging_test_support.hpp"

namespace {

// --- logger-options suite (change rework-logging-hot-path-engine, task 1.3 /
// spec logging/logger-options): defaults, bit_ceil rounding, serialization
// round-trips under JSON_NOEXCEPTION=1.
void test_logger_options() {
    // Defaults: every field carries its documented value.
    const LoggerOptions o{};
    NX_CHECK(o.log_root_directory == "./logs");
    NX_CHECK(o.initial_log_level == LogLevel::Info);
    NX_CHECK(o.max_file_size_bytes == 104857600ULL);
    NX_CHECK(o.spsc_ring_buffer_capacity == 65536U);
    NX_CHECK(o.message_capacity == 128U);
    NX_CHECK(o.max_producer_threads == 64U);
    NX_CHECK(o.overflow_policy == OverflowPolicy::DropNewest);
    NX_CHECK(o.worker_thread.cpus().empty());
    NX_CHECK(o.worker_poll_interval_us == 1000U);
    NX_CHECK(o.sync_flush_on_critical == true);
    NX_CHECK(o.spam_protection.enable_repeat_suppression == true);
    NX_CHECK(o.spam_protection.repeat_suppression_window_ms == 1000U);
    NX_CHECK(o.spam_protection.max_messages_per_sec_per_shard == 100000U);
    NX_CHECK(o.spam_protection.bypass_min_level == LogLevel::Fatal);

    // bit_ceil rounding: non-power-of-two capacity rounds up.
    LoggerOptions custom{};
    custom.spsc_ring_buffer_capacity = 1000U;
    NX_CHECK(std::bit_ceil(custom.spsc_ring_buffer_capacity) == 1024U);

    // to_string renders every field (including the worker thread spec).
    const std::string s = o.to_string();
    NX_CHECK(s.find("log_root_directory") != std::string::npos);
    NX_CHECK(s.find("message_capacity") != std::string::npos);
    NX_CHECK(s.find("overflow_policy") != std::string::npos);
    NX_CHECK(s.find("worker_thread") != std::string::npos);
    NX_CHECK(s.find("spam_protection") != std::string::npos);
    NX_CHECK(to_string(o) == s);

    // OverflowPolicy round-trip.
    NX_CHECK(to_string(OverflowPolicy::DropNewest) == "drop_newest");
    NX_CHECK(to_string(OverflowPolicy::OverwriteOldest) == "overwrite_oldest");
    OverflowPolicy op = OverflowPolicy::OverwriteOldest;
    NX_CHECK(from_string("drop_newest", op));
    NX_CHECK(op == OverflowPolicy::DropNewest);
    NX_CHECK(!from_string("bogus", op));
    NX_CHECK(op == OverflowPolicy::DropNewest); // unchanged on failure

    // JSON round-trip (strictly non-throwing under JSON_NOEXCEPTION=1).
    nlohmann::json j;
    to_json(j, o);
    NX_CHECK(j.is_object());
    NX_CHECK(j["overflow_policy"] == "drop_newest");
    NX_CHECK(j["spam_protection"]["bypass_min_level"] == "fatal");
    LoggerOptions back{};
    back.log_root_directory = "/tmp/x";
    back.initial_log_level = LogLevel::Fatal;
    from_json(j, back);
    NX_CHECK(back.log_root_directory == "./logs");
    NX_CHECK(back.initial_log_level == LogLevel::Info);
    NX_CHECK(back.spsc_ring_buffer_capacity == 65536U);
    NX_CHECK(back.message_capacity == 128U);
    NX_CHECK(back.max_producer_threads == 64U);
    NX_CHECK(back.overflow_policy == OverflowPolicy::DropNewest);
    NX_CHECK(back.spam_protection.bypass_min_level == LogLevel::Fatal);
    NX_CHECK(back.spam_protection.max_messages_per_sec_per_shard == 100000U);

    // Non-default message_capacity round-trips exactly (uint32, no bit_ceil).
    nlohmann::json jmc;
    LoggerOptions big{};
    big.message_capacity = 256U;
    to_json(jmc, big);
    LoggerOptions big_back{};
    from_json(jmc, big_back);
    NX_CHECK(big_back.message_capacity == 256U);

    // Hostile JSON leaves target untouched (no throw).
    LoggerOptions keep = o;
    keep.log_root_directory = "/keep";
    from_json(nlohmann::json(42), keep);
    NX_CHECK(keep.log_root_directory == "/keep");
    from_json(nlohmann::json::array(), keep);
    NX_CHECK(keep.log_root_directory == "/keep");

    // std::formatter<LoggerOptions>.
    NX_CHECK(std::format("{}", o).find("log_root_directory") != std::string::npos);
    NX_CHECK(std::format("[{:*>20}]", OverflowPolicy::OverwriteOldest).find("overwrite_oldest") !=
             std::string::npos);

    std::printf("logger_options: defaults, bit_ceil, JSON round-trip, formatter verified\n");
}

} // namespace

void suite_logger_options() {
    run_suite("logger_options", test_logger_options);
}
