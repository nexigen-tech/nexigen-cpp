// nexigen::tracing — flight-recorder snapshot format and cold-path I/O
// (change add-trace-daemon; spec tracing/trace-daemon "A snapshot is a
// versioned binary record" / "Snapshot retention is bounded"; design D4–D6).
//
// A snapshot is a 96-byte header followed by the retained SpanFrame records in
// chronological order. The writer goes through a `.tmp` file, fsyncs, and
// renames, so a partial file never appears under the final name. Naming and
// pruning are cold-path helpers used by the daemon session and its tests.
#pragma once

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string>
#include <string_view>
#include <vector>

#include <cerrno>
#include <dirent.h>
#include <sys/stat.h>
#include <unistd.h>

#include <nexigen/chrono/civil_time.hpp>
#include <nexigen/core/result.hpp>
#include <nexigen/core/sink_io.hpp>
#include <nexigen/tracing/detail/span_ring.hpp>
#include <nexigen/tracing/span_frame.hpp>

namespace nexigen::tracing::detail {

/// @brief Size in bytes of the fixed `.nxtr` snapshot header.
///
/// Pinned by the `sizeof(TraceDumpHeader)` static assertion; readers skip
/// exactly this many bytes before the frame area.
///
/// @note Spec `tracing/trace-daemon`, requirement "A snapshot is a versioned
///       binary record"; design D4.
inline constexpr std::size_t trace_dump_header_size = 96U;

/// @brief Current snapshot format version written into the header.
///
/// A reader comparing a different version may reject the file without
/// interpreting the frame area.
///
/// @note Spec `tracing/trace-daemon`, requirement "A snapshot is a versioned
///       binary record"; design D4.
inline constexpr std::uint16_t trace_dump_format_version = 1U;

/// @brief Magic marker at the start of every snapshot: `"NXTRA001"`.
///
/// Identifies the file as a Nexigen trace dump; a different marker lets a
/// reader reject the file before parsing anything else.
///
/// @note Spec `tracing/trace-daemon`, requirement "A snapshot is a versioned
///       binary record"; design D4.
inline constexpr std::array<char, 8> trace_dump_magic{'N', 'X', 'T', 'R', 'A', '0', '0', '1'};

/// @brief Capacity of the header's inline trigger-group field, in bytes.
///
/// The writer stores at most `trace_dump_trigger_group_size - 1` characters
/// plus a NUL terminator.
///
/// @note Spec `tracing/trace-daemon`, requirement "A snapshot is a versioned
///       binary record"; design D4.
inline constexpr std::size_t trace_dump_trigger_group_size = 32U;

/// @brief Fixed header of one `.nxtr` snapshot (little-endian host layout).
///
/// 96 bytes, 8-byte aligned, pointer-free. It is followed immediately by
/// `frame_count` `SpanFrame` records in chronological (oldest-first) order, so
/// the whole file is a flat dump of the retained window. Every field has a
/// fixed width pinned by `static_assert` and `offsetof` checks below. Built and
/// written on the daemon's cold dump path only.
///
/// @note Spec `tracing/trace-daemon`, requirement "A snapshot is a versioned
///       binary record"; design D4–D6.
struct alignas(8) TraceDumpHeader {
    /// Magic marker; equals `trace_dump_magic` (`"NXTRA001"`).
    char magic[8]{};
    /// On-disk format version; equals `trace_dump_format_version` for this
    /// writer.
    std::uint16_t version{trace_dump_format_version};
    /// Header size in bytes; always `trace_dump_header_size` (96).
    std::uint16_t header_size{trace_dump_header_size};
    /// Bit 0 set means the snapshot was recovery-triggered; other bits are
    /// written as zero.
    std::uint32_t flags{0U};
    /// Number of `SpanFrame` records following the header.
    std::uint64_t frame_count{0U};
    /// Monotonic start timestamp of the oldest retained frame (0 when empty).
    std::uint64_t first_start_ns{0U};
    /// Monotonic end timestamp of the newest retained frame (0 when empty).
    std::uint64_t last_end_ns{0U};
    /// Cumulative frames the ring evicted before this dump.
    std::uint64_t ring_overwrites{0U};
    /// PHM function group of the trigger, NUL-terminated within 32 bytes.
    char trigger_group[trace_dump_trigger_group_size]{};
    /// Monotonic time the recovery trigger was observed.
    std::uint64_t trigger_ts_ns{0U};
    /// Zeroed padding reserved for future header fields.
    std::uint64_t reserved{0U};
};

// Layout contract (design D4): exact size, alignment, and field offsets are
// part of the on-disk format, not an implementation detail.
static_assert(sizeof(TraceDumpHeader) == trace_dump_header_size);
static_assert(alignof(TraceDumpHeader) == 8);
static_assert(offsetof(TraceDumpHeader, magic) == 0);
static_assert(offsetof(TraceDumpHeader, version) == 8);
static_assert(offsetof(TraceDumpHeader, header_size) == 10);
static_assert(offsetof(TraceDumpHeader, flags) == 12);
static_assert(offsetof(TraceDumpHeader, frame_count) == 16);
static_assert(offsetof(TraceDumpHeader, first_start_ns) == 24);
static_assert(offsetof(TraceDumpHeader, last_end_ns) == 32);
static_assert(offsetof(TraceDumpHeader, ring_overwrites) == 40);
static_assert(offsetof(TraceDumpHeader, trigger_group) == 48);
static_assert(offsetof(TraceDumpHeader, trigger_ts_ns) == 80);
static_assert(offsetof(TraceDumpHeader, reserved) == 88);

/// @brief Create an absolute directory tree (`mkdir -p`), tolerating existing
/// entries.
///
/// Cold path only. Each path component is created with mode `0755`; an
/// existing entry is accepted, any other failure aborts the walk.
///
/// @param path Absolute directory path to create; must start with `/`.
/// @return Success, `core::ErrorCode::InvalidArgument` when `path` is empty or
///         relative, or `core::ErrorCode::SystemError` when a `mkdir` fails
///         for a reason other than `EEXIST`.
/// @note Spec `tracing/trace-daemon`, requirement "Snapshot retention is
///       bounded". Called once at daemon startup.
[[nodiscard]] inline core::Status create_directories(std::string_view path) noexcept {
    if (path.empty() || path.front() != '/') {
        return std::unexpected(core::ErrorCode::InvalidArgument);
    }
    std::string current{"/"};
    current.reserve(path.size());
    for (std::size_t i = 1; i < path.size(); ++i) {
        current.push_back(path[i]);
        if (path[i] != '/') {
            continue;
        }
        std::string directory = current;
        while (!directory.empty() && directory.back() == '/') {
            directory.pop_back();
        }
        if (!directory.empty() && ::mkdir(directory.c_str(), 0755) != 0 && errno != EEXIST) {
            return std::unexpected(core::ErrorCode::SystemError);
        }
    }
    std::string final_directory{path};
    while (final_directory.size() > 1U && final_directory.back() == '/') {
        final_directory.pop_back();
    }
    if (::mkdir(final_directory.c_str(), 0755) != 0 && errno != EEXIST) {
        return std::unexpected(core::ErrorCode::SystemError);
    }
    return {};
}

/// @brief Build a snapshot file name from a UTC timestamp.
///
/// Produces `incident_trace_<YYYYMMDDTHHMMSSZ>.nxtr`, inserting
/// `-<collision>` before the extension when a second snapshot lands in the same
/// second. UTC names sort chronologically, which `prune_dumps` relies on.
///
/// @param utc_ns Trigger time as nanoseconds since the Unix epoch, UTC.
/// @param collision Collision index; `0` omits the suffix, non-zero appends
///        `-<collision>`.
/// @return The bare file name (no directory component).
/// @note Spec `tracing/trace-daemon`, requirement "Snapshot retention is
///       bounded". Cold path; allocates the result string.
[[nodiscard]] inline std::string snapshot_file_name(std::int64_t utc_ns,
                                                    std::uint32_t collision = 0U) {
    const chrono::CivilTime civil = chrono::civil_from_utc_ns(utc_ns);
    char stamp[48]{};
    std::snprintf(stamp, sizeof(stamp), "%04lld%02u%02uT%02u%02u%02uZ",
                  static_cast<long long>(civil.year), static_cast<unsigned>(civil.month),
                  static_cast<unsigned>(civil.day), static_cast<unsigned>(civil.hour),
                  static_cast<unsigned>(civil.minute), static_cast<unsigned>(civil.second));
    std::string name = "incident_trace_";
    name += stamp;
    if (collision != 0U) {
        name += '-';
        name += std::to_string(collision);
    }
    name += ".nxtr";
    return name;
}

/// @brief Write one snapshot atomically: temp file, fill, fsync, rename.
///
/// Serializes the 96-byte header (magic, version, flags, frame count, first
/// start and last end timestamps, ring overwrite count, trigger group, trigger
/// timestamp) followed by the ring's retained `SpanFrame` records oldest-first,
/// copied in fixed batches of 64 so the frame loop performs no per-frame
/// allocation. The frames are written to `<final_path>.tmp`, `fsync`ed, and only
/// then renamed into place, so a partial file never appears under the final
/// name. The ring itself is not modified; the caller clears it only after
/// success.
///
/// @param final_path Destination path for the snapshot (absolute, in the
///        configured dump directory).
/// @param ring Source ring; read-only, consulted through `count()`, `at()`,
///        and `overwritten()`.
/// @param trigger_group PHM function group recorded in the header; truncated
///        to at most 31 characters plus a NUL.
/// @param trigger_ts_ns Monotonic trigger time recorded in the header.
/// @param recovery_triggered Sets header bit 0 when `true` (recovery-triggered
///        dump), leaves it clear otherwise.
/// @return Success, or `core::ErrorCode::SystemError` when the temp file
///         cannot be opened, a write fails, `fsync` fails, `close` fails, or
///         the final rename fails. On every failure the temp file is unlinked
///         and no partial snapshot remains.
/// @note Spec `tracing/trace-daemon`, requirements "A snapshot is a versioned
///       binary record" and "Snapshot retention is bounded"; design D4–D6.
///       Cold path only — steady-state draining performs no disk I/O.
[[nodiscard]] inline core::Status
write_trace_dump(std::string_view final_path, const SpanRing& ring, std::string_view trigger_group,
                 std::uint64_t trigger_ts_ns, bool recovery_triggered = true) noexcept {
    TraceDumpHeader header{};
    std::memcpy(header.magic, trace_dump_magic.data(), trace_dump_magic.size());
    header.version = trace_dump_format_version;
    header.header_size = static_cast<std::uint16_t>(trace_dump_header_size);
    header.flags = recovery_triggered ? 1U : 0U;
    header.frame_count = ring.count();
    if (!ring.empty()) {
        header.first_start_ns = ring.at(0U).start_ns;
        header.last_end_ns = ring.at(ring.count() - 1U).end_ns;
    }
    header.ring_overwrites = ring.overwritten();
    const std::size_t group_size = trigger_group.size() < trace_dump_trigger_group_size - 1U
                                       ? trigger_group.size()
                                       : trace_dump_trigger_group_size - 1U;
    std::memcpy(header.trigger_group, trigger_group.data(), group_size);
    header.trigger_group[group_size] = '\0';
    header.trigger_ts_ns = trigger_ts_ns;

    std::string tmp_path{final_path};
    tmp_path += ".tmp";
    const int fd = core::sink_io::open_output(tmp_path, false);
    if (fd < 0) {
        return std::unexpected(core::ErrorCode::SystemError);
    }

    auto fail = [&]() -> core::Status {
        static_cast<void>(::close(fd));
        static_cast<void>(::unlink(tmp_path.c_str()));
        return std::unexpected(core::ErrorCode::SystemError);
    };

    core::Status status =
        core::sink_io::write_all(fd, reinterpret_cast<const char*>(&header), sizeof(header));
    if (!status) {
        return fail();
    }

    std::array<SpanFrame, 64U> batch{};
    std::size_t index = 0U;
    while (index < ring.count()) {
        const std::size_t take = std::min(batch.size(), ring.count() - index);
        for (std::size_t i = 0; i < take; ++i) {
            batch[i] = ring.at(index + i);
        }
        status = core::sink_io::write_all(fd, reinterpret_cast<const char*>(batch.data()),
                                          take * sizeof(SpanFrame));
        if (!status) {
            return fail();
        }
        index += take;
    }

    if (!core::sink_io::fsync_fd(fd)) {
        return fail();
    }
    if (::close(fd) != 0) {
        static_cast<void>(::unlink(tmp_path.c_str()));
        return std::unexpected(core::ErrorCode::SystemError);
    }
    std::string final_path_buf{final_path};
    if (::rename(tmp_path.c_str(), final_path_buf.c_str()) != 0) {
        static_cast<void>(::unlink(tmp_path.c_str()));
        return std::unexpected(core::ErrorCode::SystemError);
    }
    return {};
}

/// @brief Delete the oldest snapshots until at most `max_files` remain.
///
/// Scans `directory` for names matching `incident_trace_*.nxtr`, sorts them
/// lexicographically (UTC names sort chronologically), and unlinks the excess
/// from the front. Directory errors are ignored: the caller reports its own
/// dump status, and pruning never fails the trigger.
///
/// @param directory Dump directory to prune.
/// @param max_files Maximum number of snapshots to retain.
/// @return Number of files actually removed (`0` when the directory cannot be
///         opened or the count is already within the limit).
/// @note Spec `tracing/trace-daemon`, requirement "Snapshot retention is
///       bounded". Cold path, run after a successful dump only.
[[nodiscard]] inline std::uint32_t prune_dumps(std::string_view directory,
                                               std::uint32_t max_files) noexcept {
    DIR* dir = ::opendir(std::string{directory}.c_str());
    if (dir == nullptr) {
        return 0U;
    }
    std::vector<std::string> names;
    while (const dirent* entry = ::readdir(dir)) {
        const std::string_view name{entry->d_name};
        constexpr std::string_view prefix = "incident_trace_";
        constexpr std::string_view suffix = ".nxtr";
        if (name.size() <= prefix.size() + suffix.size() ||
            name.substr(0, prefix.size()) != prefix ||
            name.substr(name.size() - suffix.size()) != suffix) {
            continue;
        }
        names.emplace_back(name);
    }
    static_cast<void>(::closedir(dir));
    if (names.size() <= static_cast<std::size_t>(max_files)) {
        return 0U;
    }
    std::sort(names.begin(), names.end());
    std::uint32_t removed = 0U;
    const std::size_t excess = names.size() - static_cast<std::size_t>(max_files);
    for (std::size_t i = 0; i < excess; ++i) {
        const std::string full = std::string{directory} + "/" + names[i];
        if (::unlink(full.c_str()) == 0) {
            ++removed;
        }
    }
    return removed;
}

} // namespace nexigen::tracing::detail
