// nexigen storage-format benchmark — MCAP adapter (no MCAP types leak here).
#pragma once

#include <cstdint>
#include <memory>
#include <string>

#include <nexigen/core/result.hpp>

#include "sink.hpp"

namespace nexigen::bench {

/// @brief MCAP writer configuration under test.
struct McapConfig {
    bool chunked{true};
    std::uint64_t chunk_size{256U * 1024U};
    bool no_message_index{false};
    bool no_summary{false};
    bool chunk_crc{true};
    bool data_crc{false};
};

/// @brief Writer-side statistics.
struct McapStats {
    std::uint64_t messages{0};
    std::uint64_t channels{0};
    std::uint64_t bytes{0};
    bool failed{false};
};

/// @brief Reader-side verification result.
struct McapVerify {
    std::uint64_t messages{0};
    std::uint64_t channels{0};
    std::uint64_t problems{0};
};

/// @brief Thin, exception-contained MCAP writer used by the benchmark.
class McapBenchWriter {
public:
    [[nodiscard]] static core::Result<std::unique_ptr<McapBenchWriter>>
    create(Sink& sink, const McapConfig& config) noexcept;

    ~McapBenchWriter();

    McapBenchWriter(const McapBenchWriter&) = delete;
    McapBenchWriter& operator=(const McapBenchWriter&) = delete;

    [[nodiscard]] bool write(const std::byte* payload, std::size_t size, std::uint32_t sequence,
                             std::uint64_t log_time_ns, std::uint64_t publish_time_ns) noexcept;

    [[nodiscard]] bool close() noexcept;

    [[nodiscard]] McapStats stats() const noexcept;

private:
    struct Impl;
    explicit McapBenchWriter(std::unique_ptr<Impl> impl) noexcept;

    std::unique_ptr<Impl> impl_;
};

/// @brief Reopen a completed MCAP file and count messages/channels.
[[nodiscard]] core::Result<McapVerify> verify_mcap(const std::string& path) noexcept;

/// @brief Read a possibly truncated MCAP file, counting the surviving prefix.
[[nodiscard]] core::Result<McapVerify> recover_mcap(const std::string& path) noexcept;

} // namespace nexigen::bench
