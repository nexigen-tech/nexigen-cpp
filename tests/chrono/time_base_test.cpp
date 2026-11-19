// Tests nexigen::chrono time-base service — C++23, zero-dependency harness
// (same pattern as chrono_test.cpp / design D4): NX_CHECK counts checks and
// failures with file/line, run_suite prints one summary line per suite, and
// main returns the failure count so a non-zero exit fails CTest.
//
// Suites are added as the change progresses: value vocabularies first, then the
// shared-memory block layout, seqlock, segment lifecycle and client contract.

#include <atomic>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstddef>
#include <cstdlib>
#include <format>
#include <fstream>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <thread>
#include <type_traits>
#include <vector>

#include <nlohmann/json.hpp>

#include <fcntl.h>
#include <malloc.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <unistd.h>

#include <nexigen/chrono/boot_identity.hpp>
#include <nexigen/chrono/phc_cross_timestamp.hpp>
#include <nexigen/core/integrity/crc32c.hpp>
#include <nexigen/chrono/detail/time_base_segment.hpp>
#include <nexigen/chrono/detail/time_base_seqlock.hpp>
#include <nexigen/chrono/time_base_clock.hpp>
#include <nexigen/chrono/time_base_device.hpp>
#include <nexigen/chrono/time_base_daemon_config.hpp>
#include <nexigen/chrono/adjtimex_status.hpp>
#include <nexigen/chrono/daemon_runtime.hpp>
#include <nexigen/chrono/detail/kalman2.hpp>
#include <nexigen/chrono/tai_deadline.hpp>
#include <nexigen/chrono/time_base_client.hpp>
#include <nexigen/chrono/time_base_host_domain.hpp>
#include <nexigen/chrono/time_base_estimator.hpp>
#include <nexigen/chrono/json_file_sink_config.hpp>
#include <nexigen/chrono/time_base_history_writer.hpp>
#include <nexigen/chrono/time_base_publisher.hpp>
#include <nexigen/chrono/timestamp.hpp>
#include <nexigen/chrono/time_domain_class.hpp>
#include <nexigen/chrono/domain_coefficient.hpp>
#include <nexigen/chrono/lock_state.hpp>
#include <nexigen/chrono/time_base_block.hpp>
#include <nexigen/chrono/time_base_history_entry.hpp>
#include <nexigen/chrono/time_base_status.hpp>

using namespace nexigen::chrono;

namespace {

int g_checks = 0;
int g_failures = 0;

#define NX_CHECK(...)                                                                              \
    do {                                                                                           \
        ++g_checks;                                                                                \
        if (!(__VA_ARGS__)) {                                                                      \
            ++g_failures;                                                                          \
            std::fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #__VA_ARGS__);            \
        }                                                                                          \
    } while (false)

void run_suite(const char* suite_name, void (*body)()) {
    const int checks_before = g_checks;
    const int failures_before = g_failures;

    body();

    const int suite_checks = g_checks - checks_before;
    const int suite_failures = g_failures - failures_before;
    std::printf("[%-24s] %s (%d checks, %d failed)\n", suite_name,
                suite_failures == 0 ? "OK" : "FAILED", suite_checks, suite_failures);
}

/// @brief Round-trip one enum value through the vocabulary and the formatter.
template <class E> void check_enum_roundtrip(E value, std::string_view expected_name) {
    NX_CHECK(to_string(value) == expected_name);

    E parsed = value;
    NX_CHECK(from_string(expected_name, parsed));
    NX_CHECK(parsed == value);

    NX_CHECK(std::format("{}", value) == expected_name);

    nlohmann::json document;
    to_json(document, value);
    NX_CHECK(document.is_string());
    NX_CHECK(document.get<std::string>() == expected_name);

    E decoded{};
    from_json(document, decoded);
    NX_CHECK(decoded == value);
}

// --- vocabularies (spec chrono/time-base "Time-base status", "Domain
// coefficient row").

void test_time_base_status() {
    check_enum_roundtrip(TimeBaseStatus::NotReady, "not_ready");
    check_enum_roundtrip(TimeBaseStatus::Ready, "ready");
    check_enum_roundtrip(TimeBaseStatus::Degraded, "degraded");
    check_enum_roundtrip(TimeBaseStatus::Lost, "lost");

    // Out-of-table values render "Unknown" and never parse.
    const auto bogus = static_cast<TimeBaseStatus>(0x7FU);
    NX_CHECK(to_string(bogus) == "Unknown");

    TimeBaseStatus untouched = TimeBaseStatus::Ready;
    NX_CHECK(!from_string("unknown_status", untouched));
    NX_CHECK(untouched == TimeBaseStatus::Ready);

    // Hostile JSON leaves the destination unchanged without aborting.
    from_json(nlohmann::json(42), untouched);
    NX_CHECK(untouched == TimeBaseStatus::Ready);
    from_json(nlohmann::json(nullptr), untouched);
    NX_CHECK(untouched == TimeBaseStatus::Ready);
    from_json(nlohmann::json("bogus"), untouched);
    NX_CHECK(untouched == TimeBaseStatus::Ready);
}

void test_domain_kind() {
    check_enum_roundtrip(TimeDomainClass::SystemClock, "system_clock");
    check_enum_roundtrip(TimeDomainClass::CycleCounter, "cycle_counter");
    check_enum_roundtrip(TimeDomainClass::DeviceClock, "device_clock");
    check_enum_roundtrip(SyncMode::Standalone, "standalone");
    check_enum_roundtrip(SyncMode::PtpSynced, "ptp_synced");
    check_enum_roundtrip(SyncMode::Holdover, "holdover");

    // ServiceFlags is a bitmask: names, formatter and JSON round-trip.
    ServiceFlags flags{};
    flags.set(ServiceFlag::PhcPresent);
    flags.set(ServiceFlag::TaiSynchronized);
    NX_CHECK(to_string(flags) == "phc_present|tai_synchronized");
    NX_CHECK(std::format("{}", flags) == to_string(flags));
    const nlohmann::json flags_json = flags;
    ServiceFlags decoded{};
    from_json(flags_json, decoded);
    NX_CHECK(decoded == flags);
    ServiceFlag single = ServiceFlag::SlewActive;
    NX_CHECK(from_string("slew_active", single));
    NX_CHECK(single == ServiceFlag::SlewActive);
    NX_CHECK(!from_string("bogus", single));
    ServiceFlags empty{};
    NX_CHECK(to_string(empty) == "none");
}

void test_lock_state() {
    check_enum_roundtrip(LockState::Locked, "locked");
    check_enum_roundtrip(LockState::Holdover, "holdover");
    check_enum_roundtrip(LockState::FreeRun, "free_run");
    check_enum_roundtrip(LockState::Unsupported, "unsupported");
}

// --- layout and map evaluation (spec chrono/time-base "Domain coefficient
// row", "Shared-memory publication contract").

void test_time_base_layout() {
    NX_CHECK(sizeof(DomainCoefficient) == 128U);
    NX_CHECK(alignof(DomainCoefficient) == 64U);
    NX_CHECK(sizeof(TimeBaseBlock) == time_base_block_bytes);
    NX_CHECK(alignof(TimeBaseBlock) == 64U);
    NX_CHECK(offsetof(TimeBaseBlock, generation) == 24U);
    NX_CHECK(offsetof(TimeBaseBlock, heartbeat_ns) == 64U);
    NX_CHECK(offsetof(TimeBaseBlock, rows) == 128U);
    NX_CHECK(offsetof(TimeBaseBlock, last_snapshot) == 4224U);
    NX_CHECK(sizeof(TimeBaseBlock) == 5120U);
    NX_CHECK(sizeof(TimeSnapshot) == 896U);
    NX_CHECK(alignof(TimeSnapshot) == 64U);
    NX_CHECK(std::is_trivially_copyable_v<TimeBaseBlock>);
    NX_CHECK(std::is_trivially_copyable_v<DomainCoefficient>);

    TimeBaseBlock block{};
    NX_CHECK(block.magic == time_base_magic);
    NX_CHECK(block.version == time_base_version);
    NX_CHECK(block.capacity == time_base_max_domains);
    NX_CHECK(block.domain_count == 0U);
    NX_CHECK(block.find(0U) == nullptr);

    block.rows[0].domain_id = 0U;
    block.rows[0].domain_class = TimeDomainClass::SystemClock;
    block.rows[0].native_anchor = 1'000U;
    block.rows[0].canonical_anchor_ns = 5'000;
    block.rows[0].scale_fwd_q32 = 1ULL << 32U; // one canonical ns per native unit
    block.rows[0].scale_rev_q32 = 1ULL << 32U;
    block.rows[0].flags = DomainCoefficient::has_reverse_bit;
    block.rows[0].valid_from_native = 0U;
    block.rows[0].valid_to_native = 10'000U;
    block.rows[0].valid_from_canonical = 5'000U;
    block.rows[0].valid_to_canonical = 6'000U;
    block.rows[1].domain_id = 7U;
    block.domain_count = 2U;

    const DomainCoefficient* found = block.find(7U);
    NX_CHECK(found != nullptr && found->domain_id == 7U);
    NX_CHECK(block.find(9U) == nullptr);

    // Affine evaluation, positive and negative deltas.
    NX_CHECK(canonical_from_native(block.rows[0], 1'500U) == 5'500);
    NX_CHECK(canonical_from_native(block.rows[0], 500U) == 4'500);
    NX_CHECK(native_from_canonical(block.rows[0], 5'500) == 1'500U);

    // Validity in both domains.
    NX_CHECK(contains_native(block.rows[0], 0U));
    NX_CHECK(contains_native(block.rows[0], 10'000U));
    NX_CHECK(!contains_native(block.rows[0], 10'001U));
    NX_CHECK(contains_canonical(block.rows[0], 5'000U));
    NX_CHECK(!contains_canonical(block.rows[0], 9'999U));
}

// --- serialization (spec chrono/time-base "Time-base serialization").

void test_time_base_serialization() {
    DomainCoefficient coefficient{};
    coefficient.domain_id = 3U;
    coefficient.domain_class = TimeDomainClass::DeviceClock;
    coefficient.lock = LockState::Holdover;
    coefficient.epoch = 42U;
    coefficient.native_anchor = 100U;
    coefficient.canonical_anchor_ns = 1'000;
    coefficient.scale_fwd_q32 = 2ULL << 20U;
    coefficient.unc_ns = 55U;
    coefficient.valid_from_native = 10U;
    coefficient.valid_to_native = 1'000U;
    coefficient.valid_from_canonical = 900U;
    coefficient.valid_to_canonical = 3'000U;

    NX_CHECK(!coefficient.to_string().empty());
    NX_CHECK(std::format("{}", coefficient) == coefficient.to_string());

    nlohmann::json row_json;
    to_json(row_json, coefficient);
    DomainCoefficient decoded{};
    from_json(row_json, decoded);
    NX_CHECK(decoded.domain_id == coefficient.domain_id);
    NX_CHECK(decoded.domain_class == coefficient.domain_class);
    NX_CHECK(decoded.lock == coefficient.lock);
    NX_CHECK(decoded.epoch == coefficient.epoch);
    NX_CHECK(decoded.native_anchor == coefficient.native_anchor);
    NX_CHECK(decoded.canonical_anchor_ns == coefficient.canonical_anchor_ns);
    NX_CHECK(decoded.scale_fwd_q32 == coefficient.scale_fwd_q32);
    NX_CHECK(decoded.unc_ns == coefficient.unc_ns);
    NX_CHECK(decoded.valid_to_canonical == coefficient.valid_to_canonical);

    // Hostile input leaves the row untouched.
    DomainCoefficient untouched = coefficient;
    from_json(nlohmann::json(7), untouched);
    NX_CHECK(untouched.domain_id == coefficient.domain_id);
    from_json(nlohmann::json{{"kind", 5}, {"lock", "bogus"}}, untouched);
    NX_CHECK(untouched.domain_class == coefficient.domain_class);
    NX_CHECK(untouched.lock == coefficient.lock);

    TimeSnapshot snapshot{};
    snapshot.generation = 11U;
    snapshot.boot_token = 0x1234U;
    snapshot.status = TimeBaseStatus::Ready;
    snapshot.sync_mode = SyncMode::PtpSynced;
    snapshot.service_flags = ServiceFlags{static_cast<std::uint32_t>(ServiceFlag::PhcPresent) |
                                          static_cast<std::uint32_t>(ServiceFlag::TaiSynchronized)};
    snapshot.monotonic_raw_ns = 5'000U;
    snapshot.domain_count = 1U;
    snapshot.domains[0].domain_id = k_realtime_domain_id;
    snapshot.domains[0].domain_class = TimeDomainClass::SystemClock;
    snapshot.domains[0].lock = LockState::Locked;
    snapshot.domains[0].epoch = 3U;
    snapshot.domains[0].value = 7'000U;
    NX_CHECK(!snapshot.to_string().empty());
    const nlohmann::json snapshot_json = snapshot;
    TimeSnapshot snapshot_back{};
    from_json(snapshot_json, snapshot_back);
    NX_CHECK(snapshot_back.generation == 11U);
    NX_CHECK(snapshot_back.sync_mode == SyncMode::PtpSynced);
    NX_CHECK(snapshot_back.service_flags.has(ServiceFlag::PhcPresent));
    NX_CHECK(snapshot_back.service_flags.has(ServiceFlag::TaiSynchronized));
    NX_CHECK(snapshot_back.domain_count == 1U);
    NX_CHECK(snapshot_back.domains[0].value == 7'000U);

    TimeBaseBlock block{};
    block.status = TimeBaseStatus::Degraded;
    block.domain_count = 1U;
    block.generation = 9U;
    block.rows[0] = coefficient;

    NX_CHECK(!block.to_string().empty());
    NX_CHECK(std::format("{}", block) == block.to_string());

    nlohmann::json block_json;
    to_json(block_json, block);
    TimeBaseBlock decoded_block{};
    from_json(block_json, decoded_block);
    NX_CHECK(decoded_block.status == TimeBaseStatus::Degraded);
    NX_CHECK(decoded_block.generation == 9U);
    NX_CHECK(decoded_block.domain_count == 1U);
    NX_CHECK(decoded_block.rows[0].domain_id == 3U);

    TimeBaseHistoryEntry entry{};
    entry.generation = 9U;
    entry.status = TimeBaseStatus::Degraded;
    entry.coefficient = coefficient;
    entry.set_boot_uuid("01234567-89ab-cdef-0123-456789abcdef");
    NX_CHECK(entry.boot_uuid_view() == "01234567-89ab-cdef-0123-456789abcdef");
    NX_CHECK(std::format("{}", entry) == entry.to_string());

    nlohmann::json entry_json;
    to_json(entry_json, entry);
    TimeBaseHistoryEntry decoded_entry{};
    from_json(entry_json, decoded_entry);
    NX_CHECK(decoded_entry.generation == 9U);
    NX_CHECK(decoded_entry.status == TimeBaseStatus::Degraded);
    NX_CHECK(decoded_entry.boot_uuid_view() == entry.boot_uuid_view());
    NX_CHECK(decoded_entry.coefficient.domain_id == 3U);
}

// --- seqlock (spec chrono/time-base "Shared-memory publication contract").

void test_time_base_seqlock() {
    using nexigen::chrono::detail::TimeBaseReader;
    using nexigen::chrono::detail::TimeBaseWriter;

    TimeBaseBlock shared{};
    TimeBaseWriter writer{shared};
    TimeBaseReader reader{shared};

    // Late reader: a fresh view immediately sees the published value.
    TimeBaseBlock staged{};
    staged.utc_offset_ns = 77;
    staged.rows[0].domain_id = 77U;
    staged.rows[0].canonical_anchor_ns = 77;
    staged.domain_count = 1U;
    writer.publish(staged, 12'345U);

    TimeBaseBlock snapshot{};
    NX_CHECK(reader.read(snapshot));
    NX_CHECK(snapshot.utc_offset_ns == 77);
    NX_CHECK(snapshot.rows[0].domain_id == 77U);
    NX_CHECK(reader.heartbeat_ns() == 12'345U);

    // Concurrent writer/reader: every accepted copy is internally consistent.
    std::atomic<bool> stop{false};
    std::atomic<std::uint64_t> accepted{0};
    std::atomic<std::uint64_t> torn{0};

    std::thread producer{[&] {
        for (std::uint64_t i = 0; i < 4'000U; ++i) {
            TimeBaseBlock stage{};
            stage.utc_offset_ns = static_cast<std::int64_t>(i);
            stage.rows[0].canonical_anchor_ns = static_cast<std::int64_t>(i);
            stage.rows[0].domain_id = static_cast<std::uint16_t>(i & 0xFFFFU);
            stage.domain_count = 1U;
            writer.publish(stage, i);
        }
        stop.store(true, std::memory_order_release);
    }};

    std::thread consumer{[&] {
        while (!stop.load(std::memory_order_acquire)) {
            TimeBaseBlock copy{};
            if (!reader.read(copy)) {
                continue;
            }
            // Both fields are written inside the same seqlock section.
            if (copy.utc_offset_ns != copy.rows[0].canonical_anchor_ns) {
                torn.fetch_add(1U, std::memory_order_relaxed);
            }
            accepted.fetch_add(1U, std::memory_order_relaxed);
        }
    }};

    producer.join();
    consumer.join();

    NX_CHECK(torn.load() == 0U);
    NX_CHECK(accepted.load() > 0U);

    // The final value is visible after the writer stopped.
    NX_CHECK(reader.read(snapshot));
    NX_CHECK(snapshot.utc_offset_ns == 3'999);
    NX_CHECK(reader.heartbeat_ns() == 3'999U);
}

// --- segment lifecycle (spec chrono/time-base "Shared-memory publication
// contract"; design D-B).

void test_time_base_segment() {
    using nexigen::chrono::detail::TimeBaseReader;
    using nexigen::chrono::detail::TimeBaseSegment;
    using nexigen::chrono::detail::TimeBaseWriter;

    const std::string name = "/nexigen-tb-test-" + std::to_string(static_cast<long>(::getpid()));

    // Malformed names are rejected before any OS call.
    NX_CHECK(!TimeBaseSegment::create("no-slash", 1U).has_value());
    NX_CHECK(!TimeBaseSegment::create("/two/slashes", 1U).has_value());

    auto created = TimeBaseSegment::create(name, 0xABCDEF0123456789ULL);
    NX_CHECK(created.has_value());
    if (!created) {
        return;
    }
    std::optional<TimeBaseSegment> owner{std::move(*created)};
    NX_CHECK(owner->is_owner());
    NX_CHECK(owner->block() != nullptr);
    NX_CHECK(owner->block()->boot_token == 0xABCDEF0123456789ULL);
    NX_CHECK(owner->block()->magic == time_base_magic);
    NX_CHECK(owner->block()->heartbeat_ns == 0U);

    // The segment is world-readable so an application under another uid can read it.
    const int probe_fd = ::shm_open(name.c_str(), O_RDONLY, 0);
    NX_CHECK(probe_fd >= 0);
    if (probe_fd >= 0) {
        struct stat info{};
        NX_CHECK(::fstat(probe_fd, &info) == 0);
        NX_CHECK((info.st_mode & 0777) == 0644);
        ::close(probe_fd);
    }

    // A reader attaches read-only and observes publications.
    auto attached = TimeBaseSegment::attach(name);
    NX_CHECK(attached.has_value());
    if (attached) {
        TimeBaseWriter writer{*owner->block()};
        TimeBaseReader reader{*attached->block()};

        TimeBaseBlock staged{};
        staged.utc_offset_ns = 123;
        staged.rows[0].domain_id = 5U;
        staged.domain_count = 1U;
        writer.publish(staged, 999U);

        TimeBaseBlock copy{};
        NX_CHECK(reader.read(copy));
        NX_CHECK(copy.utc_offset_ns == 123);
        NX_CHECK(copy.rows[0].domain_id == 5U);
        NX_CHECK(reader.heartbeat_ns() == 999U);
    }

    // A second live owner is rejected, not silently shared.
    auto second = TimeBaseSegment::create(name, 1U);
    NX_CHECK(!second.has_value());
    if (!second) {
        NX_CHECK(second.error() == nexigen::core::ErrorCode::Conflict);
    }

    // Clean shutdown unlinks the name.
    owner.reset();
    auto gone = TimeBaseSegment::attach(name);
    NX_CHECK(!gone.has_value());
    if (!gone) {
        NX_CHECK(gone.error() == nexigen::core::ErrorCode::NotFound);
    }

    // Crash takeover: a leftover segment with no live lock is re-initialized.
    const std::string stale = name + "-stale";
    {
        const int fd = ::shm_open(stale.c_str(), O_CREAT | O_EXCL | O_RDWR, 0644);
        NX_CHECK(fd >= 0);
        if (fd >= 0) {
            NX_CHECK(::ftruncate(fd, static_cast<off_t>(sizeof(TimeBaseBlock))) == 0);
            void* const mapping =
                ::mmap(nullptr, sizeof(TimeBaseBlock), PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
            NX_CHECK(mapping != MAP_FAILED);
            if (mapping != MAP_FAILED) {
                auto* const block = static_cast<TimeBaseBlock*>(mapping);
                *block = TimeBaseBlock{};
                block->boot_token = 111U;
                block->heartbeat_ns = 222U;
                ::munmap(mapping, sizeof(TimeBaseBlock));
            }
            ::close(fd); // releases the lock, leaves the name behind
        }
    }
    auto taken = TimeBaseSegment::create(stale, 333U);
    NX_CHECK(taken.has_value());
    if (taken) {
        NX_CHECK(taken->is_owner());
        NX_CHECK(taken->block()->boot_token == 333U);
        NX_CHECK(taken->block()->heartbeat_ns == 0U);
        NX_CHECK(taken->block()->generation == 2U);
    }
}

// --- daemon configuration (spec chrono/time-base "Daemon configuration").

std::string write_temp_text(std::string_view text) {
    char path[] = "/tmp/nexigen-tb-cfg-XXXXXX";
    const int fd = ::mkstemp(path);
    if (fd < 0) {
        return {};
    }
    const ssize_t ignored = ::write(fd, text.data(), text.size());
    static_cast<void>(ignored);
    ::close(fd);
    return path;
}

void test_time_base_daemon_config() {
    TimeBaseDaemonConfig config{};
    NX_CHECK(config.segment_name == "/nexigen-timebase");
    NX_CHECK(config.publish_interval == std::chrono::milliseconds{100});
    NX_CHECK(config.fit_window == std::chrono::seconds{10});
    NX_CHECK(config.publish_tsc);
    NX_CHECK(config.domains.empty());
    NX_CHECK(!config.to_string().empty());
    NX_CHECK(std::format("{}", config) == config.to_string());

    // JSON round-trip, including one declared hardware domain.
    config.segment_name = "/nexigen-timebase-test";
    config.publish_interval = std::chrono::milliseconds{250};
    config.domains.push_back(HardwareDomainConfig{"fpga0", "abcdef", "/dev/ptp2", 100'000'000U,
                                                  DeviceAccessMode::RegisterPair, true});
    config.sync_mode = SyncMode::PtpSynced;
    config.time_sync.max_step_ns = 2'000'000;
    config.time_sync.max_rate_ppm = 250;
    config.time_sync.min_window_ns = 50'000'000;
    config.holdover_window = std::chrono::milliseconds{750};
    config.record_session_path = "/tmp/nexigen-session";
    NX_CHECK(!config.domains.front().to_string().empty());
    NX_CHECK(std::format("{}", config.domains.front()) == config.domains.front().to_string());

    nlohmann::json document;
    to_json(document, config);
    TimeBaseDaemonConfig decoded{};
    from_json(document, decoded);
    NX_CHECK(decoded.segment_name == config.segment_name);
    NX_CHECK(decoded.publish_interval == config.publish_interval);
    NX_CHECK(decoded.domains.size() == 1U);
    NX_CHECK(decoded.domains.front().uuid == "abcdef");
    NX_CHECK(decoded.domains.front().mode == DeviceAccessMode::RegisterPair);
    NX_CHECK(decoded.domains.front().simulate);
    NX_CHECK(decoded.sync_mode == SyncMode::PtpSynced);
    NX_CHECK(decoded.time_sync.max_step_ns == 2'000'000);
    NX_CHECK(decoded.time_sync.max_rate_ppm == 250);
    NX_CHECK(decoded.time_sync.min_window_ns == 50'000'000);
    NX_CHECK(decoded.holdover_window == std::chrono::milliseconds{750});
    NX_CHECK(decoded.record_session_path == "/tmp/nexigen-session");

    // File loader: a valid document replaces the destination.
    const std::string valid = write_temp_text(R"({
        "segment_name": "/nexigen-timebase-loaded",
        "publish_interval_ns": 500000000,
        "fit_window_ns": 2000000000,
        "publish_tsc": false,
        "domains": [{"name": "fpga0", "uuid": "u", "access": "/dev/ptp2"}]
    })");
    NX_CHECK(!valid.empty());
    TimeBaseDaemonConfig loaded{};
    NX_CHECK(load_time_base_daemon_config(valid, loaded).has_value());
    NX_CHECK(loaded.segment_name == "/nexigen-timebase-loaded");
    NX_CHECK(loaded.publish_interval == std::chrono::milliseconds{500});
    NX_CHECK(!loaded.publish_tsc);
    NX_CHECK(loaded.domains.size() == 1U);
    ::unlink(valid.c_str());

    // Host-only configuration is valid.
    const std::string host_only = write_temp_text(R"({"domains": []})");
    TimeBaseDaemonConfig host{};
    NX_CHECK(load_time_base_daemon_config(host_only, host).has_value());
    NX_CHECK(host.domains.empty());
    ::unlink(host_only.c_str());

    // Missing, malformed and invalid documents are rejected without mutating `loaded`.
    TimeBaseDaemonConfig untouched = loaded;
    NX_CHECK(
        !load_time_base_daemon_config("/tmp/nexigen-tb-does-not-exist", untouched).has_value());
    NX_CHECK(untouched.segment_name == loaded.segment_name);

    const std::string malformed = write_temp_text("{ not json");
    NX_CHECK(!load_time_base_daemon_config(malformed, untouched).has_value());
    ::unlink(malformed.c_str());

    const std::string negative = write_temp_text(R"({"publish_interval_ns": -1})");
    NX_CHECK(!load_time_base_daemon_config(negative, untouched).has_value());
    ::unlink(negative.c_str());

    const std::string unnamed = write_temp_text(R"({"domains": [{"access": "/dev/ptp0"}]})");
    NX_CHECK(!load_time_base_daemon_config(unnamed, untouched).has_value());
    ::unlink(unnamed.c_str());

    NX_CHECK(untouched.segment_name == loaded.segment_name);
    NX_CHECK(untouched.publish_interval == loaded.publish_interval);
    NX_CHECK(untouched.domains.size() == loaded.domains.size());
}

// --- host-domain sampling (spec chrono/time-base "Canonical monotonic scale",
// "Domain coefficient row").

namespace {

struct FakeHostState {
    std::uint64_t mono_ns{1'000'000};
    std::uint64_t ticks{3'000'000};
    bool is_tsc{true};
    bool synchronized{true};
    bool fail_clock{false};
};

std::uint64_t fake_mono(void* self) noexcept {
    const auto* const state = static_cast<const FakeHostState*>(self);
    return state->fail_clock ? 0U : state->mono_ns;
}

std::uint64_t fake_ticks(void* self) noexcept {
    const auto* const state = static_cast<const FakeHostState*>(self);
    return state->ticks;
}

bool fake_is_tsc(void* self) noexcept {
    return static_cast<const FakeHostState*>(self)->is_tsc;
}

bool fake_sync(void* self) noexcept {
    return static_cast<const FakeHostState*>(self)->synchronized;
}

std::uint64_t real_mono(void*) noexcept {
    const auto now = Timestamp::now();
    return now ? now->nanoseconds() : 0U;
}

std::uint64_t real_ticks_x3(void*) noexcept {
    const auto now = Timestamp::now();
    return now ? now->nanoseconds() * 3U : 0U;
}

HostClockSource fake_source(FakeHostState& state) noexcept {
    return HostClockSource{&state,  &fake_mono, &fake_ticks,  nullptr,
                           nullptr, nullptr,    &fake_is_tsc, &fake_sync};
}

} // namespace

void test_host_domains() {
    FakeHostState state{};
    HostClockSource source = fake_source(state);
    TimeBaseBlock block{};

    // Canonical only when TSC publication is disabled.
    HostDomainResult result =
        sample_host_domains(source, block, 0x42U, 1'000'000U,
                            /*counter_scale_fwd_q32=*/0U, /*publish_tsc=*/false);
    NX_CHECK(result.rows == 1U);
    NX_CHECK(result.status == TimeBaseStatus::Ready);
    NX_CHECK(block.domain_count == 1U);
    NX_CHECK(block.rows[0].domain_id == k_monotonic_raw_domain_id);
    NX_CHECK(block.rows[0].domain_class == TimeDomainClass::SystemClock);
    NX_CHECK(block.rows[0].scale_fwd_q32 == (1ULL << 32U));
    NX_CHECK(block.rows[0].epoch == 0x42U);
    NX_CHECK(block.rows[0].unc_ns == 0U);
    NX_CHECK(canonical_from_native(block.rows[0], state.mono_ns) ==
             static_cast<std::int64_t>(state.mono_ns));

    // A non-TSC clocksource withdraws the TSC row and degrades the base.
    state.is_tsc = false;
    result = sample_host_domains(source, block, 0x42U, 1'000'000U, 1ULL << 32U, true);
    NX_CHECK(result.rows == 1U);
    NX_CHECK(result.status == TimeBaseStatus::Degraded);
    NX_CHECK(block.find(k_host_counter_domain_id) == nullptr);

    // A failed synchronization probe has the same effect.
    state.is_tsc = true;
    state.synchronized = false;
    result = sample_host_domains(source, block, 0x42U, 1'000'000U, 1ULL << 32U, true);
    NX_CHECK(result.rows == 1U);
    NX_CHECK(result.status == TimeBaseStatus::Degraded);

    // Eligible counter publishes a measured TSC row.
    state.synchronized = true;
    result = sample_host_domains(source, block, 0x42U, 1'000'000U, 1ULL << 32U, true);
    NX_CHECK(result.rows == 2U);
    NX_CHECK(result.status == TimeBaseStatus::Ready);
    const DomainCoefficient* tsc = block.find(k_host_counter_domain_id);
    NX_CHECK(tsc != nullptr);
    if (tsc != nullptr) {
        NX_CHECK(tsc->domain_class == TimeDomainClass::CycleCounter);
        NX_CHECK(tsc->lock == LockState::Locked);
        NX_CHECK(tsc->scale_fwd_q32 == (1ULL << 32U));
        NX_CHECK(canonical_from_native(*tsc, state.ticks + 500U) ==
                 static_cast<std::int64_t>(state.mono_ns + 500U));
    }

    // A zero counter read is not a valid domain.
    state.ticks = 0U;
    result = sample_host_domains(source, block, 0x42U, 1'000'000U, 1ULL << 32U, true);
    NX_CHECK(result.rows == 1U);
    NX_CHECK(result.status == TimeBaseStatus::Degraded);

    // A failed canonical read loses the base entirely.
    state.ticks = 3'000'000U;
    state.fail_clock = true;
    result = sample_host_domains(source, block, 0x42U, 1'000'000U, 1ULL << 32U, true);
    NX_CHECK(result.rows == 0U);
    NX_CHECK(result.status == TimeBaseStatus::Lost);
    NX_CHECK(block.domain_count == 0U);

    // Scale measurement over a real clock: the fake advances 3 ticks per ns.
    FakeHostState measured{};
    const HostClockSource real_source{&measured, &real_mono, &real_ticks_x3, nullptr,
                                      nullptr,   nullptr,    &fake_is_tsc,   &fake_sync};
    const std::uint64_t scale = measure_counter_scale(real_source, std::chrono::milliseconds{2});
    NX_CHECK(scale > 0U);
    const std::uint64_t expected = (1ULL << 32U) / 3U;
    const std::uint64_t tolerance = expected / 20U;
    NX_CHECK(scale + tolerance >= expected && scale <= expected + tolerance);
}

// --- estimator (spec chrono/time-base "Domain coefficient row"; design D-F).
void test_time_base_estimator() {
    TimeBaseEstimator estimator{};
    NX_CHECK(estimator.sample_count() == 0U);
    NX_CHECK(!estimator.estimate(5U, 1U, 1'000U).has_value());

    // A clean linear domain: one canonical ns per tick.
    for (std::uint64_t i = 0; i < 10U; ++i) {
        estimator.add_sample(i * 1'000U, 1'000'000 + static_cast<std::int64_t>(i * 1'000U));
    }
    NX_CHECK(estimator.sample_count() == 10U);
    auto row = estimator.estimate(5U, 7U, 5'000U);
    NX_CHECK(row.has_value());
    if (row) {
        NX_CHECK(row->domain_id == 5U);
        NX_CHECK(row->epoch == 7U);
        NX_CHECK(row->domain_class == TimeDomainClass::DeviceClock);
        NX_CHECK(row->lock == LockState::Locked);
        NX_CHECK(row->scale_fwd_q32 == (1ULL << 32U));
        NX_CHECK(row->unc_ns == 1U);
        NX_CHECK(row->valid_from_native == 0U);
        NX_CHECK(row->valid_to_native == 9'000U);
    }

    // Drift: 1.002 ns per tick.
    TimeBaseEstimator drifting{};
    for (std::uint64_t i = 0; i < 10U; ++i) {
        drifting.add_sample(i * 1'000U, 1'000'000 + static_cast<std::int64_t>(i * 1'002U));
    }
    auto drifted = drifting.estimate(6U, 1U, 5'000U);
    NX_CHECK(drifted.has_value());
    if (drifted) {
        const std::uint64_t expected_scale = (1'002ULL << 32U) / 1'000U;
        NX_CHECK(drifted->scale_fwd_q32 == expected_scale);
    }

    // One delayed pair inflates the uncertainty but keeps the lock.
    TimeBaseEstimator delayed{};
    for (std::uint64_t i = 0; i < 10U; ++i) {
        const std::int64_t extra = i == 4U ? 900 : 0;
        delayed.add_sample(i * 1'000U, 1'000'000 + static_cast<std::int64_t>(i * 1'000U) + extra);
    }
    auto delayed_row = delayed.estimate(8U, 1U, 5'000U);
    NX_CHECK(delayed_row.has_value());
    if (delayed_row) {
        NX_CHECK(delayed_row->lock == LockState::Locked);
        NX_CHECK(delayed_row->unc_ns >= 900U);
    }

    // The same spread with a tight bound becomes holdover.
    auto held = delayed.estimate(8U, 1U, 100U);
    NX_CHECK(held.has_value());
    if (held) {
        NX_CHECK(held->lock == LockState::Holdover);
    }

    // Step detection is a function of the residual bound, not a special path.
    NX_CHECK(TimeBaseEstimator::is_significant_change(*row, *row, 0U) == false);

    DomainCoefficient changed_epoch = *row;
    changed_epoch.epoch = 8U;
    NX_CHECK(TimeBaseEstimator::is_significant_change(changed_epoch, *row, 0U));

    DomainCoefficient changed_scale = *row;
    changed_scale.scale_fwd_q32 += 10'000U; // ~2.3 ppm
    NX_CHECK(TimeBaseEstimator::is_significant_change(changed_scale, *row, 0U));

    DomainCoefficient changed_anchor = *row;
    changed_anchor.native_anchor += 1'000U;
    changed_anchor.canonical_anchor_ns += 2'000; // 1000 ns drift beyond the map
    NX_CHECK(TimeBaseEstimator::is_significant_change(changed_anchor, *row, 10U));

    NX_CHECK(!estimator.to_string().empty());
    NX_CHECK(std::format("{}", estimator) == estimator.to_string());

    estimator.reset();
    NX_CHECK(estimator.sample_count() == 0U);
}

// --- client attach, required domains, holdover/fail-fast (spec
// chrono/time-base "Client attach and required domains", "Holdover and
// fail-fast").

void test_time_base_client() {
    const std::string name = "/nexigen-tb-client-" + std::to_string(static_cast<long>(::getpid()));
    auto segment = nexigen::chrono::detail::TimeBaseSegment::create(name, 0x77U);
    NX_CHECK(segment.has_value());
    if (!segment) {
        return;
    }
    nexigen::chrono::detail::TimeBaseWriter writer{*segment->block()};

    const auto now_result = Timestamp::now();
    NX_CHECK(now_result.has_value());
    if (!now_result) {
        return;
    }
    const std::uint64_t now = now_result->nanoseconds();

    auto publish_block =
        [&](std::uint64_t heartbeat, std::uint64_t tsc_epoch, std::uint64_t utc_valid_to,
            TimeBaseStatus status = TimeBaseStatus::Ready, bool with_device = false) {
            TimeBaseBlock staged{};
            staged.status = status;
            staged.boot_token = 0x77U;
            staged.utc_offset_ns = 1'000'000;
            staged.utc_unc_ns = 10U;
            staged.domain_count = with_device ? 4U : 3U;
            staged.rows[0].domain_id = k_monotonic_raw_domain_id;
            staged.rows[0].domain_class = TimeDomainClass::SystemClock;
            staged.rows[0].lock = LockState::Locked;
            staged.rows[0].scale_fwd_q32 = 1ULL << 32U;
            staged.rows[0].native_anchor = now;
            staged.rows[0].canonical_anchor_ns = static_cast<std::int64_t>(now);
            staged.rows[0].valid_from_native = now;
            staged.rows[0].valid_to_native = now + 10'000'000'000ULL;
            staged.rows[0].valid_from_canonical = now;
            staged.rows[0].valid_to_canonical = now + 10'000'000'000ULL;
            staged.rows[1].domain_id = k_host_counter_domain_id;
            staged.rows[1].domain_class = TimeDomainClass::CycleCounter;
            staged.rows[1].lock = LockState::Locked;
            staged.rows[1].epoch = tsc_epoch;
            staged.rows[1].scale_fwd_q32 = 1ULL << 32U;
            staged.rows[1].native_anchor = now * 3U;
            staged.rows[1].canonical_anchor_ns = static_cast<std::int64_t>(now);
            staged.rows[1].valid_from_native = now * 3U;
            staged.rows[1].valid_to_native = (now + 10'000'000'000ULL) * 3U;
            staged.rows[1].valid_from_canonical = now;
            staged.rows[1].valid_to_canonical = now + 10'000'000'000ULL;
            const std::uint32_t realtime_index = with_device ? 3U : 2U;
            DomainCoefficient& realtime = staged.rows[realtime_index];
            realtime.domain_id = k_realtime_domain_id;
            realtime.domain_class = TimeDomainClass::SystemClock;
            realtime.lock = LockState::Locked;
            realtime.epoch = 1U;
            realtime.native_anchor = now + 1'000'000U;
            realtime.canonical_anchor_ns = static_cast<std::int64_t>(now);
            realtime.scale_fwd_q32 = 1ULL << 32U;
            realtime.scale_rev_q32 = 1ULL << 32U;
            realtime.flags = DomainCoefficient::has_reverse_bit;
            realtime.valid_from_native = now + 1'000'000U;
            realtime.valid_to_native = utc_valid_to + 1'000'000U;
            realtime.valid_from_canonical = now;
            realtime.valid_to_canonical = utc_valid_to;
            if (with_device) {
                staged.rows[2].domain_id = 9U;
                staged.rows[2].domain_class = TimeDomainClass::DeviceClock;
                staged.rows[2].lock = LockState::Locked;
                staged.rows[2].epoch = 1U;
                staged.rows[2].scale_fwd_q32 = 1ULL << 32U;
                staged.rows[2].native_anchor = now;
                staged.rows[2].canonical_anchor_ns = static_cast<std::int64_t>(now);
                staged.rows[2].valid_from_native = now;
                staged.rows[2].valid_to_native = now + 10'000'000'000ULL;
                staged.rows[2].valid_from_canonical = now;
                staged.rows[2].valid_to_canonical = now + 10'000'000'000ULL;
            }
            writer.publish(staged, heartbeat);
        };

    publish_block(now, 5U, now + 1'000'000'000ULL);

    TimeBaseClientConfig config{};
    config.required_domains[0] = k_host_counter_domain_id;
    config.required_count = 1U;
    config.stale_after = std::chrono::milliseconds{50};
    config.holdover = std::chrono::milliseconds{100};

    // A block that is not usable must fail attach, even with every required row.
    publish_block(now, 5U, now + 1'000'000'000ULL, TimeBaseStatus::NotReady);
    auto not_ready = TimeBaseClient::attach(name, config);
    NX_CHECK(!not_ready.has_value());
    if (!not_ready.has_value()) {
        NX_CHECK(not_ready.error() == nexigen::core::ErrorCode::NotInitialized);
    }
    publish_block(now, 5U, now + 1'000'000'000ULL, TimeBaseStatus::Lost);
    auto lost = TimeBaseClient::attach(name, config);
    NX_CHECK(!lost.has_value());
    if (!lost.has_value()) {
        NX_CHECK(lost.error() == nexigen::core::ErrorCode::NotFound);
    }
    publish_block(now, 5U, now + 1'000'000'000ULL);

    auto attached = TimeBaseClient::attach(name, config);
    NX_CHECK(attached.has_value());
    if (!attached) {
        return;
    }
    TimeBaseClient client = std::move(*attached);
    NX_CHECK(client.state() == ClientState::Attached);
    NX_CHECK(client.status() == TimeBaseStatus::Ready);

    const DomainCoefficient* tsc = client.coefficient(k_host_counter_domain_id);
    NX_CHECK(tsc != nullptr);
    if (tsc != nullptr) {
        NX_CHECK(tsc->epoch == 5U);
    }

    // Conversion through the cached map.
    const auto canonical = client.canonical_from_native(k_host_counter_domain_id, now * 3U + 30U);
    NX_CHECK(canonical.has_value());
    if (canonical) {
        NX_CHECK(*canonical == static_cast<std::int64_t>(now + 30U));
    }
    NX_CHECK(!client.canonical_from_native(9U, 1U).has_value());

    // Any-to-any conversion through the hub.
    const DomainInstant from_tsc{k_host_counter_domain_id, 5U, now * 3U + 30U};
    const auto hub_instant = client.convert(from_tsc, k_monotonic_raw_domain_id);
    NX_CHECK(hub_instant.has_value());
    if (hub_instant.has_value()) {
        NX_CHECK(hub_instant->value == now + 30U);
    }
    const auto to_realtime = client.convert(from_tsc, k_realtime_domain_id);
    NX_CHECK(to_realtime.has_value());
    if (to_realtime.has_value()) {
        NX_CHECK(to_realtime->epoch == 1U);
    }
    const auto same = client.convert(from_tsc, k_host_counter_domain_id);
    NX_CHECK(same.has_value() && same->value == from_tsc.value);
    NX_CHECK(
        !client.convert(DomainInstant{k_host_counter_domain_id, 99U, 1U}, k_monotonic_raw_domain_id)
             .has_value());
    NX_CHECK(client.last_snapshot().has_value());

    // The 3-argument overload pins the destination epoch.
    NX_CHECK(client.convert(from_tsc, k_realtime_domain_id, 1U).has_value());
    NX_CHECK(!client.convert(from_tsc, k_realtime_domain_id, 2U).has_value());
    // A destination row that does not cover the converted hub instant fails
    // instead of extrapolating: realtime is only valid for one second here.
    NX_CHECK(!client
                  .convert(DomainInstant{k_monotonic_raw_domain_id, 0U, now + 2'000'000'000ULL},
                           k_realtime_domain_id)
                  .has_value());

    // UTC is derived from the published series and expires with it.
    NX_CHECK(client.utc_from_canonical(static_cast<std::int64_t>(now)).has_value());
    NX_CHECK(*client.utc_from_canonical(static_cast<std::int64_t>(now)) ==
             static_cast<std::int64_t>(now + 1'000'000U));
    NX_CHECK(
        !client.utc_from_canonical(static_cast<std::int64_t>(now + 2'000'000'000ULL)).has_value());

    // Freshness: attached -> holdover -> lost, all on the canonical scale.
    NX_CHECK(client.on_tick(now) == ClientState::Attached);
    NX_CHECK(client.on_tick(now + 60'000'000ULL) == ClientState::Holdover);
    NX_CHECK(client.on_tick(now + 101'000'000ULL) == ClientState::Lost);

    // A daemon restart that preserves the epoch is transparent.
    publish_block(now + 200'000'000ULL, 5U, now + 2'000'000'000ULL);
    NX_CHECK(client.on_tick(now + 200'000'000ULL) == ClientState::Attached);

    // A non-required domain may disappear without stopping the client.
    publish_block(now + 220'000'000ULL, 5U, now + 2'000'000'000ULL, TimeBaseStatus::Ready, true);
    NX_CHECK(client.on_tick(now + 220'000'000ULL) == ClientState::Attached);
    NX_CHECK(client.coefficient(9U) != nullptr);
    publish_block(now + 230'000'000ULL, 5U, now + 2'000'000'000ULL, TimeBaseStatus::Degraded,
                  false);
    NX_CHECK(client.on_tick(now + 230'000'000ULL) == ClientState::Attached);
    NX_CHECK(client.coefficient(9U) == nullptr);

    // An epoch change is immediate loss, without a holdover window.
    publish_block(now + 250'000'000ULL, 6U, now + 2'000'000'000ULL);
    NX_CHECK(client.on_tick(now + 250'000'000ULL) == ClientState::Lost);

    // Attaching while a required domain is absent fails outright.
    auto missing = TimeBaseClient::attach("/nexigen-tb-client-missing", config);
    NX_CHECK(!missing.has_value());

    NX_CHECK(!client.to_string().empty());
    NX_CHECK(std::format("{}", client) == client.to_string());

    // ClientState vocabulary.
    NX_CHECK(to_string(ClientState::Attached) == "attached");
    NX_CHECK(to_string(ClientState::Holdover) == "holdover");
    NX_CHECK(to_string(ClientState::Lost) == "lost");
    ClientState parsed = ClientState::Lost;
    NX_CHECK(from_string("holdover", parsed));
    NX_CHECK(parsed == ClientState::Holdover);
    NX_CHECK(!from_string("bogus", parsed));
    nlohmann::json state_json;
    to_json(state_json, ClientState::Attached);
    NX_CHECK(state_json == "attached");
    ClientState decoded_state = ClientState::Lost;
    from_json(state_json, decoded_state);
    NX_CHECK(decoded_state == ClientState::Attached);
    NX_CHECK(std::format("{}", ClientState::Holdover) == "holdover");
}

// --- publish loop (spec chrono/time-base "Coefficient history", "Time-base
// status"; design D-F).

namespace {

struct AdvancingFake {
    std::uint64_t offset_ns{0};
    bool is_tsc{true};
    bool synchronized{true};
    bool fail_clock{false};
};

std::uint64_t advancing_mono(void* self) noexcept {
    const auto* const state = static_cast<const AdvancingFake*>(self);
    const auto now = Timestamp::now();
    return (!now || state->fail_clock) ? 0U : now->nanoseconds() + state->offset_ns;
}

std::uint64_t advancing_ticks(void* self) noexcept {
    const auto* const state = static_cast<const AdvancingFake*>(self);
    const auto now = Timestamp::now();
    return now ? now->nanoseconds() * 3U + state->offset_ns * 3U : 0U;
}

bool advancing_is_tsc(void* self) noexcept {
    return static_cast<const AdvancingFake*>(self)->is_tsc;
}

bool advancing_sync(void* self) noexcept {
    return static_cast<const AdvancingFake*>(self)->synchronized;
}

HostClockSource advancing_source(AdvancingFake& state) noexcept {
    return HostClockSource{&state,  &advancing_mono, &advancing_ticks,  nullptr,
                           nullptr, nullptr,         &advancing_is_tsc, &advancing_sync};
}

struct RecordingHistory {
    std::vector<TimeBaseHistoryEntry> entries{};
};

bool record_history(void* self, const TimeBaseHistoryEntry& entry) noexcept {
    static_cast<RecordingHistory*>(self)->entries.push_back(entry);
    return true;
}

} // namespace

void test_time_base_publisher() {
    const std::string name = "/nexigen-tb-pub-" + std::to_string(static_cast<long>(::getpid()));
    auto segment = nexigen::chrono::detail::TimeBaseSegment::create(name, 0x55U);
    NX_CHECK(segment.has_value());
    if (!segment) {
        return;
    }

    AdvancingFake clock{};
    RecordingHistory history{};
    TimeBaseDaemonConfig config{};
    config.publish_interval = std::chrono::seconds{1};
    TimeBasePublisher publisher{*segment,
                                config,
                                advancing_source(clock),
                                TimeBasePublisher::HistorySink{&history, &record_history},
                                "01234567-89ab-cdef-0123-456789abcdef",
                                0x55U};
    publisher.measure(std::chrono::milliseconds{1});
    NX_CHECK(publisher.counter_scale() > 0U);

    const auto now_result = Timestamp::now();
    NX_CHECK(now_result.has_value());
    if (!now_result) {
        return;
    }
    const std::uint64_t now = now_result->nanoseconds();

    // First publication: canonical plus TSC rows, both recorded.
    publisher.publish(now);
    NX_CHECK(publisher.status() == TimeBaseStatus::Ready);
    NX_CHECK(publisher.generation() == 4U);
    NX_CHECK(history.entries.size() == 2U);
    if (history.entries.size() == 2U) {
        NX_CHECK(history.entries[0].status == TimeBaseStatus::Ready);
        NX_CHECK(history.entries[0].gap == false);
        NX_CHECK(history.entries[0].coefficient.domain_id == k_monotonic_raw_domain_id);
        NX_CHECK(history.entries[1].coefficient.domain_id == k_host_counter_domain_id);
    }

    nexigen::chrono::detail::TimeBaseReader reader{*segment->block()};
    TimeBaseBlock snapshot{};
    NX_CHECK(reader.read(snapshot));
    NX_CHECK(snapshot.status == TimeBaseStatus::Ready);
    NX_CHECK(snapshot.domain_count == 2U);
    NX_CHECK(snapshot.generation == publisher.generation());
    NX_CHECK(snapshot.heartbeat_ns == now);
    NX_CHECK(snapshot.service_flags.has(ServiceFlag::HostCounterAvailable));
    NX_CHECK(snapshot.service_flags.has(ServiceFlag::HostCounterSynchronized));

    // A second publication with an unchanged map records nothing new.
    publisher.publish(now + 1'000'000'000ULL);
    NX_CHECK(publisher.generation() == 6U);
    NX_CHECK(history.entries.size() == 2U);
    NX_CHECK(reader.read(snapshot));
    NX_CHECK(snapshot.heartbeat_ns == now + 1'000'000'000ULL);

    // Losing the TSC domain degrades the base and records its removal.
    clock.is_tsc = false;
    publisher.publish(now + 2'000'000'000ULL);
    NX_CHECK(publisher.status() == TimeBaseStatus::Degraded);
    NX_CHECK(history.entries.size() == 3U);
    if (history.entries.size() == 3U) {
        NX_CHECK(history.entries.back().coefficient.domain_id == k_host_counter_domain_id);
        NX_CHECK(history.entries.back().coefficient.lock == LockState::Unsupported);
    }

    // A clock failure loses the base entirely and records the removal.
    clock.fail_clock = true;
    publisher.publish(now + 3'000'000'000ULL);
    NX_CHECK(publisher.status() == TimeBaseStatus::Lost);
    NX_CHECK(history.entries.size() == 4U);
    if (history.entries.size() == 4U) {
        NX_CHECK(history.entries.back().coefficient.domain_id == k_monotonic_raw_domain_id);
        NX_CHECK(history.entries.back().coefficient.lock == LockState::Unsupported);
    }

    // Recovery, then a long silence is recorded as an explicit gap.
    clock.fail_clock = false;
    clock.is_tsc = true;
    publisher.publish(now + 4'000'000'000ULL);
    const std::size_t before_gap = history.entries.size();
    publisher.publish(now + 20'000'000'000ULL);
    NX_CHECK(history.entries.size() > before_gap);
    bool saw_gap = false;
    for (const auto& entry : history.entries) {
        if (entry.gap) {
            saw_gap = true;
        }
    }
    NX_CHECK(saw_gap);

    // Shutdown publishes a final Lost block.
    publisher.mark_lost(now + 21'000'000'000ULL);
    NX_CHECK(publisher.status() == TimeBaseStatus::Lost);
    NX_CHECK(reader.read(snapshot));
    NX_CHECK(snapshot.status == TimeBaseStatus::Lost);
    NX_CHECK(snapshot.heartbeat_ns == now + 21'000'000'000ULL);
}

// --- realtime step/slew verdicts (spec chrono/time-base "Time-base status";
// design D-E).

namespace {

struct SystemFake {
    std::uint64_t raw_ns{1'000'000'000ULL};
    std::int64_t realtime_offset_ns{0};
    bool fail_system{false};
};

std::uint64_t system_fake_mono(void* self) noexcept {
    return static_cast<SystemFake*>(self)->raw_ns;
}

std::uint64_t system_fake_ticks(void* self) noexcept {
    static_cast<void>(self);
    return 0U;
}

std::uint64_t system_fake_monotonic(void* self) noexcept {
    const auto* const state = static_cast<SystemFake*>(self);
    return state->fail_system ? 0U : state->raw_ns;
}

std::uint64_t system_fake_tai(void* self) noexcept {
    const auto* const state = static_cast<SystemFake*>(self);
    return state->fail_system ? 0U : state->raw_ns + 37'000'000'000ULL;
}

std::uint64_t system_fake_realtime(void* self) noexcept {
    const auto* const state = static_cast<SystemFake*>(self);
    if (state->fail_system) {
        return 0U;
    }
    return static_cast<std::uint64_t>(static_cast<std::int64_t>(state->raw_ns) +
                                      state->realtime_offset_ns);
}

HostClockSource system_fake_source(SystemFake& state) noexcept {
    return HostClockSource{&state,
                           &system_fake_mono,
                           &system_fake_ticks,
                           &system_fake_monotonic,
                           &system_fake_tai,
                           &system_fake_realtime,
                           nullptr,
                           nullptr};
}

} // namespace

void test_time_base_step_detection() {
    using nexigen::chrono::detail::TimeBaseSegment;
    using nexigen::chrono::detail::TimeBaseReader;

    const std::string name = "/nexigen-tb-step-" + std::to_string(static_cast<long>(::getpid()));
    auto segment = TimeBaseSegment::create(name, 0x5354U);
    NX_CHECK(segment.has_value());
    if (!segment) {
        return;
    }

    SystemFake clock{};
    RecordingHistory history{};
    TimeBaseDaemonConfig config{};
    config.segment_name = name;
    config.publish_tsc = false;
    config.publish_interval = std::chrono::milliseconds{100};
    TimeBasePublisher publisher{*segment,
                                config,
                                system_fake_source(clock),
                                TimeBasePublisher::HistorySink{&history, &record_history},
                                "01234567-89ab-cdef-0123-456789abcdef",
                                0x5354U};
    publisher.measure(std::chrono::milliseconds{1});

    publisher.publish(clock.raw_ns);
    TimeBaseBlock block{};
    NX_CHECK(TimeBaseReader{*segment->block()}.read(block));
    NX_CHECK(!block.service_flags.has(ServiceFlag::StepDetected));
    NX_CHECK(block.service_flags.has(ServiceFlag::TaiAvailable));
    NX_CHECK(block.service_flags.has(ServiceFlag::TaiSynchronized));

    // A two second realtime jump is a step, not a slew.
    clock.raw_ns += 100'000'000ULL;
    clock.realtime_offset_ns += 2'000'000'000LL;
    publisher.publish(clock.raw_ns);
    NX_CHECK(TimeBaseReader{*segment->block()}.read(block));
    NX_CHECK(block.service_flags.has(ServiceFlag::StepDetected));
    const DomainCoefficient* const realtime = block.find(k_realtime_domain_id);
    NX_CHECK(realtime != nullptr);
    if (realtime != nullptr) {
        NX_CHECK(realtime->lock == LockState::Holdover);
    }

    // A small rate error over the minimum window is a slew.
    clock.raw_ns += 100'000'000ULL;
    clock.realtime_offset_ns += 100'000LL;
    publisher.publish(clock.raw_ns);
    NX_CHECK(TimeBaseReader{*segment->block()}.read(block));
    NX_CHECK(block.service_flags.has(ServiceFlag::SlewActive));
    NX_CHECK(block.last_snapshot.domain_count >= 3U);
    NX_CHECK(block.last_snapshot.find(k_tai_domain_id) != nullptr);

    segment->unlink();
}

// --- synchronization-mode derivation (spec chrono/time-sync "Platform sync
// mode"; design D-E).

void test_sync_mode_transitions() {
    using nexigen::chrono::detail::TimeBaseSegment;
    using nexigen::chrono::detail::TimeBaseReader;

    const std::string name = "/nexigen-tb-sync-" + std::to_string(static_cast<long>(::getpid()));
    auto segment = TimeBaseSegment::create(name, 0x5359U);
    NX_CHECK(segment.has_value());
    if (!segment) {
        return;
    }
    SystemFake clock{};
    RecordingHistory history{};
    TimeBaseDaemonConfig config{};
    config.segment_name = name;
    config.publish_tsc = false;
    config.publish_interval = std::chrono::milliseconds{100};
    config.holdover_window = std::chrono::milliseconds{300};
    config.sync_mode = SyncMode::PtpSynced;
    TimeBasePublisher publisher{*segment,
                                config,
                                system_fake_source(clock),
                                TimeBasePublisher::HistorySink{&history, &record_history},
                                "01234567-89ab-cdef-0123-456789abcdef",
                                0x5359U};
    publisher.measure(std::chrono::milliseconds{1});
    TimeBaseBlock block{};

    // TAI is disciplined: the policy derives PtpSynced on its own.
    publisher.publish(clock.raw_ns);
    NX_CHECK(TimeBaseReader{*segment->block()}.read(block));
    NX_CHECK(block.sync_mode == SyncMode::PtpSynced);
    NX_CHECK(block.service_flags.has(ServiceFlag::PtpStackReady));

    // TAI loss starts the holdover window.
    clock.raw_ns += 100'000'000ULL;
    clock.fail_system = true;
    publisher.publish(clock.raw_ns);
    NX_CHECK(TimeBaseReader{*segment->block()}.read(block));
    NX_CHECK(block.sync_mode == SyncMode::Holdover);
    NX_CHECK(!block.service_flags.has(ServiceFlag::TaiSynchronized));

    // Inside the window it stays Holdover.
    clock.raw_ns += 100'000'000ULL;
    publisher.publish(clock.raw_ns);
    NX_CHECK(TimeBaseReader{*segment->block()}.read(block));
    NX_CHECK(block.sync_mode == SyncMode::Holdover);

    // Past the window it drops to Standalone.
    clock.raw_ns += 500'000'000ULL;
    publisher.publish(clock.raw_ns);
    NX_CHECK(TimeBaseReader{*segment->block()}.read(block));
    NX_CHECK(block.sync_mode == SyncMode::Standalone);

    // Forced policies are published verbatim.
    publisher.set_sync_mode(SyncMode::Holdover);
    clock.raw_ns += 100'000'000ULL;
    publisher.publish(clock.raw_ns);
    NX_CHECK(TimeBaseReader{*segment->block()}.read(block));
    NX_CHECK(block.sync_mode == SyncMode::Holdover);

    // Missing services: required bits stay set, readiness stays clear.
    publisher.set_sync_mode(SyncMode::PtpSynced);
    ServiceFlags required{};
    required.set(ServiceFlag::Ptp4lRequired);
    required.set(ServiceFlag::Phc2sysRequired);
    publisher.set_service_flags(required);
    clock.raw_ns += 100'000'000ULL;
    publisher.publish(clock.raw_ns);
    NX_CHECK(TimeBaseReader{*segment->block()}.read(block));
    NX_CHECK(block.service_flags.has(ServiceFlag::Ptp4lRequired));
    NX_CHECK(block.service_flags.has(ServiceFlag::Phc2sysRequired));
    NX_CHECK(!block.service_flags.has(ServiceFlag::PtpStackReady));
    NX_CHECK(block.sync_mode != SyncMode::PtpSynced);

    segment->unlink();
}

// --- hardware domains and 1 Hz snapshot (spec chrono/time-base "Hardware
// domain access modes", "1 Hz service snapshot"; design D-F, D-G).

void test_time_base_hardware() {
    using nexigen::chrono::detail::TimeBaseSegment;
    using nexigen::chrono::detail::TimeBaseWriter;

    const std::string name = "/nexigen-tb-hw-" + std::to_string(static_cast<long>(::getpid()));
    auto segment = TimeBaseSegment::create(name, 0x4857U);
    NX_CHECK(segment.has_value());
    if (!segment) {
        return;
    }

    AdvancingFake clock{};
    RecordingHistory history{};
    TimeBaseDaemonConfig config{};
    config.segment_name = name;
    config.domains.push_back(HardwareDomainConfig{"simulated", "phc-uuid", "/dev/ptp9", 25'000'000U,
                                                  DeviceAccessMode::PhcCrossTimestamp});
    TimeBasePublisher publisher{*segment,
                                config,
                                advancing_source(clock),
                                TimeBasePublisher::HistorySink{&history, &record_history},
                                "01234567-89ab-cdef-0123-456789abcdef",
                                0x4857U};

    SimulatedPhcDevice phc{nexigen::chrono::device_domain_base, 1U, 1ULL << 32U, 1'000U, 4U};
    SimulatedTimeBaseDevice registers{17U, 1U, 1ULL << 32U, 2'000U, 6U};
    RegisterPairDevice registers_reader{registers.register_pair_backend()};
    NX_CHECK(publisher.bind_device(phc.backend()));
    NX_CHECK(publisher.bind_device(registers_reader.backend()));
    NX_CHECK(publisher.device_count() == 2U);
    // The config declares one PHC domain; the register device is extra but the
    // declared domain is bound, so nothing is missing.
    NX_CHECK(publisher.missing_domain_count() == 0U);
    NX_CHECK(!publisher.bind_device(phc.backend()));

    publisher.measure(std::chrono::milliseconds{1});
    const auto now_result = Timestamp::now();
    NX_CHECK(now_result.has_value());
    if (!now_result) {
        return;
    }
    const std::uint64_t now = now_result->nanoseconds();
    publisher.set_sync_mode(SyncMode::Holdover);
    for (std::uint64_t step = 0U; step < 12U; ++step) {
        publisher.publish(now + step * 100'000'000ULL);
    }

    nexigen::chrono::detail::TimeBaseReader reader{*segment->block()};
    TimeBaseBlock snapshot{};
    NX_CHECK(reader.read(snapshot));
    NX_CHECK(snapshot.sync_mode == SyncMode::Holdover);
    const DomainCoefficient* const phc_row = snapshot.find(nexigen::chrono::device_domain_base);
    NX_CHECK(phc_row != nullptr);
    if (phc_row != nullptr) {
        NX_CHECK(phc_row->domain_class == TimeDomainClass::DeviceClock);
        NX_CHECK(phc_row->has_reverse());
        NX_CHECK(!phc_row->manual_pair());
    }
    const DomainCoefficient* const register_row = snapshot.find(17U);
    NX_CHECK(register_row != nullptr);
    if (register_row != nullptr) {
        NX_CHECK(register_row->latched());
        NX_CHECK(register_row->ptm_capable());
    }
    NX_CHECK(snapshot.service_flags.has(ServiceFlag::PhcPresent));
    NX_CHECK(snapshot.service_flags.has(ServiceFlag::DeclaredDomainsComplete));

    // The 1 Hz snapshot appears only after one second of publication time.
    NX_CHECK(snapshot.last_snapshot.domain_count >= 3U);
    NX_CHECK(snapshot.last_snapshot.generation != 0U);
    NX_CHECK(snapshot.last_snapshot.sync_mode == SyncMode::Holdover);
    NX_CHECK(snapshot.last_snapshot.find(nexigen::chrono::device_domain_base) != nullptr);

    // Manual pair: the published row must carry `manual_pair` and not PTM.
    struct ManualHwState {
        std::uint64_t ticks{5'000U};
        std::uint64_t raw{0U};
    };
    ManualHwState manual_state{};
    manual_state.raw = now > 1'000'000'000ULL ? now - 1'000'000'000ULL : 0U;
    ManualPairDevice manual_device{18U,
                                   1U,
                                   "legacy",
                                   &manual_state,
                                   +[](void* self) noexcept -> std::uint64_t {
                                       auto* const state = static_cast<ManualHwState*>(self);
                                       state->ticks += 1'000U;
                                       return state->ticks;
                                   },
                                   +[](void* self) noexcept -> std::uint64_t {
                                       auto* const state = static_cast<ManualHwState*>(self);
                                       state->raw += 1'000U;
                                       return state->raw;
                                   }};
    NX_CHECK(publisher.bind_device(manual_device.backend()));
    for (std::uint64_t step = 12U; step < 16U; ++step) {
        publisher.publish(now + step * 100'000'000ULL);
    }
    NX_CHECK(nexigen::chrono::detail::TimeBaseReader{*segment->block()}.read(snapshot));
    const DomainCoefficient* const manual_row = snapshot.find(18U);
    NX_CHECK(manual_row != nullptr);
    if (manual_row != nullptr) {
        NX_CHECK(manual_row->manual_pair());
        NX_CHECK(!manual_row->ptm_capable());
    }

    // A declared PHC that never binds degrades the base.
    TimeBaseDaemonConfig missing_config{};
    missing_config.segment_name = name;
    missing_config.domains.push_back(HardwareDomainConfig{"absent", "uuid", "/dev/ptp7", 0U,
                                                          DeviceAccessMode::PhcCrossTimestamp});
    const std::string missing_name =
        "/nexigen-tb-hw-missing-" + std::to_string(static_cast<long>(::getpid()));
    auto missing_segment = TimeBaseSegment::create(missing_name, 0x4D49U);
    NX_CHECK(missing_segment.has_value());
    if (missing_segment) {
        RecordingHistory missing_history{};
        TimeBasePublisher missing_publisher{
            *missing_segment,
            missing_config,
            advancing_source(clock),
            TimeBasePublisher::HistorySink{&missing_history, &record_history},
            "01234567-89ab-cdef-0123-456789abcdef",
            0x4D49U};
        NX_CHECK(missing_publisher.missing_domain_count() == 1U);
        missing_publisher.measure(std::chrono::milliseconds{1});
        missing_publisher.publish(now);
        NX_CHECK(missing_publisher.status() == TimeBaseStatus::Degraded);
        TimeBaseBlock missing_block{};
        NX_CHECK(
            nexigen::chrono::detail::TimeBaseReader{*missing_segment->block()}.read(missing_block));
        NX_CHECK(!missing_block.service_flags.has(ServiceFlag::DeclaredDomainsComplete));
        missing_segment->unlink();
    }

    segment->unlink();
}

// --- history file (spec chrono/time-base "Coefficient history").

void test_time_base_history_writer() {
    const std::string path =
        "/tmp/nexigen-tb-history-" + std::to_string(static_cast<long>(::getpid())) + ".jsonl";
    ::unlink(path.c_str());

    TimeBaseHistoryEntry first{};
    first.generation = 5U;
    first.status = TimeBaseStatus::Ready;
    first.gap = false;
    first.set_boot_uuid("01234567-89ab-cdef-0123-456789abcdef");
    first.coefficient.domain_id = 7U;
    first.coefficient.domain_class = TimeDomainClass::DeviceClock;
    first.coefficient.lock = LockState::Locked;

    TimeBaseHistoryEntry second{};
    second.generation = 6U;
    second.status = TimeBaseStatus::Degraded;
    second.gap = true;
    second.set_boot_uuid("01234567-89ab-cdef-0123-456789abcdef");

    {
        TimeBaseHistoryWriter writer{JsonFileSinkConfig{path, false}};
        NX_CHECK(writer.is_open());
        NX_CHECK(!writer.to_string().empty());
        NX_CHECK(writer.sink().append != nullptr);
        NX_CHECK(writer.append(first));
        NX_CHECK(writer.append(second));
        NX_CHECK(writer.flush().has_value());
    }

    std::ifstream input{path};
    NX_CHECK(input.is_open());
    std::string line_one;
    std::string line_two;
    NX_CHECK(static_cast<bool>(std::getline(input, line_one)));
    NX_CHECK(static_cast<bool>(std::getline(input, line_two)));
    if (!line_one.empty() && !line_two.empty()) {
        const nlohmann::json parsed_one = nlohmann::json::parse(line_one, nullptr, false);
        const nlohmann::json parsed_two = nlohmann::json::parse(line_two, nullptr, false);
        NX_CHECK(!parsed_one.is_discarded());
        NX_CHECK(!parsed_two.is_discarded());
        NX_CHECK(parsed_one["schema"] == 2);
        NX_CHECK(parsed_one["generation"] == 5);
        NX_CHECK(parsed_one["boot_uuid"] == "01234567-89ab-cdef-0123-456789abcdef");
        NX_CHECK(parsed_one["gap"] == false);
        NX_CHECK(parsed_one["coefficient"]["domain_id"] == 7);
        NX_CHECK(parsed_two["gap"] == true);
        NX_CHECK(parsed_two["generation"] == 6);
    }
    ::unlink(path.c_str());
}

void test_time_base_history_restart() {
    const std::string path = "/tmp/nexigen-tb-history-restart-" +
                             std::to_string(static_cast<long>(::getpid())) + ".jsonl";
    ::unlink(path.c_str());

    TimeBaseHistoryEntry first{};
    first.generation = 5U;
    first.status = TimeBaseStatus::Ready;
    first.set_boot_uuid("01234567-89ab-cdef-0123-456789abcdef");
    TimeBaseHistoryEntry second{};
    second.generation = 7U;
    second.status = TimeBaseStatus::Ready;
    second.set_boot_uuid("01234567-89ab-cdef-0123-456789abcdef");

    // Two daemon lifetimes open the same history path: earlier entries must
    // survive (spec "History survives a daemon restart").
    {
        TimeBaseHistoryWriter writer{JsonFileSinkConfig{path, true}};
        NX_CHECK(writer.is_open());
        NX_CHECK(writer.append(first));
        NX_CHECK(writer.flush().has_value());
    }
    {
        TimeBaseHistoryWriter writer{JsonFileSinkConfig{path, true}};
        NX_CHECK(writer.is_open());
        NX_CHECK(writer.append(second));
        NX_CHECK(writer.flush().has_value());
    }

    std::ifstream input{path};
    NX_CHECK(input.is_open());
    std::vector<std::string> lines;
    std::string line;
    while (std::getline(input, line)) {
        lines.push_back(line);
    }
    NX_CHECK(lines.size() == 2U);
    if (lines.size() == 2U) {
        const nlohmann::json line_one = nlohmann::json::parse(lines[0], nullptr, false);
        const nlohmann::json line_two = nlohmann::json::parse(lines[1], nullptr, false);
        NX_CHECK(!line_one.is_discarded() && line_one["generation"] == 5);
        NX_CHECK(!line_two.is_discarded() && line_two["generation"] == 7);
    }
    ::unlink(path.c_str());
}

void test_boot_identity() {
    char text[boot_token_text_size + 1U]{};
    const std::uint64_t token = 0x0123456789abcdefULL;
    const std::string_view rendered = format_boot_token(token, text);
    NX_CHECK(rendered.size() == 16U);
    NX_CHECK(rendered == "0123456789abcdef");

    std::uint64_t parsed = 0;
    NX_CHECK(parse_boot_token(rendered, parsed));
    NX_CHECK(parsed == token);
    NX_CHECK(!parse_boot_token("xyz", parsed));
    NX_CHECK(!parse_boot_token("0123456789abcde", parsed));

    NX_CHECK(boot_token_from_uuid("01234567-89ab-cdef-0123-456789abcdef") == 0x0123456789abcdefULL);
    // The live kernel token is either readable or zero; a nonzero value must
    // round-trip through its own text form.
    char live_text[boot_token_text_size + 1U]{};
    const std::uint64_t live = current_boot_token();
    static_cast<void>(format_boot_token(live, live_text));
    std::uint64_t live_parsed = 1;
    NX_CHECK(parse_boot_token(std::string_view(live_text, boot_token_text_size), live_parsed));
    NX_CHECK(live_parsed == live);
}

void test_time_base_clock() {
    using nexigen::chrono::detail::TimeBaseSegment;
    using nexigen::chrono::detail::TimeBaseWriter;

    const std::string name = "/nexigen-tb-clock-" + std::to_string(static_cast<long>(::getpid()));
    auto created = TimeBaseSegment::create(name, 0x00C10C4B00C10C4BULL);
    NX_CHECK(created.has_value());
    if (!created) {
        return;
    }
    std::optional<TimeBaseSegment> owner{std::move(*created)};

    TimeBaseBlock block{};
    block.status = TimeBaseStatus::Ready;
    block.boot_token = 0x00C10C4B00C10C4BULL;
    block.domain_count = 1U;
    block.utc_offset_ns = 1'000;
    block.rows[0].domain_id = k_realtime_domain_id;
    block.rows[0].domain_class = TimeDomainClass::SystemClock;
    block.rows[0].lock = LockState::Locked;
    block.rows[0].native_anchor = 1'000U;
    block.rows[0].canonical_anchor_ns = 0;
    block.rows[0].scale_fwd_q32 = 1ULL << 32U;
    block.rows[0].scale_rev_q32 = 1ULL << 32U;
    block.rows[0].flags = DomainCoefficient::has_reverse_bit;
    block.rows[0].valid_from_native = 0U;
    block.rows[0].valid_to_native = 10'000'001ULL;
    block.rows[0].valid_from_canonical = 0U;
    block.rows[0].valid_to_canonical = 10'000'000ULL;
    TimeBaseWriter writer{*owner->block()};
    writer.publish(block, 500U);

    NX_CHECK(TimeBaseClock::attach(name).has_value());
    NX_CHECK(TimeBaseClock::attached());
    NX_CHECK(TimeBaseClock::segment_name() == name);
    NX_CHECK(TimeBaseClock::boot_token() == 0x00C10C4B00C10C4BULL);
    NX_CHECK(TimeBaseClock::utc_from_canonical(2'000) == std::optional<std::int64_t>{3'000});
    // Beyond the published horizon the value stays unresolved.
    NX_CHECK(!TimeBaseClock::utc_from_canonical(10'000'001).has_value());
    NX_CHECK(!TimeBaseClock::utc_from_canonical(-1).has_value());

    // An uncalibrated publication produces no fabricated UTC.
    block.rows[0].lock = LockState::Unsupported;
    writer.publish(block, 600U);
    NX_CHECK(!TimeBaseClock::utc_from_canonical(2'000).has_value());

    TimeBaseClock::detach();
    NX_CHECK(!TimeBaseClock::attached());
    NX_CHECK(!TimeBaseClock::utc_from_canonical(2'000).has_value());
    NX_CHECK(!TimeBaseClock::boot_token().has_value());
}

void test_device_backend() {
    using nexigen::chrono::detail::TimeBaseSegment;
    using nexigen::chrono::detail::TimeBaseWriter;

    SimulatedTimeBaseDevice device{7U, 5U, 1ULL << 32U, 1'000U, 5U};
    auto backend = device.backend();
    NX_CHECK(backend.valid());
    NX_CHECK(backend.domain_id() == 7U);
    NX_CHECK(backend.epoch() == 5U);
    NX_CHECK(backend.mode() == DeviceAccessMode::RegisterPair);

    TimeBaseEstimator estimator{};
    for (int i = 0; i < 8; ++i) {
        NX_CHECK(backend.sample_into(estimator));
    }
    NX_CHECK(estimator.sample_count() == 8U);
    const std::optional<DomainCoefficient> row = estimator.estimate(7U, 5U, 1'000U);
    NX_CHECK(row.has_value());
    if (row.has_value()) {
        NX_CHECK(row->domain_id == 7U);
        NX_CHECK(row->domain_class == TimeDomainClass::DeviceClock);
        NX_CHECK(row->epoch == 5U);
        NX_CHECK(row->lock == LockState::Locked);
        NX_CHECK(row->scale_fwd_q32 > 0U);
        NX_CHECK(row->valid_to_native > row->valid_from_native);
    }

    // Every documented D1-D3 field is exercised by the simulated device.
    const DeviceRegisterMap& registers = device.registers();
    NX_CHECK(registers.magic == 0x4E58'5442U);
    NX_CHECK(registers.version == 1U);
    NX_CHECK(registers.caps != 0U);
    bool uuid_nonzero = false;
    for (const std::uint8_t byte : registers.uuid) {
        uuid_nonzero = uuid_nonzero || byte != 0U;
    }
    NX_CHECK(uuid_nonzero);
    NX_CHECK(registers.dev_ticks != 0U);
    NX_CHECK(registers.ptm_ticks != 0U);
    NX_CHECK(registers.pair_sequence != 0U);
    NX_CHECK(registers.pair_crc != 0U);
    NX_CHECK(registers.lock_state == 1U);
    NX_CHECK(registers.sync_epoch == 5U);
    NX_CHECK(registers.last_exchange_dev_ticks != 0U);
    NX_CHECK(registers.prop_delay_ns == 5U);
    NX_CHECK(registers.residual_ns != 0U);

    // PHC cross-timestamp backend.
    SimulatedPhcDevice phc{9U, 2U, 1ULL << 32U, 500U, 4U};
    auto phc_backend = phc.backend();
    NX_CHECK(phc_backend.valid());
    NX_CHECK(phc_backend.mode() == DeviceAccessMode::PhcCrossTimestamp);
    TimeBaseCorrelationPair phc_pair{};
    NX_CHECK(phc_backend.read(phc_pair));
    NX_CHECK(phc_pair.uncertainty_ns > 0U);
    NX_CHECK(phc_pair.latched);
    NX_CHECK(!phc_pair.ptm_capable);

    // Manual pair: the software read window becomes latency and uncertainty.
    struct ManualState {
        std::uint64_t ticks{5'000U};
        std::uint64_t raw{1'000'000U};
    };
    ManualState manual_state{};
    ManualPairDevice manual{11U,
                            3U,
                            "legacy",
                            &manual_state,
                            +[](void* self) noexcept -> std::uint64_t {
                                auto* state = static_cast<ManualState*>(self);
                                state->ticks += 1'000U;
                                return state->ticks;
                            },
                            +[](void* self) noexcept -> std::uint64_t {
                                auto* state = static_cast<ManualState*>(self);
                                state->raw += 2U;
                                return state->raw;
                            }};
    auto manual_backend = manual.backend();
    NX_CHECK(manual_backend.valid());
    NX_CHECK(manual_backend.mode() == DeviceAccessMode::ManualPair);
    TimeBaseCorrelationPair manual_pair{};
    NX_CHECK(manual_backend.read(manual_pair));
    NX_CHECK(manual_pair.read_latency_ns == 2U);
    NX_CHECK(manual_pair.uncertainty_ns == 2U);
    NX_CHECK(!manual_pair.latched);
    NX_CHECK(!manual_pair.ptm_capable);

    // Access-mode vocabulary round-trips.
    NX_CHECK(to_string(DeviceAccessMode::PhcCrossTimestamp) == "phc_cross_timestamp");
    NX_CHECK(to_string(DeviceAccessMode::RegisterPair) == "register_pair");
    NX_CHECK(to_string(DeviceAccessMode::ManualPair) == "manual_pair");
    DeviceAccessMode parsed_mode = DeviceAccessMode::ManualPair;
    NX_CHECK(from_string("register_pair", parsed_mode));
    NX_CHECK(parsed_mode == DeviceAccessMode::RegisterPair);
    NX_CHECK(!from_string("bogus", parsed_mode));
    NX_CHECK(std::format("{}", DeviceAccessMode::PhcCrossTimestamp) == "phc_cross_timestamp");
    nlohmann::json mode_json;
    to_json(mode_json, DeviceAccessMode::ManualPair);
    NX_CHECK(mode_json == "manual_pair");
    DeviceAccessMode decoded_mode = DeviceAccessMode::RegisterPair;
    from_json(mode_json, decoded_mode);
    NX_CHECK(decoded_mode == DeviceAccessMode::ManualPair);
    from_json(nlohmann::json(7), decoded_mode);
    NX_CHECK(decoded_mode == DeviceAccessMode::ManualPair);

    // Register-pair protocol: two consecutive stable reads are required, the
    // D1/D2 CRC is validated, and a stale sequence is rejected.
    SimulatedTimeBaseDevice latch_device{19U, 3U, 1ULL << 32U, 1'000U, 5U};
    latch_device.advance_pair();
    RegisterPairDevice latch_reader{
        19U, 3U, "latched", &latch_device, +[](void* self, DeviceRegisterMap& out) noexcept {
            return static_cast<SimulatedTimeBaseDevice*>(self)->snapshot_registers(out);
        }};
    TimeBaseCorrelationPair latched{};
    NX_CHECK(latch_reader.read_pair(latched));
    NX_CHECK(latched.latched);
    NX_CHECK(latched.ptm_capable);
    NX_CHECK(latched.native_ticks != 0U);
    NX_CHECK(!latch_reader.read_pair(latched)); // stale: no new latch edge
    latch_device.advance_pair();
    NX_CHECK(latch_reader.read_pair(latched));

    // A torn read (sequence moves between the two snapshots) is discarded.
    struct TornState {
        DeviceRegisterMap map{};
        std::uint32_t calls{0};
    };
    TornState torn_state{};
    RegisterPairDevice torn_reader{
        20U, 1U, "torn", &torn_state, +[](void* self, DeviceRegisterMap& out) noexcept {
            auto* const state = static_cast<TornState*>(self);
            ++state->calls;
            state->map.pair_sequence = state->calls;
            state->map.dev_ticks = 1'000U + state->calls;
            state->map.ptm_ticks = 2'000U + state->calls;
            const auto* const bytes = reinterpret_cast<const std::uint8_t*>(&state->map);
            state->map.pair_crc = nexigen::core::integrity::crc32c(
                std::span<const std::uint8_t>(bytes, offsetof(DeviceRegisterMap, pair_crc)));
            out = state->map;
            return true;
        }};
    TimeBaseCorrelationPair torn_pair{};
    NX_CHECK(!torn_reader.read_pair(torn_pair));

    // A snapshot with a valid sequence but a bad CRC is rejected.
    struct BadCrcState {
        DeviceRegisterMap map{};
    };
    BadCrcState bad_state{};
    bad_state.map.pair_sequence = 1U;
    bad_state.map.dev_ticks = 1'000U;
    bad_state.map.ptm_ticks = 2'000U;
    bad_state.map.pair_crc = 0xDEADBEEFU;
    RegisterPairDevice bad_crc_reader{21U, 1U, "bad-crc", &bad_state,
                                      +[](void* self, DeviceRegisterMap& out) noexcept {
                                          out = static_cast<BadCrcState*>(self)->map;
                                          return true;
                                      }};
    NX_CHECK(!bad_crc_reader.read_pair(torn_pair));

    // The real PHC backend reports open/read errors without throwing.
    NX_CHECK(!PhcCrossTimestampClock::open("").has_value());
    NX_CHECK(!PhcCrossTimestampClock::open("/nonexistent/ptp99").has_value());
    auto null_phc = PhcCrossTimestampClock::open("/dev/null");
    if (null_phc.has_value()) {
        TimeBaseCorrelationPair null_pair{};
        NX_CHECK(!null_phc->read_pair(null_pair)); // ioctl on /dev/null fails
        PhcCrossTimestampClock moved{std::move(*null_phc)};
        NX_CHECK(moved.is_open());
        NX_CHECK(!null_phc->is_open());
    }

    // The estimated row is publishable and readable through the client path.
    const std::string name = "/nexigen-tb-device-" + std::to_string(static_cast<long>(::getpid()));
    auto created = TimeBaseSegment::create(name, 0x4445'5649U);
    NX_CHECK(created.has_value());
    if (created.has_value() && row.has_value()) {
        std::optional<TimeBaseSegment> owner{std::move(*created)};
        TimeBaseBlock block{};
        block.status = TimeBaseStatus::Ready;
        block.domain_count = 2U;
        block.rows[0].domain_id = k_monotonic_raw_domain_id;
        block.rows[0].domain_class = TimeDomainClass::SystemClock;
        block.rows[0].lock = LockState::Locked;
        block.rows[0].scale_fwd_q32 = 1ULL << 32U;
        block.rows[1] = *row;
        TimeBaseWriter{*owner->block()}.publish(block, 1U);

        auto attached_client = TimeBaseClient::attach(name, TimeBaseClientConfig{});
        NX_CHECK(attached_client.has_value());
        if (attached_client.has_value()) {
            NX_CHECK(attached_client->coefficient(7U) != nullptr);
            const std::uint64_t native = row->native_anchor;
            const auto projected = attached_client->canonical_from_native(7U, native);
            NX_CHECK(projected.has_value());
            if (projected.has_value()) {
                NX_CHECK(*projected == canonical_from_native(*row, native));
            }
        }
        owner->unlink();
    }
}

// --- fixed-point 2-state Kalman filter (spec chrono/time-base "Canonical
// monotonic scale"; design D-D).

void test_kalman2() {
    using nexigen::chrono::detail::Kalman2;
    using nexigen::chrono::detail::Kalman2Config;

    Kalman2Config config{};
    config.measurement_ns2 = 1'000'000; // 1 us sigma
    config.process_offset_ns2 = 1'000;
    config.process_drift_q12 = 1LL << 12U;
    Kalman2 filter{config};
    NX_CHECK(!filter.initialized());
    NX_CHECK(filter.predict_offset_ns(0U) == 0);

    // Lock onto a constant 1 us offset with alternating measurement noise.
    for (std::uint64_t i = 1U; i <= 100U; ++i) {
        const std::int64_t noise = (i % 2U == 0U) ? 400 : -400;
        filter.update(1'000 + noise, 100'000'000U);
    }
    NX_CHECK(filter.initialized());
    NX_CHECK(filter.offset_ns() > 500);
    NX_CHECK(filter.offset_ns() < 1'500);
    NX_CHECK(filter.drift_ppb() > -2'000'000'000LL);
    NX_CHECK(filter.drift_ppb() < 2'000'000'000LL);
    NX_CHECK(filter.offset_variance_ns2() >= 0);

    // A linear drift of 20 ns per second is tracked.
    Kalman2Config drift_config{};
    drift_config.measurement_ns2 = 1'000'000;
    drift_config.process_offset_ns2 = 1;
    drift_config.process_drift_q12 = 1LL << 12U;
    Kalman2 drifting{drift_config};
    std::int64_t truth = 500;
    for (std::uint64_t i = 0U; i < 200U; ++i) {
        drifting.update(truth, 100'000'000U);
        truth += 2; // 20 ns/s at 10 Hz
    }
    NX_CHECK(drifting.drift_ppb() > 5'000'000'000LL);
    NX_CHECK(drifting.drift_ppb() < 25'000'000'000LL);
    const std::int64_t predicted = drifting.predict_offset_ns(1'000'000'000U);
    NX_CHECK(predicted >= drifting.offset_ns());
    NX_CHECK(predicted <= drifting.offset_ns() + 60);

    // A step moves the estimate toward the new offset within a few samples.
    Kalman2 stepping{config};
    for (std::uint64_t i = 0U; i < 20U; ++i) {
        stepping.update(0, 100'000'000U);
    }
    for (std::uint64_t i = 0U; i < 20U; ++i) {
        stepping.update(1'000'000, 100'000'000U);
    }
    NX_CHECK(stepping.offset_ns() > 500'000);

    // Reset drops the history.
    stepping.reset(0);
    NX_CHECK(stepping.offset_ns() == 0);
    NX_CHECK(stepping.drift_ppb() == 0);
}

// --- kernel discipline and daemon runtime bootstrap (spec chrono/time-base
// "Time-base status", "Real-time daemon execution"; design D-E, D-H).

void test_adjtimex_status() {
#if defined(__linux__)
    const auto discipline = read_adjtimex();
    NX_CHECK(discipline.has_value());
    if (!discipline.has_value()) {
        return;
    }
    NX_CHECK(!discipline->to_string().empty());
    NX_CHECK(std::format("{}", *discipline) == discipline->to_string());
    NX_CHECK(!discipline->locked() || !discipline->unsynchronized);
    const nlohmann::json encoded = *discipline;
    AdjtimexStatus decoded{};
    from_json(encoded, decoded);
    NX_CHECK(decoded.kernel_status == discipline->kernel_status);
    NX_CHECK(decoded.freq_ppb == discipline->freq_ppb);
    NX_CHECK(decoded.pll_engaged == discipline->pll_engaged);
    from_json(nlohmann::json(42), decoded);
    NX_CHECK(decoded.kernel_status == discipline->kernel_status);
#endif
}

void test_daemon_runtime() {
    using nexigen::chrono::DaemonRuntimeConfig;
    using nexigen::chrono::DaemonRuntimeReport;

    DaemonRuntimeConfig config{};
    config.lock_memory = false;
    DaemonRuntimeReport report{};
    NX_CHECK(apply_daemon_runtime(config, report).has_value());
    NX_CHECK(!report.memory_locked);

    config.thread.set_apply_mode(static_cast<nexigen::core::ThreadApplyMode>(99));
    const auto invalid = apply_daemon_runtime(config, report);
    NX_CHECK(!invalid.has_value());
    NX_CHECK(invalid.error() == nexigen::core::ErrorCode::InvalidArgument);
}

// --- TAI absolute deadlines (spec chrono/time-base "Any-to-any domain
// conversion" companion helper; design D-H).

void test_tai_deadline() {
    NX_CHECK(!tai_deadline(-1).has_value());
    const std::optional<timespec> deadline = tai_deadline(1'500'000'250);
    NX_CHECK(deadline.has_value());
    if (deadline.has_value()) {
        NX_CHECK(deadline->tv_sec == 1);
        NX_CHECK(deadline->tv_nsec == 500'000'250L);
    }
    // A deadline in the past returns immediately.
    NX_CHECK(sleep_until_tai(1'000'000'000).has_value());
    NX_CHECK(!sleep_until_tai(-5).has_value());

    // Sleeping to a future TAI deadline waits for roughly that long.
#if defined(CLOCK_TAI)
    timespec now_tai{};
    NX_CHECK(::clock_gettime(CLOCK_TAI, &now_tai) == 0);
    timespec now_raw{};
    NX_CHECK(::clock_gettime(CLOCK_MONOTONIC_RAW, &now_raw) == 0);
    const std::int64_t tai_ns =
        static_cast<std::int64_t>(now_tai.tv_sec) * 1'000'000'000LL + now_tai.tv_nsec;
    NX_CHECK(sleep_until_tai(tai_ns + 2'000'000).has_value());
    timespec after_raw{};
    NX_CHECK(::clock_gettime(CLOCK_MONOTONIC_RAW, &after_raw) == 0);
    const std::uint64_t before_ns = static_cast<std::uint64_t>(now_raw.tv_sec) * 1'000'000'000ULL +
                                    static_cast<std::uint64_t>(now_raw.tv_nsec);
    const std::uint64_t after_ns = static_cast<std::uint64_t>(after_raw.tv_sec) * 1'000'000'000ULL +
                                   static_cast<std::uint64_t>(after_raw.tv_nsec);
    NX_CHECK(after_ns >= before_ns);
#endif
}

// --- contract edge cases (spec chrono/time-base "Domain coefficient row",
// "Time-base serialization"; design D-C, D-E).

void test_time_base_contract_gaps() {
    // A row with a zero or missing reverse map can never be published.
    DomainCoefficient row{};
    row.domain_id = 7U;
    row.scale_fwd_q32 = 1ULL << 32U;
    row.scale_rev_q32 = 1ULL << 32U;
    row.flags = DomainCoefficient::has_reverse_bit;
    NX_CHECK(TimeBasePublisher::row_publishable(row));
    row.scale_fwd_q32 = 0U;
    NX_CHECK(!TimeBasePublisher::row_publishable(row));
    row.scale_fwd_q32 = 1ULL << 32U;
    row.scale_rev_q32 = 0U;
    NX_CHECK(!TimeBasePublisher::row_publishable(row));
    row.scale_rev_q32 = 1ULL << 32U;
    row.flags = 0U;
    NX_CHECK(!TimeBasePublisher::row_publishable(row));

    // Snapshot diagnostics are deterministic.
    TimeSnapshot snapshot{};
    snapshot.generation = 3U;
    snapshot.status = TimeBaseStatus::Ready;
    snapshot.sync_mode = SyncMode::PtpSynced;
    NX_CHECK(std::format("{}", snapshot) == snapshot.to_string());
    NX_CHECK(!snapshot.to_string().empty());

    // Hostile JSON leaves block, snapshot and history entry unchanged.
    TimeBaseBlock block{};
    block.generation = 9U;
    block.status = TimeBaseStatus::Ready;
    block.domain_count = 1U;
    block.rows[0].domain_id = k_monotonic_raw_domain_id;
    const TimeBaseBlock block_before = block;
    from_json(nlohmann::json(42), block);
    NX_CHECK(block.generation == block_before.generation);
    NX_CHECK(block.domain_count == block_before.domain_count);
    NX_CHECK(block.rows[0].domain_id == block_before.rows[0].domain_id);

    const TimeSnapshot snapshot_before = snapshot;
    from_json(nlohmann::json("bogus"), snapshot);
    NX_CHECK(snapshot.generation == snapshot_before.generation);
    NX_CHECK(snapshot.sync_mode == snapshot_before.sync_mode);

    TimeBaseHistoryEntry entry{};
    entry.generation = 11U;
    entry.status = TimeBaseStatus::Degraded;
    entry.sync_mode = SyncMode::Holdover;
    const TimeBaseHistoryEntry entry_before = entry;
    from_json(nlohmann::json(nullptr), entry);
    NX_CHECK(entry.generation == entry_before.generation);
    NX_CHECK(entry.status == entry_before.status);
    NX_CHECK(entry.sync_mode == entry_before.sync_mode);

    // Runtime flags reflect the applied thread report, not a blanket claim.
    DaemonRuntimeReport report{};
    report.memory_locked = true;
    ServiceFlags flags = runtime_service_flags(SyncMode::PtpSynced, report);
    NX_CHECK(!flags.has(ServiceFlag::RtSchedApplied));
    NX_CHECK(flags.has(ServiceFlag::MlockApplied));
    NX_CHECK(flags.has(ServiceFlag::Ptp4lRequired));
    NX_CHECK(flags.has(ServiceFlag::Phc2sysRequired));
    NX_CHECK(!runtime_service_flags(SyncMode::Standalone, report).has(ServiceFlag::Ptp4lRequired));

    // The configured TimeSyncPolicy actually drives the publication verdict:
    // with a zero step tolerance any offset change is a detected step.
    const std::string policy_name =
        "/nexigen-tb-policy-" + std::to_string(static_cast<long>(::getpid()));
    auto policy_segment = nexigen::chrono::detail::TimeBaseSegment::create(policy_name, 0x504FU);
    NX_CHECK(policy_segment.has_value());
    if (policy_segment) {
        SystemFake clock{};
        TimeBaseDaemonConfig policy_config{};
        policy_config.segment_name = policy_name;
        policy_config.publish_tsc = false;
        policy_config.time_sync.max_step_ns = 0;
        RecordingHistory policy_history{};
        TimeBasePublisher policy_publisher{
            *policy_segment,
            policy_config,
            system_fake_source(clock),
            TimeBasePublisher::HistorySink{&policy_history, &record_history},
            "01234567-89ab-cdef-0123-456789abcdef",
            0x504FU};
        policy_publisher.measure(std::chrono::milliseconds{1});
        policy_publisher.publish(clock.raw_ns);
        clock.raw_ns += 100'000'000ULL;
        clock.realtime_offset_ns += 1; // one nanosecond is above a zero tolerance
        policy_publisher.publish(clock.raw_ns);
        TimeBaseBlock policy_block{};
        NX_CHECK(
            nexigen::chrono::detail::TimeBaseReader{*policy_segment->block()}.read(policy_block));
        NX_CHECK(policy_block.service_flags.has(ServiceFlag::StepDetected));
        policy_segment->unlink();
    }
}

void test_allocation_free_paths() {
    using nexigen::chrono::detail::TimeBaseSegment;
    using nexigen::chrono::detail::TimeBaseWriter;

    const std::string name = "/nexigen-tb-alloc-" + std::to_string(static_cast<long>(::getpid()));
    auto created = TimeBaseSegment::create(name, 0xA110U);
    NX_CHECK(created.has_value());
    if (!created) {
        return;
    }
    std::optional<TimeBaseSegment> owner{std::move(*created)};
    TimeBaseBlock block{};
    block.status = TimeBaseStatus::Ready;
    block.domain_count = 2U;
    block.utc_offset_ns = 1'000;
    block.rows[0].domain_id = k_monotonic_raw_domain_id;
    block.rows[0].domain_class = TimeDomainClass::SystemClock;
    block.rows[0].lock = LockState::Locked;
    block.rows[0].scale_fwd_q32 = 1ULL << 32U;
    block.rows[0].valid_from_native = 0U;
    block.rows[0].valid_to_native = 1'000'000'000ULL;
    block.rows[1].domain_id = k_host_counter_domain_id;
    block.rows[1].domain_class = TimeDomainClass::CycleCounter;
    block.rows[1].lock = LockState::Locked;
    block.rows[1].epoch = 1U;
    block.rows[1].scale_fwd_q32 = 1ULL << 32U;
    block.rows[1].valid_from_native = 0U;
    block.rows[1].valid_to_native = 1'000'000'000ULL;
    TimeBaseWriter writer{*owner->block()};
    writer.publish(block, 1U);

    auto attached = TimeBaseClient::attach(name, TimeBaseClientConfig{});
    NX_CHECK(attached.has_value());
    if (!attached) {
        return;
    }
    NX_CHECK(TimeBaseClock::attach(name).has_value());
    // Warm the thread-local block caches before measuring.
    static_cast<void>(attached->refresh());
    static_cast<void>(attached->on_tick(2U));
    static_cast<void>(attached->canonical_from_native(k_host_counter_domain_id, 10U));
    static_cast<void>(TimeBaseClock::utc_from_canonical(10));

    const std::size_t before = ::mallinfo2().uordblks;
    for (std::uint64_t i = 0; i < 2'000U; ++i) {
        writer.publish(block, i + 1U);
        static_cast<void>(attached->refresh());
        static_cast<void>(attached->on_tick(i + 1U));
        static_cast<void>(attached->canonical_from_native(k_host_counter_domain_id, i * 10U));
        static_cast<void>(attached->utc_from_canonical(static_cast<std::int64_t>(i * 10U)));
        static_cast<void>(TimeBaseClock::utc_from_canonical(static_cast<std::int64_t>(i * 10U)));
    }
    const std::size_t after = ::mallinfo2().uordblks;
    NX_CHECK(after == before);

    // 10 Hz publication with a hardware domain and the 1 Hz snapshot must stay
    // allocation-free after warm-up.
    const std::string pub_name =
        "/nexigen-tb-alloc-pub-" + std::to_string(static_cast<long>(::getpid()));
    auto pub_created = TimeBaseSegment::create(pub_name, 0xA111U);
    NX_CHECK(pub_created.has_value());
    if (pub_created) {
        std::optional<TimeBaseSegment> pub_owner{std::move(*pub_created)};
        AdvancingFake pub_clock{};
        TimeBaseDaemonConfig pub_config{};
        pub_config.segment_name = pub_name;
        TimeBasePublisher publisher{*pub_owner,
                                    pub_config,
                                    advancing_source(pub_clock),
                                    TimeBasePublisher::HistorySink{},
                                    "01234567-89ab-cdef-0123-456789abcdef",
                                    0xA111U};
        SimulatedPhcDevice alloc_phc{nexigen::chrono::device_domain_base, 1U, 1ULL << 32U, 1'000U,
                                     4U};
        NX_CHECK(publisher.bind_device(alloc_phc.backend()));
        publisher.measure(std::chrono::milliseconds{1});
        const auto alloc_now = Timestamp::now();
        NX_CHECK(alloc_now.has_value());
        const std::uint64_t base_ns = alloc_now ? alloc_now->nanoseconds() : 0U;
        for (std::uint64_t warm = 0U; warm < 5U; ++warm) {
            publisher.publish(base_ns + warm * 100'000'000ULL);
        }
        const std::size_t publish_before = ::mallinfo2().uordblks;
        for (std::uint64_t i = 0U; i < 200U; ++i) {
            publisher.publish(base_ns + i * 100'000'000ULL);
            static_cast<void>(attached->refresh());
            static_cast<void>(attached->convert(
                DomainInstant{nexigen::chrono::device_domain_base, 0U, 10'000U}, k_tai_domain_id));
            static_cast<void>(attached->last_snapshot());
        }
        const std::size_t publish_after = ::mallinfo2().uordblks;
        NX_CHECK(publish_after == publish_before);
        pub_owner->unlink();
    }

    TimeBaseClock::detach();
    owner->unlink();
}

} // namespace

int main() {
    static_assert(std::is_enum_v<TimeBaseStatus>);
    static_assert(std::is_enum_v<TimeDomainClass>);
    static_assert(std::is_enum_v<LockState>);
    static_assert(sizeof(TimeBaseStatus) == 1U);

    run_suite("time_base_status", test_time_base_status);
    run_suite("domain_kind", test_domain_kind);
    run_suite("lock_state", test_lock_state);
    run_suite("time_base_layout", test_time_base_layout);
    run_suite("time_base_serialization", test_time_base_serialization);
    run_suite("time_base_seqlock", test_time_base_seqlock);
    run_suite("time_base_segment", test_time_base_segment);
    run_suite("time_base_daemon_config", test_time_base_daemon_config);
    run_suite("host_domains", test_host_domains);
    run_suite("time_base_client", test_time_base_client);
    run_suite("time_base_estimator", test_time_base_estimator);
    run_suite("time_base_publisher", test_time_base_publisher);
    run_suite("time_base_history_writer", test_time_base_history_writer);
    run_suite("time_base_history_restart", test_time_base_history_restart);
    run_suite("boot_identity", test_boot_identity);
    run_suite("time_base_clock", test_time_base_clock);
    run_suite("device_backend", test_device_backend);
    run_suite("time_base_hardware", test_time_base_hardware);
    run_suite("time_base_step_detection", test_time_base_step_detection);
    run_suite("sync_mode_transitions", test_sync_mode_transitions);
    run_suite("kalman2", test_kalman2);
    run_suite("adjtimex_status", test_adjtimex_status);
    run_suite("daemon_runtime", test_daemon_runtime);
    run_suite("time_base_contract_gaps", test_time_base_contract_gaps);
    run_suite("tai_deadline", test_tai_deadline);
    run_suite("allocation_free_paths", test_allocation_free_paths);

    std::printf("%d checks, %d failed\n", g_checks, g_failures);
    return g_failures;
}
