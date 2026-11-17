// nexigen storage-format benchmark — sinks used by every candidate.
#include "sink.hpp"

#include <cerrno>
#include <cstring>

#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>

namespace nexigen::bench {

void CountingSink::write(const std::byte* /*data*/, std::size_t size) noexcept {
    bytes_ += size;
}

bool FileSink::open(const std::string& path) noexcept {
    close();
    fd_ = ::open(path.c_str(), O_CREAT | O_TRUNC | O_WRONLY, 0644);
    if (fd_ < 0) {
        failed_ = true;
        return false;
    }
    bytes_ = 0;
    failed_ = false;
    return true;
}

void FileSink::write(const std::byte* data, std::size_t size) noexcept {
    if (failed_ || fd_ < 0) {
        failed_ = true;
        return;
    }
    std::size_t offset = 0;
    while (offset < size) {
        const ssize_t written = ::write(fd_, data + offset, size - offset);
        if (written < 0) {
            if (errno == EINTR) {
                continue;
            }
            failed_ = true;
            return;
        }
        offset += static_cast<std::size_t>(written);
    }
    bytes_ += size;
}

void FileSink::flush() noexcept {
    if (fd_ >= 0) {
        static_cast<void>(::fdatasync(fd_));
    }
}

std::uint64_t FileSink::file_size() const noexcept {
    if (fd_ < 0) {
        return bytes_;
    }
    struct stat info {};
    if (::fstat(fd_, &info) != 0) {
        return bytes_;
    }
    return static_cast<std::uint64_t>(info.st_size);
}

void FileSink::close() noexcept {
    if (fd_ >= 0) {
        static_cast<void>(::close(fd_));
        fd_ = -1;
    }
}

} // namespace nexigen::bench
