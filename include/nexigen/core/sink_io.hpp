// nexigen::core — shared cold-path file-descriptor plumbing for the
// observability file sinks (change dedup-sink-plumbing; spec
// core/sink-plumbing "Shared full-write loop" / "Shared file-open policy").
//
// Consumed by nexigen::metrics / nexigen::tracing / nexigen::chrono JSON and
// Chrome file sinks: each used to carry a private ::write loop with its own
// EINTR/partial-write handling (four copies, two of them returning bool and
// two returning core::Status — divergence the change collapses). This header
// is the ONE write/open/fsync path (the fixed_writer.hpp "one implementation
// instead of three near-identical copies" precedent; per-sink fd ownership,
// move semantics and line formats stay with the sinks).
//
// DETAIL DISCLAIMER (mirrors settings_io.hpp and fixed_writer.hpp): the
// namespace keeps a dedicated name (`nexigen::core::sink_io`) so test scopes
// that `using namespace nexigen::core` never learn a SECOND bare `detail`
// candidate name.
//
// Cold-path scope: every function here runs only on configuration, rotation
// or flush paths (never inside a record/batch/snapshot producer path). The
// one std::string path copy is construction-config / rotation-frequency,
// never ns-path work: ns-class latency budgets are untouched.
//
// Write contract: the loop composes any number of bytes across partial
// `write` calls in file order, retries interrupted (EINTR) writes without
// data loss or duplication, and reports failure as a first-failure
// core::Status (SystemError). No exceptions, no allocations after the open
// path, nothing reported as success while bytes are missing.
// Retry coverage: the EINTR/partial path is exercised on real descriptors
// by the core `sink_io` suite (interval-timer probe without SA_RESTART
// through a throttled pipe; stable across repeated fresh runs).
#pragma once

#include <cstddef>
#include <cerrno>
#include <string>
#include <string_view>

#include <fcntl.h>
#include <unistd.h>

#include <nexigen/core/result.hpp>

namespace nexigen::core::sink_io {

/// @brief Open an output file under the shared sink open policy.
///
/// Close-on-exec, create-if-missing, mode 0644, with the append policy selecting
/// appended writes (`O_APPEND`) or truncation (`O_TRUNC`).
///
/// @param path File path to open; copied once, so cold path only.
/// @param append `true` appends to existing content, `false` truncates it.
/// @return Open descriptor, or `-1` when the path cannot be opened (each caller
///         reports its own `SystemError`).
/// @note Spec `core/sink-plumbing`, requirement "Shared file-open policy".
[[nodiscard]] inline int open_output(std::string_view path, bool append) noexcept {
    const std::string path_buf{path};
    const int flags = O_WRONLY | O_CREAT | O_CLOEXEC | (append ? O_APPEND : O_TRUNC);
    return ::open(path_buf.c_str(), flags, 0644);
}

/// @brief Write all bytes across partial transfers, in file order.
///
/// Retries `EINTR` without losing or duplicating data and maps every other
/// failure to a first-failure status. Moved verbatim from the per-module sink
/// copies; cold path only.
///
/// @param fd Open descriptor to write to.
/// @param data Bytes to write; must remain valid for the call.
/// @param bytes Number of bytes at `data`.
/// @return `Status{}` once every byte was written; `SystemError` on the first
///         failed `write` call.
/// @note Spec `core/sink-plumbing`, requirement "Shared full-write loop".
[[nodiscard]] inline core::Status write_all(int fd, const char* data, std::size_t bytes) noexcept {
    std::size_t offset = 0;
    while (offset < bytes) {
        const ssize_t written = ::write(fd, data + offset, bytes - offset);
        if (written > 0) {
            offset += static_cast<std::size_t>(written);
        } else if (written < 0 && errno == EINTR) {
            continue;
        } else {
            return std::unexpected(core::ErrorCode::SystemError);
        }
    }
    return {};
}

/// @brief Request durable persistence for an open descriptor.
///
/// @param fd Open descriptor to flush with `fsync`.
/// @return `Status{}` on success; `SystemError` when `fsync` fails.
/// @note Spec `core/sink-plumbing`, requirement "Shared full-write loop": the
///       per-sink flush path requests durable persistence.
[[nodiscard]] inline core::Status fsync_fd(int fd) noexcept {
    if (::fsync(fd) != 0) {
        return std::unexpected(core::ErrorCode::SystemError);
    }
    return {};
}

} // namespace nexigen::core::sink_io
