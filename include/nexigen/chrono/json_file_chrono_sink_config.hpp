// nexigen::chrono — JSON-lines file sink (change add-chrono-clock-service;
// spec chrono/chrono-sink "JSON-file chrono sink").
//
// Writes one JSON object per snapshot per line, rendered with std::to_chars
// into a fixed scratch buffer and written with a raw ::write. The file is
// opened during construction (the sink owns the descriptor); the write path is
// allocation-free and flush() pushes the data to the file with fsync.
#pragma once

#include <charconv>
#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <unistd.h>

#include <cerrno>
#include <fcntl.h>

#include <nexigen/chrono/chrono_snapshot.hpp>
#include <nexigen/chrono/concepts/chrono_sink_concept.hpp>
#include <nexigen/core/result.hpp>
#include <nexigen/core/sink_io.hpp>

namespace nexigen::chrono {

/// File sink configuration: the path and whether snapshots are appended.

// Extracted struct; the class and formatters remain in json_file_chrono_sink.hpp.
struct JsonFileChronoSinkConfig {
    std::string path{};
    bool append{true};

    /// Cold-path diagnostic (mandate 9).
    [[nodiscard]] std::string to_string() const {
        return "JsonFileChronoSinkConfig(path=" + path + ", append=" + (append ? "true" : "false") +
               ")";
    }
};

/// Free helper (mandate 9): mirrors the member.
[[nodiscard]] inline std::string to_string(const JsonFileChronoSinkConfig& config) {
    return config.to_string();
}

/// A sink that appends one JSON object per snapshot to a file.

} // namespace nexigen::chrono
