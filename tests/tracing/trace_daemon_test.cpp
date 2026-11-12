// Tests nexigen::tracing TraceDaemon internals and end-to-end session
// (change add-trace-daemon; spec tracing/trace-daemon). COM-gated.
#include "tracing_test_support.hpp"

#include <array>
#include <atomic>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <functional>
#include <iterator>
#include <string>
#include <string_view>
#include <thread>
#include <vector>

#include <cerrno>
#include <fcntl.h>
#include <sys/mman.h>
#include <sys/resource.h>
#include <unistd.h>

#include <nexigen/chrono/civil_time.hpp>
#include <nexigen/com/event.hpp>
#include <nexigen/com/node.hpp>
#include <nexigen/com/publisher.hpp>
#include <nexigen/com/service_name.hpp>
#include <nexigen/exec/app_state.hpp>
#include <nexigen/exec/application_state.hpp>
#include <nexigen/sm/recovery_notification.hpp>
#include <nexigen/tracing/detail/span_ring.hpp>
#include <nexigen/tracing/detail/trace_daemon_session.hpp>
#include <nexigen/tracing/detail/trace_dump.hpp>
#include <nexigen/tracing/iceoryx2_trace_sink.hpp>
#include <nexigen/tracing/iceoryx2_trace_sink_config.hpp>
#include <nexigen/tracing/span_frame.hpp>
#include <nexigen/tracing/trace_daemon_config.hpp>

namespace com = nexigen::com;
namespace tracing = nexigen::tracing;
namespace detail = nexigen::tracing::detail;
namespace sm = nexigen::sm;

namespace {

using tracing::SpanFrame;
using tracing::SpanRecord;
using tracing::TraceDaemonConfig;

constexpr const char* temp_root = "/tmp/opencode/nexigen-trace-daemon-tests";

std::string make_dir(const char* tag) {
    const std::string dir = std::string{temp_root} + "/" + tag + "-" +
                            std::to_string(static_cast<unsigned long>(::getpid()));
    std::error_code error;
    std::filesystem::remove_all(dir, error);
    std::filesystem::create_directories(dir, error);
    return dir;
}

std::string unique_name(const char* base) {
    return std::string{base} + std::to_string(static_cast<unsigned long>(::getpid()));
}

SpanFrame make_frame(std::uint64_t sequence, std::uint64_t start_ns, const char* name) {
    SpanFrame frame{};
    frame.trace_id = tracing::TraceId{0x1000ULL + sequence};
    frame.span_id = tracing::SpanId{0x2000ULL + sequence};
    frame.parent_span_id = tracing::SpanId{0x3000ULL + sequence};
    frame.kind = tracing::SpanKind::Server;
    frame.status = tracing::SpanStatus::Ok;
    frame.thread_id = static_cast<std::uint32_t>(10U + sequence);
    frame.start_ns = start_ns;
    frame.end_ns = start_ns + 50U;
    frame.line = static_cast<std::uint32_t>(sequence);
    frame.sequence = sequence;
    frame.publish_ts_ns = start_ns + 60U;
    std::snprintf(frame.name, sizeof(frame.name), "%s", name);
    frame.app_id[0] = 'T';
    frame.app_id[1] = 'E';
    frame.app_id[2] = 'S';
    frame.app_id[3] = 'T';
    frame.pid = 4242U;
    return frame;
}

SpanRecord make_record(std::uint64_t index) {
    SpanRecord record{};
    record.trace_id = tracing::TraceId{0xAAAA0000ULL + index};
    record.span_id = tracing::SpanId{0xBBBB0000ULL + index};
    record.kind = tracing::SpanKind::Client;
    record.status = tracing::SpanStatus::Ok;
    record.thread_id = 77U;
    record.start = chrono::Timestamp{1'000'000U + index};
    record.end = chrono::Timestamp{1'000'050U + index};
    record.file_name = "/src/daemon_test.cpp";
    record.function_name = "run_pipeline";
    record.line = 10U;
    record.column = 2U;
    std::snprintf(record.name, sizeof(record.name), "record_%llu",
                  static_cast<unsigned long long>(index));
    return record;
}

bool read_dump(const std::string& path, detail::TraceDumpHeader& header,
               std::vector<SpanFrame>& frames) {
    const int fd = ::open(path.c_str(), O_RDONLY | O_CLOEXEC);
    if (fd < 0) {
        return false;
    }
    auto read_all = [fd](void* destination, std::size_t bytes) {
        auto* out = static_cast<char*>(destination);
        std::size_t offset = 0U;
        while (offset < bytes) {
            const ssize_t got = ::read(fd, out + offset, bytes - offset);
            if (got > 0) {
                offset += static_cast<std::size_t>(got);
            } else if (got < 0 && errno == EINTR) {
                continue;
            } else {
                return false;
            }
        }
        return true;
    };
    bool ok = read_all(&header, sizeof(header));
    if (ok) {
        ok = std::memcmp(header.magic, detail::trace_dump_magic.data(),
                         detail::trace_dump_magic.size()) == 0 &&
             header.version == detail::trace_dump_format_version &&
             header.header_size == detail::trace_dump_header_size;
    }
    if (ok) {
        frames.resize(static_cast<std::size_t>(header.frame_count));
        for (SpanFrame& frame : frames) {
            if (!read_all(&frame, sizeof(frame))) {
                ok = false;
                break;
            }
        }
    }
    static_cast<void>(::close(fd));
    return ok;
}

std::uint32_t count_dumps(const std::string& directory) {
    std::uint32_t count = 0U;
    std::error_code error;
    for (const auto& entry : std::filesystem::directory_iterator(directory, error)) {
        const std::string name = entry.path().filename().string();
        if (name.rfind("incident_trace_", 0) == 0 && name.size() > 5 &&
            name.substr(name.size() - 5) == ".nxtr") {
            ++count;
        }
    }
    return count;
}

void test_config() {
    TraceDaemonConfig config{};
    NX_CHECK(config.validate().has_value());
    NX_CHECK(config.to_string().find("Nexigen/SM/Recovery") != std::string::npos);
    NX_CHECK(std::format("{}", config) == config.to_string());
    NX_CHECK(tracing::to_string(config) == config.to_string());

    TraceDaemonConfig invalid = config;
    invalid.service_name.clear();
    NX_CHECK(!invalid.validate().has_value());
    invalid = config;
    invalid.recovery_event_name.clear();
    NX_CHECK(!invalid.validate().has_value());
    invalid = config;
    invalid.app_name.clear();
    NX_CHECK(!invalid.validate().has_value());
    invalid = config;
    invalid.drain_interval_ms = 4U;
    NX_CHECK(!invalid.validate().has_value());
    invalid = config;
    invalid.drain_interval_ms = 1001U;
    NX_CHECK(!invalid.validate().has_value());
    invalid = config;
    invalid.ring_capacity_records = 0U;
    NX_CHECK(!invalid.validate().has_value());
    invalid = config;
    invalid.dump_directory = "relative/path";
    NX_CHECK(!invalid.validate().has_value());
    invalid = config;
    invalid.max_dump_files = 0U;
    NX_CHECK(!invalid.validate().has_value());

    nlohmann::json json{};
    tracing::to_json(json, config);
    TraceDaemonConfig parsed{};
    tracing::from_json(json, parsed);
    NX_CHECK(parsed.service_name == config.service_name);
    NX_CHECK(parsed.recovery_service_name == config.recovery_service_name);
    NX_CHECK(parsed.recovery_event_name == config.recovery_event_name);
    NX_CHECK(parsed.subscriber_max_buffer_depth == config.subscriber_max_buffer_depth);
    NX_CHECK(parsed.drain_interval_ms == config.drain_interval_ms);
    NX_CHECK(parsed.ring_capacity_records == config.ring_capacity_records);
    NX_CHECK(parsed.dump_directory == config.dump_directory);
    NX_CHECK(parsed.max_dump_files == config.max_dump_files);
    NX_CHECK(parsed.trigger_on_recovery == config.trigger_on_recovery);
    NX_CHECK(parsed.app_name == config.app_name);

    TraceDaemonConfig untouched{};
    const std::string before = untouched.dump_directory;
    tracing::from_json(nlohmann::json{"nope"}, untouched);
    NX_CHECK(untouched.dump_directory == before);
}

void test_ring() {
    detail::SpanRing ring{4U};
    NX_CHECK(ring.capacity() == 4U);
    NX_CHECK(ring.count() == 0U);
    NX_CHECK(ring.empty());

    for (std::uint64_t i = 0; i < 6U; ++i) {
        ring.push(make_frame(i, 1000U + i, "ring"));
    }
    NX_CHECK(ring.count() == 4U);
    NX_CHECK(ring.overwritten() == 2U);
    NX_CHECK(ring.at(0U).sequence == 2U);
    NX_CHECK(ring.at(3U).sequence == 5U);

    ring.clear();
    NX_CHECK(ring.count() == 0U);
    NX_CHECK(ring.overwritten() == 2U); // cumulative counter survives clear

    ring.push(make_frame(9U, 9000U, "after_clear"));
    NX_CHECK(ring.count() == 1U);
    NX_CHECK(ring.at(0U).sequence == 9U);

    // Per-push allocation freedom: the ring's storage exists before this window.
    const std::size_t before = arena_used();
    for (std::uint64_t i = 0; i < 1000U; ++i) {
        ring.push(make_frame(i, i, "alloc"));
    }
    NX_CHECK(arena_used() == before);
}

void test_dump() {
    static_assert(sizeof(detail::TraceDumpHeader) == 96U);
    static_assert(alignof(detail::TraceDumpHeader) == 8U);
    static_assert(offsetof(detail::TraceDumpHeader, trigger_group) == 48U);
    static_assert(offsetof(detail::TraceDumpHeader, trigger_ts_ns) == 80U);

    const std::string dir = make_dir("dump");
    const std::string path = dir + "/incident_trace_20260101T000000Z.nxtr";

    detail::SpanRing ring{4U};
    for (std::uint64_t i = 0; i < 4U; ++i) {
        ring.push(make_frame(i, 1000U + i * 100U, "dump"));
    }
    NX_CHECK(detail::write_trace_dump(path, ring, "ADAS_FG", 777U, true).has_value());

    detail::TraceDumpHeader header{};
    std::vector<SpanFrame> frames;
    NX_CHECK(read_dump(path, header, frames));
    NX_CHECK(header.flags == 1U);
    NX_CHECK(header.frame_count == 4U);
    NX_CHECK(header.first_start_ns == 1000U);
    NX_CHECK(header.last_end_ns == 1350U);
    NX_CHECK(header.ring_overwrites == 0U);
    NX_CHECK(std::string_view{header.trigger_group} == "ADAS_FG");
    NX_CHECK(header.trigger_ts_ns == 777U);
    NX_CHECK(frames.size() == 4U);
    for (std::size_t i = 0; i < frames.size(); ++i) {
        NX_CHECK(frames[i].sequence == i);
        NX_CHECK(frames[i].start_ns == 1000U + i * 100U);
        NX_CHECK(frames[i].name_view() == "dump");
        NX_CHECK(frames[i].app_id_text() == "TEST");
    }

    // Unknown magic/version must be detectable, not silently interpreted.
    const std::string corrupt_magic = dir + "/corrupt_magic.nxtr";
    const std::string corrupt_version = dir + "/corrupt_version.nxtr";
    {
        std::ifstream in(path.c_str(), std::ios::binary);
        std::string bytes((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
        std::string magic_bad = bytes;
        magic_bad[0] = 'X';
        std::ofstream(corrupt_magic.c_str(), std::ios::binary)
            .write(magic_bad.data(), static_cast<std::streamsize>(magic_bad.size()));
        std::string version_bad = bytes;
        version_bad[8] = 99;
        std::ofstream(corrupt_version.c_str(), std::ios::binary)
            .write(version_bad.data(), static_cast<std::streamsize>(version_bad.size()));
    }
    detail::TraceDumpHeader ignored_header{};
    std::vector<SpanFrame> ignored_frames;
    NX_CHECK(!read_dump(corrupt_magic, ignored_header, ignored_frames));
    NX_CHECK(!read_dump(corrupt_version, ignored_header, ignored_frames));

    // A failed write leaves neither the final file nor its temp.
    const std::string missing = dir + "/missing/incident_trace_x.nxtr";
    NX_CHECK(!detail::write_trace_dump(missing, ring, "X", 1U).has_value());
    NX_CHECK(::access(missing.c_str(), F_OK) != 0);
    NX_CHECK(::access((missing + ".tmp").c_str(), F_OK) != 0);
}

void test_naming_and_pruning() {
    const std::string dir = make_dir("retention");
    const std::string nested = dir + "/a/b/c";
    NX_CHECK(detail::create_directories(nested).has_value());
    NX_CHECK(std::filesystem::is_directory(nested));

    const std::string early = detail::snapshot_file_name(1'000'000'000'000'000'000LL, 0U);
    const std::string later = detail::snapshot_file_name(2'000'000'000'000'000'000LL, 0U);
    NX_CHECK(early < later);
    NX_CHECK(detail::snapshot_file_name(1'000'000'000'000'000'000LL, 0U) !=
             detail::snapshot_file_name(1'000'000'000'000'000'000LL, 1U));
    NX_CHECK(early.rfind("incident_trace_", 0) == 0);
    NX_CHECK(early.substr(early.size() - 5) == ".nxtr");

    const std::array<const char*, 5> names{
        "incident_trace_20260101T000000Z.nxtr", "incident_trace_20260102T000000Z.nxtr",
        "incident_trace_20260103T000000Z.nxtr", "incident_trace_20260104T000000Z.nxtr",
        "incident_trace_20260105T000000Z.nxtr"};
    for (const char* name : names) {
        std::ofstream file((dir + "/" + name).c_str(), std::ios::binary);
        file.put('\0');
    }
    std::ofstream((dir + "/unrelated.txt").c_str(), std::ios::binary).put('\0');

    NX_CHECK(detail::prune_dumps(dir, 3U) == 2U);
    NX_CHECK(!std::filesystem::exists(dir + "/" + names[0]));
    NX_CHECK(!std::filesystem::exists(dir + "/" + names[1]));
    NX_CHECK(std::filesystem::exists(dir + "/" + names[2]));
    NX_CHECK(std::filesystem::exists(dir + "/" + names[3]));
    NX_CHECK(std::filesystem::exists(dir + "/" + names[4]));
    NX_CHECK(std::filesystem::exists(dir + "/unrelated.txt"));
    NX_CHECK(count_dumps(dir) == 3U);
}

struct E2eContext {
    TraceDaemonConfig config{};
    std::string dir{};
};

E2eContext make_e2e_context(const char* tag, bool trigger) {
    E2eContext context{};
    context.dir = make_dir(tag);
    context.config.service_name = unique_name("Nexigen/Test/TracedSpans");
    context.config.recovery_service_name = unique_name("Nexigen/Test/TracedRecovery");
    context.config.recovery_event_name = unique_name("Nexigen/Test/TracedEvent");
    context.config.subscriber_max_buffer_depth = 64U;
    context.config.drain_interval_ms = 10U;
    context.config.ring_capacity_records = 8U;
    context.config.dump_directory = context.dir;
    context.config.max_dump_files = 3U;
    context.config.trigger_on_recovery = trigger;
    context.config.app_name = unique_name("TRCD");
    return context;
}

bool wait_for(detail::TraceDaemonSession& session, const std::function<bool()>& predicate,
              int attempts = 200) {
    for (int i = 0; i < attempts; ++i) {
        if (predicate()) {
            return true;
        }
        if (!session.wait_and_process().has_value()) {
            return false;
        }
    }
    return predicate();
}

bool publish_recovery(com::Publisher<sm::RecoveryNotification>& publisher,
                      com::EventNotifier& notifier, const char* group) {
    sm::RecoveryNotification notification{};
    std::snprintf(notification.function_group, sizeof(notification.function_group), "%s", group);
    auto loaned = publisher.loan_uninit();
    if (!loaned.has_value()) {
        return false;
    }
    auto sample = loaned->write_payload(notification);
    if (!publisher.send(std::move(sample)).has_value()) {
        return false;
    }
    return notifier.notify().has_value();
}

void test_session_e2e() {
    E2eContext context = make_e2e_context("e2e", true);
    const TraceDaemonConfig& config = context.config;

    auto test_node = com::Node::create();
    NX_CHECK(test_node.has_value());
    if (!test_node.has_value()) {
        return;
    }

    const auto readiness_name =
        com::ServiceName::create(nexigen::exec::application_state_service(config.app_name).c_str());
    NX_CHECK(readiness_name.has_value());
    auto readiness = com::EventListener::create(*test_node, *readiness_name);
    NX_CHECK(readiness.has_value());

    detail::TraceDaemonSession session{config};
    NX_CHECK(session.start().has_value());
    NX_CHECK(session.started());
    NX_CHECK(session.ring() != nullptr);
    NX_CHECK(session.ring()->capacity() == config.ring_capacity_records);
    NX_CHECK(session.to_string().find(config.service_name) != std::string::npos);

    std::uint64_t readiness_id = 999U;
    std::uint64_t readiness_activations = 0U;
    if (readiness.has_value()) {
        const auto waited = readiness->try_wait([&](com::EventId id) {
            ++readiness_activations;
            readiness_id = id.as_value();
        });
        NX_CHECK(waited.has_value());
        NX_CHECK(readiness_activations >= 1U);
        NX_CHECK(readiness_id == static_cast<std::uint64_t>(nexigen::exec::AppState::Running));
    }

    tracing::Iceoryx2TraceSinkConfig sink_config{};
    sink_config.service_name = config.service_name;
    sink_config.app_id = "TEST";
    sink_config.subscriber_max_buffer_depth = config.subscriber_max_buffer_depth;
    tracing::Iceoryx2TraceSink sink{sink_config, &*test_node};
    NX_CHECK(sink.start().has_value());
    std::this_thread::sleep_for(std::chrono::milliseconds(100));

    for (std::uint64_t i = 0; i < 3U; ++i) {
        NX_CHECK(sink.on_span(make_record(i)).has_value());
    }
    NX_CHECK(wait_for(session, [&]() { return session.ring()->count() == 3U; }));
    if (session.ring()->count() == 3U) {
        for (std::size_t i = 0; i < 3U; ++i) {
            NX_CHECK(session.ring()->at(i).sequence == i);
            NX_CHECK(session.ring()->at(i).name_view() ==
                     std::string_view{"record_" + std::to_string(i)});
        }
    }
    NX_CHECK(session.spans_received() == 3U);

    const auto recovery_name = com::ServiceName::create(config.recovery_service_name.c_str());
    const auto event_name = com::ServiceName::create(config.recovery_event_name.c_str());
    NX_CHECK(recovery_name.has_value() && event_name.has_value());
    auto recovery_publisher =
        com::Publisher<sm::RecoveryNotification>::create(*test_node, *recovery_name);
    auto event_notifier = com::EventNotifier::create(*test_node, *event_name);
    NX_CHECK(recovery_publisher.has_value() && event_notifier.has_value());
    if (!recovery_publisher.has_value() || !event_notifier.has_value()) {
        session.stop();
        return;
    }
    std::this_thread::sleep_for(std::chrono::milliseconds(100));

    NX_CHECK(publish_recovery(*recovery_publisher, *event_notifier, "ADAS_FG"));
    NX_CHECK(wait_for(session, [&]() { return session.dumps_written() == 1U; }));
    NX_CHECK(session.ring()->count() == 0U);
    NX_CHECK(session.last_dump_path().rfind(context.dir, 0) == 0);

    detail::TraceDumpHeader header{};
    std::vector<SpanFrame> frames;
    NX_CHECK(read_dump(session.last_dump_path(), header, frames));
    NX_CHECK(std::string_view{header.trigger_group} == "ADAS_FG");
    NX_CHECK(header.frame_count == 3U);
    NX_CHECK(frames.size() == 3U);

    // Coalescing: two notifications before one wake-up produce one snapshot,
    // naming the last group; a header-only snapshot is valid (ring is empty).
    NX_CHECK(publish_recovery(*recovery_publisher, *event_notifier, "G1"));
    NX_CHECK(publish_recovery(*recovery_publisher, *event_notifier, "G2"));
    NX_CHECK(wait_for(session, [&]() { return session.dumps_written() == 2U; }));
    NX_CHECK(session.triggers() == 2U);
    detail::TraceDumpHeader coalesced{};
    std::vector<SpanFrame> coalesced_frames;
    NX_CHECK(read_dump(session.last_dump_path(), coalesced, coalesced_frames));
    NX_CHECK(std::string_view{coalesced.trigger_group} == "G2");
    NX_CHECK(coalesced.frame_count == 0U);

    const std::uint32_t files_before_stop = count_dumps(context.dir);
    session.stop();
    NX_CHECK(!session.started());
    NX_CHECK(count_dumps(context.dir) == files_before_stop);
    session.stop(); // idempotent
}

void test_trigger_disabled() {
    E2eContext context = make_e2e_context("disabled", false);
    const TraceDaemonConfig& config = context.config;

    auto test_node = com::Node::create();
    NX_CHECK(test_node.has_value());
    if (!test_node.has_value()) {
        return;
    }
    detail::TraceDaemonSession session{config};
    NX_CHECK(session.start().has_value());

    const auto recovery_name = com::ServiceName::create(config.recovery_service_name.c_str());
    const auto event_name = com::ServiceName::create(config.recovery_event_name.c_str());
    NX_CHECK(recovery_name.has_value() && event_name.has_value());
    auto publisher = com::Publisher<sm::RecoveryNotification>::create(*test_node, *recovery_name);
    auto notifier = com::EventNotifier::create(*test_node, *event_name);
    NX_CHECK(publisher.has_value() && notifier.has_value());
    if (!publisher.has_value() || !notifier.has_value()) {
        session.stop();
        return;
    }
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    NX_CHECK(publish_recovery(*publisher, *notifier, "IGNORED"));
    for (int i = 0; i < 10; ++i) {
        NX_CHECK(session.wait_and_process().has_value());
    }
    NX_CHECK(session.dumps_written() == 0U);
    NX_CHECK(count_dumps(context.dir) == 0U);
    session.stop();
}

void test_mlock_best_effort() {
    // Lower the memlock limit and probe whether the kernel denies mlockall in
    // this environment. Root/CAP_IPC_LOCK may bypass the limit; the guarantee
    // under test is that a denial does not abort startup.
    rlimit limit{};
    limit.rlim_cur = 0U;
    limit.rlim_max = 0U;
    const bool lowered = ::setrlimit(RLIMIT_MEMLOCK, &limit) == 0;
    const bool denied = ::mlockall(MCL_CURRENT) != 0;
    if (!lowered || !denied) {
        return; // privileged environment: cannot exercise the denial path
    }

    E2eContext context = make_e2e_context("mlock", true);
    detail::TraceDaemonSession session{context.config};
    NX_CHECK(session.start().has_value());
    NX_CHECK(session.mlock_failed());
    session.stop();
}

} // namespace

int main() {
    run_suite("trace_config", test_config);
    run_suite("span_ring", test_ring);
    run_suite("trace_dump", test_dump);
    run_suite("naming_retention", test_naming_and_pruning);
    run_suite("session_e2e", test_session_e2e);
    run_suite("trigger_disabled", test_trigger_disabled);
    run_suite("mlock_best_effort", test_mlock_best_effort);
    std::printf("total: %d checks, %d failed\n", g_checks, g_failures);
    return g_failures == 0 ? 0 : 1;
}
