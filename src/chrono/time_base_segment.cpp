// nexigen::chrono — mapped time-base segment lifecycle implementation (change
// add-time-base-service; spec chrono/time-base "Shared-memory publication
// contract"; design D-B, D-C).
//
// Cold path only: every operation here runs once at daemon start or client
// attach, never in a publish loop or a producer path. The helper deliberately
// keeps POSIX calls in this compiled unit so consumer translation units that
// only read the block never include <sys/mman.h> or <sys/file.h>.
#include <atomic>
#include <cerrno>
#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <utility>

#include <fcntl.h>
#include <sys/file.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <unistd.h>

#include <nexigen/chrono/detail/time_base_segment.hpp>

namespace nexigen::chrono::detail {
namespace {

/// @brief POSIX shared-memory names: one leading slash, no further separators.
///
/// @param name Candidate name.
/// @return `true` when the name can be passed to `shm_open`.
[[nodiscard]] bool valid_segment_name(std::string_view name) noexcept {
    if (name.size() < 2U || name.size() > 255U || name[0] != '/') {
        return false;
    }
    if (name.find('/', 1U) != std::string_view::npos) {
        return false;
    }
    return name.find('\0') == std::string_view::npos;
}

/// @brief Initialize a fresh block in mapped storage under a takeover-safe order.
///
/// The magic is cleared first and restored last, so a reader that copies during
/// the initialization rejects the copy instead of accepting a half-written
/// table. The generation is set to an even non-zero value so a reader that
/// cached the previous incarnation (whose generation was 0 at creation) sees a
/// change.
///
/// @param block Mapping base.
/// @param boot_token Boot token to publish.
void initialize_block(TimeBaseBlock* block, std::uint64_t boot_token) noexcept {
    TimeBaseBlock fresh{};
    fresh.magic = 0U;
    fresh.boot_token = boot_token;
    fresh.generation = 2U;

    block->magic = 0U;
#if !defined(__SANITIZE_THREAD__)
    std::atomic_thread_fence(std::memory_order_release);
#endif
    *block = fresh;
#if !defined(__SANITIZE_THREAD__)
    std::atomic_thread_fence(std::memory_order_release);
#endif
    block->magic = time_base_magic;
}

/// @brief Whether a mapped block carries a compatible header.
///
/// @param block Mapping base.
/// @return `true` when magic and version match.
[[nodiscard]] bool compatible_header(const TimeBaseBlock* block) noexcept {
    return block != nullptr && block->magic == time_base_magic &&
           block->version == time_base_version;
}

} // namespace

TimeBaseSegment::TimeBaseSegment(int fd, TimeBaseBlock* block, std::string name,
                                 bool owner) noexcept
    : fd_(fd), block_(block), name_(std::move(name)), owner_(owner) {}

TimeBaseSegment::TimeBaseSegment(TimeBaseSegment&& other) noexcept
    : fd_(other.fd_), block_(other.block_), name_(std::move(other.name_)), owner_(other.owner_) {
    other.fd_ = -1;
    other.block_ = nullptr;
    other.owner_ = false;
}

TimeBaseSegment& TimeBaseSegment::operator=(TimeBaseSegment&& other) noexcept {
    if (this != &other) {
        release();
        fd_ = other.fd_;
        block_ = other.block_;
        name_ = std::move(other.name_);
        owner_ = other.owner_;
        other.fd_ = -1;
        other.block_ = nullptr;
        other.owner_ = false;
    }
    return *this;
}

TimeBaseSegment::~TimeBaseSegment() {
    release();
}

core::Result<TimeBaseSegment> TimeBaseSegment::create(std::string_view name,
                                                      std::uint64_t boot_token) noexcept {
    if (!valid_segment_name(name)) {
        return std::unexpected(core::ErrorCode::InvalidArgument);
    }
    const std::string owned_name{name};
    const int fd = ::shm_open(owned_name.c_str(), O_CREAT | O_RDWR, 0644);
    if (fd < 0) {
        return std::unexpected(core::ErrorCode::SystemError);
    }
    // The advisory lock is the ownership token: the kernel drops it on death.
    if (::flock(fd, LOCK_EX | LOCK_NB) != 0) {
        ::close(fd);
        return std::unexpected(errno == EWOULDBLOCK ? core::ErrorCode::Conflict
                                                    : core::ErrorCode::SystemError);
    }
    // Force the read bits regardless of the daemon's umask.
    static_cast<void>(::fchmod(fd, 0644));
    if (::ftruncate(fd, static_cast<off_t>(sizeof(TimeBaseBlock))) != 0) {
        ::close(fd);
        return std::unexpected(core::ErrorCode::SystemError);
    }
    void* const mapping =
        ::mmap(nullptr, sizeof(TimeBaseBlock), PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
    if (mapping == MAP_FAILED) {
        ::close(fd);
        return std::unexpected(core::ErrorCode::SystemError);
    }
    auto* const block = static_cast<TimeBaseBlock*>(mapping);
    initialize_block(block, boot_token);
    return TimeBaseSegment{fd, block, owned_name, true};
}

core::Result<TimeBaseSegment> TimeBaseSegment::attach(std::string_view name) noexcept {
    if (!valid_segment_name(name)) {
        return std::unexpected(core::ErrorCode::InvalidArgument);
    }
    const std::string owned_name{name};
    const int fd = ::shm_open(owned_name.c_str(), O_RDONLY, 0);
    if (fd < 0) {
        return std::unexpected(core::ErrorCode::NotFound);
    }
    struct stat info{};
    if (::fstat(fd, &info) != 0 || info.st_size < static_cast<off_t>(sizeof(TimeBaseBlock))) {
        ::close(fd);
        return std::unexpected(core::ErrorCode::CorruptData);
    }
    void* const mapping = ::mmap(nullptr, sizeof(TimeBaseBlock), PROT_READ, MAP_SHARED, fd, 0);
    if (mapping == MAP_FAILED) {
        ::close(fd);
        return std::unexpected(core::ErrorCode::SystemError);
    }
    auto* const block = static_cast<TimeBaseBlock*>(mapping);
    if (!compatible_header(block)) {
        ::munmap(mapping, sizeof(TimeBaseBlock));
        ::close(fd);
        return std::unexpected(core::ErrorCode::CorruptData);
    }
    return TimeBaseSegment{fd, block, owned_name, false};
}

void TimeBaseSegment::unlink() noexcept {
    if (owner_ && !name_.empty()) {
        ::shm_unlink(name_.c_str());
        owner_ = false;
    }
}

void TimeBaseSegment::release() noexcept {
    if (block_ != nullptr) {
        ::munmap(block_, sizeof(TimeBaseBlock));
        block_ = nullptr;
    }
    if (owner_ && !name_.empty()) {
        ::shm_unlink(name_.c_str());
    }
    owner_ = false;
    if (fd_ >= 0) {
        ::close(fd_);
        fd_ = -1;
    }
}

} // namespace nexigen::chrono::detail
