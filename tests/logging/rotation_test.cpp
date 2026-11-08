// Split from logging_test.cpp: nexigen::logging rotation schedules and size-based rotation.

#include "logging_test_support.hpp"

namespace {

void test_rotation() {
    constexpr std::int64_t hour_end_ns =
        1'784'851'199'999'999'999LL;                       // 2026-07-23T23:59:59.999999999Z
    constexpr std::int64_t next_hour_ns = hour_end_ns + 1; // 2026-07-24T00:00:00Z
    constexpr std::int64_t ns_hour = 3'600'000'000'000LL;

    // Keys move exactly at boundaries.
    NX_CHECK(detail::period_key(RotationSchedule::Hourly, 0, hour_end_ns) ==
             detail::period_key(RotationSchedule::Hourly, 0, hour_end_ns));
    NX_CHECK(detail::period_key(RotationSchedule::Hourly, 0, next_hour_ns) ==
             detail::period_key(RotationSchedule::Hourly, 0, hour_end_ns) + 1);
    // next_hour_ns lands on 07-24T00:00:00Z — already the next DAY as well.
    NX_CHECK(detail::period_key(RotationSchedule::Daily, 0, next_hour_ns) ==
             detail::period_key(RotationSchedule::Daily, 0, hour_end_ns) + 1);
    NX_CHECK(detail::period_key(RotationSchedule::Daily, 0, hour_end_ns + ns_hour) ==
             detail::period_key(RotationSchedule::Daily, 0, hour_end_ns) + 1);

    char label[64]{};
    NX_CHECK(detail::period_label(RotationSchedule::Hourly, 0, hour_end_ns, label) == 13);
    NX_CHECK(std::strcmp(label, "2026-07-23T23") == 0);
    NX_CHECK(detail::period_label(RotationSchedule::Daily, 0, hour_end_ns, label) == 10);
    NX_CHECK(std::strcmp(label, "2026-07-23") == 0);
    // Leap day label
    NX_CHECK(detail::period_label(RotationSchedule::Daily, 0, 1'709'164'800'000'000'000LL, label) ==
             10);
    NX_CHECK(std::strcmp(label, "2024-02-29") == 0);
    // Custom key/label: 250 ms windows (epoch-anchored, so align the probe
    // to the window start to exercise the boundary itself).
    constexpr std::uint64_t custom = 250'000'000ULL;
    constexpr std::int64_t t = 1'784'850'120'321'841'133LL;
    const std::int64_t idx0 = static_cast<std::int64_t>(static_cast<std::uint64_t>(t) / custom);
    const std::int64_t t0a = t - static_cast<std::int64_t>(static_cast<std::uint64_t>(t) % custom);
    NX_CHECK(detail::period_key(RotationSchedule::Custom, custom, t) == idx0);
    NX_CHECK(detail::period_key(RotationSchedule::Custom, custom, t0a) == idx0);
    NX_CHECK(detail::period_key(RotationSchedule::Custom, custom,
                                t0a + static_cast<std::int64_t>(custom) - 1) == idx0);
    NX_CHECK(detail::period_key(RotationSchedule::Custom, custom,
                                t0a + static_cast<std::int64_t>(custom)) == idx0 + 1);
    const std::size_t llen = detail::period_label(RotationSchedule::Custom, custom, t, label);
    char expect_label[64];
    const int epos = std::snprintf(expect_label, sizeof expect_label, "epoch-%lld",
                                   static_cast<long long>(idx0));
    NX_CHECK(epos > 0);
    NX_CHECK(std::strcmp(label, expect_label) == 0 && std::strlen(label) == llen);
    // None: key 0, no label.
    NX_CHECK(detail::period_key(RotationSchedule::None, 0, t) == 0);
    NX_CHECK(detail::period_label(RotationSchedule::None, 0, t, label) == 0);

    // Filesystem switch: two 1 ms custom windows land in distinct directories,
    // sentinel lines round-trip, and the whole sequence allocates nothing.
    alignas(64) static char base[512];
    std::snprintf(base, sizeof base, "/tmp/opencode/nexigen-rot-%d-%lld", ::getpid(),
                  static_cast<long long>(idx0 % 1'000'000));
    const std::string_view base_dir(base);
    constexpr std::uint64_t fast_period = 1'000'000ULL; // 1 ms

    detail::RotationScratch sc{};
    int fd = -1;
    // warm-up open first so any lazy libc setup happens outside the measured window
    NX_CHECK(detail::open_period(RotationSchedule::Custom, fast_period, t, base_dir, "warm.jsonl",
                                 sc, fd)
                 .has_value());
    NX_CHECK(fd >= 0);
    ::close(fd);

    const std::size_t arena_b = g_alloc_used;
    const std::size_t heap_b = heap_in_use();

    int fd1 = -1;
    NX_CHECK(detail::open_period(RotationSchedule::Custom, fast_period, t, base_dir, "app.jsonl",
                                 sc, fd1)
                 .has_value());
    NX_CHECK(fd1 >= 0);
    char path1[560];
    std::strcpy(path1, sc.file); // remember for read-back
    static_assert(sizeof path1 == sizeof sc.file);
    NX_CHECK(::write(fd1, "a\n", 2) == 2);
    ::close(fd1);

    int fd2 = -1;
    NX_CHECK(detail::open_period(RotationSchedule::Custom, fast_period, t + 2'000'000, base_dir,
                                 "app.jsonl", sc, fd2)
                 .has_value());
    NX_CHECK(fd2 >= 0);
    char path2[560];
    std::strcpy(path2, sc.file);
    NX_CHECK(std::strcmp(path1, path2) != 0); // different windows => different files
    NX_CHECK(::write(fd2, "b\n", 2) == 2);
    ::close(fd2);

    NX_CHECK(g_alloc_used == arena_b);
    NX_CHECK(heap_in_use() == heap_b); // mkdirat/openat/write are allocation-free

    // Read both files back: contents intact, both periods preserved.
    auto read_first_line = [](const char* path, char (&out)[8]) -> std::size_t {
        const int fdr = ::open(path, O_RDONLY | O_CLOEXEC);
        if (fdr < 0) {
            return 0;
        }
        const ssize_t n = ::read(fdr, out, 2);
        ::close(fdr);
        if (n <= 0) {
            return 0;
        }
        out[n] = '\0';
        return static_cast<std::size_t>(n);
    };
    char got[8]{};
    NX_CHECK(read_first_line(path1, got) == 2 && std::strcmp(got, "a\n") == 0);
    NX_CHECK(read_first_line(path2, got) == 2 && std::strcmp(got, "b\n") == 0);

    // Error paths: empty base rejected; a non-directory component maps to SystemError.
    NX_CHECK(
        !detail::open_period(RotationSchedule::Hourly, 0, t, "", "app.jsonl", sc, fd).has_value());
    const Status bad =
        detail::open_period(RotationSchedule::Hourly, 0, t, "/etc/hostname", "app.jsonl", sc, fd);
    NX_CHECK(!bad.has_value());
    NX_CHECK(bad.error() == ErrorCode::SystemError);
    std::printf("rotation: windows+labels verified, fs switch alloc-free (%s)\n", base);
}

// --- FileLogger suites (tasks 6.1-6.4 / spec logging/file-logger).

// Count newlines in a file; SIZE_MAX if the file cannot be opened.

// --- size-based rotation (spec logging/file-logger "Size-based file rotation").
void test_file_size_rotation() {
    const std::string base = unique_path("settings_size_split");
    static_cast<void>(::mkdir(base.c_str(), 0755));
    {
        static FileLogger<64, 128, 1> logger{};
        FileLoggerConfig cfg{};
        cfg.app_name = "size";
        cfg.base_dir = base;
        cfg.file_name = "sz.jsonl";
        cfg.rotation.kind = RotationSchedule::None;
        cfg.max_file_size_bytes = 300ULL;
        NX_CHECK(logger.initialize(cfg).has_value());
        for (int i = 0; i < 10; ++i) {
            logger.info("record {} padding padding padding padding padding padding padding",
                        static_cast<std::uint64_t>(i));
        }
        NX_CHECK(logger.flush().has_value());
        const std::vector<std::string> files = list_regular_files(base);
        NX_CHECK(files.size() > 1U);
        NX_CHECK(has_basename(files, "sz.jsonl"));
        NX_CHECK(has_basename(files, "sz.1.jsonl"));
        NX_CHECK(total_lines(files) == 10U);
    }

    // Zero disables segmentation: one file, all records.
    {
        const std::string zero_dir = unique_path("settings_size_zero");
        static_cast<void>(::mkdir(zero_dir.c_str(), 0755));
        static FileLogger<64, 128, 1> logger{};
        FileLoggerConfig cfg{};
        cfg.app_name = "size";
        cfg.base_dir = zero_dir;
        cfg.file_name = "sz.jsonl";
        cfg.rotation.kind = RotationSchedule::None;
        cfg.max_file_size_bytes = 0ULL;
        NX_CHECK(logger.initialize(cfg).has_value());
        for (int i = 0; i < 10; ++i) {
            logger.info("record {}", static_cast<std::uint64_t>(i));
        }
        NX_CHECK(logger.flush().has_value());
        const std::vector<std::string> files = list_regular_files(zero_dir);
        NX_CHECK(files.size() == 1U);
        NX_CHECK(has_basename(files, "sz.jsonl"));
        NX_CHECK(!has_basename(files, "sz.1.jsonl"));
        NX_CHECK(total_lines(files) == 10U);
    }

    // A single line larger than the limit still lands exactly once.
    {
        const std::string tiny_dir = unique_path("settings_size_tiny");
        static_cast<void>(::mkdir(tiny_dir.c_str(), 0755));
        static FileLogger<64, 128, 1> logger{};
        FileLoggerConfig cfg{};
        cfg.app_name = "size";
        cfg.base_dir = tiny_dir;
        cfg.file_name = "sz.jsonl";
        cfg.rotation.kind = RotationSchedule::None;
        cfg.max_file_size_bytes = 1ULL;
        NX_CHECK(logger.initialize(cfg).has_value());
        logger.info("one line larger than the limit");
        NX_CHECK(logger.flush().has_value());
        const std::vector<std::string> files = list_regular_files(tiny_dir);
        NX_CHECK(files.size() == 1U);
        NX_CHECK(total_lines(files) == 1U);
    }

    // Pre-existing content counts toward the limit and triggers a switch.
    {
        const std::string old_dir = unique_path("settings_size_existing");
        static_cast<void>(::mkdir(old_dir.c_str(), 0755));
        write_text(old_dir + "/sz.jsonl", std::string(1000, 'x'));
        static FileLogger<64, 128, 1> logger{};
        FileLoggerConfig cfg{};
        cfg.app_name = "size";
        cfg.base_dir = old_dir;
        cfg.file_name = "sz.jsonl";
        cfg.rotation.kind = RotationSchedule::None;
        cfg.max_file_size_bytes = 300ULL;
        NX_CHECK(logger.initialize(cfg).has_value());
        logger.info("next segment please");
        NX_CHECK(logger.flush().has_value());
        const std::vector<std::string> files = list_regular_files(old_dir);
        NX_CHECK(has_basename(files, "sz.1.jsonl"));
        NX_CHECK(line_count(old_dir + "/sz.1.jsonl") == 1U);
    }

    // Segment path builder: naming rules (unit-level).
    {
        detail::RotationScratch scratch{};
        int fd = -1;
        const std::string dir = unique_path("settings_segment_paths");
        static_cast<void>(::mkdir(dir.c_str(), 0755));
        NX_CHECK(
            detail::open_segment(RotationSchedule::None, 0, 0, dir, "app.jsonl", 2U, scratch, fd)
                .has_value());
        NX_CHECK(fd >= 0);
        if (fd >= 0) {
            static_cast<void>(::close(fd));
        }
        NX_CHECK(std::string(scratch.file).find("app.2.jsonl") != std::string::npos);
        NX_CHECK(detail::open_segment(RotationSchedule::None, 0, 0, dir, "noext", 3U, scratch, fd)
                     .has_value());
        if (fd >= 0) {
            static_cast<void>(::close(fd));
        }
        NX_CHECK(std::string(scratch.file).find("noext.3") != std::string::npos);
        NX_CHECK(
            !detail::open_segment(RotationSchedule::None, 0, 0, "", "app.jsonl", 1U, scratch, fd)
                 .has_value());
    }

    std::printf("file_size_rotation: split, naming, zero, oversized, existing verified\n");
}

void test_spam_window_and_rotation_errors() {
    {
        SpamProtectionConfig cfg{};
        cfg.enable_repeat_suppression = true;
        cfg.repeat_suppression_window_ms = 1;
        detail::SpamProtection sp{cfg};
        detail::SpamShardState st{};
        const std::uint64_t hash = detail::message_hash("f.cpp", 1, "msg", nullptr, 0);
        NX_CHECK(sp.check_repeat(st, hash, 0));
        NX_CHECK(!sp.check_repeat(st, hash, 500'000));  // inside the window
        NX_CHECK(sp.check_repeat(st, hash, 2'000'000)); // window expired → pass
        NX_CHECK(st.repeat_count == 1);
    }
    {
        detail::RotationScratch scratch{};
        int fd = -1;
        NX_CHECK(
            !detail::open_period(RotationSchedule::None, 0, 0, "", "f", scratch, fd).has_value());
        NX_CHECK(!detail::open_period(RotationSchedule::None, 0, 0, "/tmp", std::string(256, 'x'),
                                      scratch, fd)
                      .has_value());
        NX_CHECK(!detail::open_period(RotationSchedule::None, 0, 0, std::string(512, 'b'), "f",
                                      scratch, fd)
                      .has_value());
        NX_CHECK(!detail::open_period(RotationSchedule::None, 0, 0, "/proc/nexigen-nope", "f",
                                      scratch, fd)
                      .has_value());
    }
}

} // namespace

void suite_rotation() {
    run_suite("rotation", test_rotation);
}

void suite_file_size_rotation() {
    run_suite("file_size_rotation", test_file_size_rotation);
}

void suite_spam_window_rotation_errors() {
    run_suite("spam_window_rotation_errors", test_spam_window_and_rotation_errors);
}
