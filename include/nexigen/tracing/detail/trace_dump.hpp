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

inline constexpr std::size_t trace_dump_header_size = 96U;
inline constexpr std::uint16_t trace_dump_format_version = 1U;
inline constexpr std::array<char, 8> trace_dump_magic{'N', 'X', 'T', 'R', 'A', '0', '0', '1'};
inline constexpr std::size_t trace_dump_trigger_group_size = 32U;

/// Fixed header of one `.nxtr` snapshot (little-endian host layout).
struct alignas(8) TraceDumpHeader {
    char magic[8]{};                                     ///< "NXTRA001"
    std::uint16_t version{trace_dump_format_version};    ///< format version
    std::uint16_t header_size{trace_dump_header_size};   ///< 96
    std::uint32_t flags{0U};                             ///< bit0 = recovery-triggered
    std::uint64_t frame_count{0U};                       ///< retained frames
    std::uint64_t first_start_ns{0U};                    ///< oldest retained start
    std::uint64_t last_end_ns{0U};                       ///< newest retained end
    std::uint64_t ring_overwrites{0U};                   ///< frames evicted by the ring
    char trigger_group[trace_dump_trigger_group_size]{}; ///< PHM function group
    std::uint64_t trigger_ts_ns{0U};                     ///< monotonic trigger time
    std::uint64_t reserved{0U};
};

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

/// Create an absolute directory tree (`mkdir -p`), tolerating existing entries.
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

/// `incident_trace_<YYYYMMDDTHHMMSSZ>.nxtr`, with `-<collision>` before the
/// extension when a second snapshot lands in the same second. UTC names sort
/// chronologically.
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

/// Write one snapshot atomically (temp + fsync + rename). The ring is not
/// modified; the caller clears it only after success.
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

/// Delete the oldest `incident_trace_*.nxtr` files until at most `max_files`
/// remain. Returns the number of removed files; directory errors are ignored
/// (the caller reports its own dump status).
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
