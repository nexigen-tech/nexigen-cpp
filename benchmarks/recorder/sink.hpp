// nexigen storage-format benchmark — sinks used by every candidate.
#pragma once

#include <cstddef>
#include <cstdint>
#include <string>

namespace nexigen::bench {

/// @brief Minimal byte sink shared by MCAP and the custom prototype.
class Sink {
public:
    Sink() = default;
    Sink(const Sink&) = delete;
    Sink& operator=(const Sink&) = delete;
    virtual ~Sink() = default;

    /// @brief Append bytes. Must not throw; errors are exposed via failed().
    virtual void write(const std::byte* data, std::size_t size) noexcept = 0;

    /// @brief Flush any sink-owned buffers (and optionally fsync).
    virtual void flush() noexcept {}

    [[nodiscard]] virtual std::uint64_t bytes_written() const noexcept = 0;
    [[nodiscard]] virtual std::uint64_t file_size() const noexcept { return bytes_written(); }
    [[nodiscard]] virtual bool failed() const noexcept = 0;
};

/// @brief Discards bytes and counts them: isolates writer CPU from storage.
class CountingSink final : public Sink {
public:
    void write(const std::byte* data, std::size_t size) noexcept override;
    [[nodiscard]] std::uint64_t bytes_written() const noexcept override {
        return bytes_;
    }
    [[nodiscard]] bool failed() const noexcept override { return false; }

private:
    std::uint64_t bytes_{0};
};

/// @brief Writes bytes to a file with a POSIX descriptor.
class FileSink final : public Sink {
public:
    /// @brief Open `path` with O_CREAT|O_TRUNC|O_WRONLY.
    [[nodiscard]] bool open(const std::string& path) noexcept;

    void write(const std::byte* data, std::size_t size) noexcept override;
    void flush() noexcept override;

    [[nodiscard]] std::uint64_t bytes_written() const noexcept override { return bytes_; }
    [[nodiscard]] std::uint64_t file_size() const noexcept override;
    [[nodiscard]] bool failed() const noexcept override { return failed_; }

    void close() noexcept;
    ~FileSink() override { close(); }

private:
    int fd_{-1};
    std::uint64_t bytes_{0};
    bool failed_{false};
};

} // namespace nexigen::bench
