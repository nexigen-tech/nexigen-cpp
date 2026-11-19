// nexigen::chrono — mapped time-base segment lifecycle (change
// add-time-base-service; spec chrono/time-base "Shared-memory publication
// contract"; design D-B, D-C).
//
// A segment is one POSIX shared-memory object holding exactly one
// `TimeBaseBlock`. The daemon creates it, holds an exclusive `flock` for its
// lifetime, and unlinks it on a clean shutdown; consumers attach read-only.
//
// Ownership is decided by the advisory lock, not by a recorded pid: the kernel
// releases the lock when the owner dies, so `create()` on a leftover segment
// from a crashed daemon succeeds by taking over, while `create()` against a
// live daemon fails with `Conflict`. The takeover re-initializes the block with
// a non-zero generation so a reader that cached the previous incarnation
// refreshes.
//
// The segment is 4 KiB plus the header, aligned for direct `mmap` access. Mode
// is forced to 0644 so applications running under another uid can read it.
#pragma once

#include <string>
#include <string_view>

#include <nexigen/chrono/time_base_block.hpp>
#include <nexigen/core/result.hpp>

namespace nexigen::chrono::detail {

/// @brief One mapped time-base segment, owned or attached.
///
/// Move-only. Destroying an owner unmaps, unlinks and releases the lock;
/// destroying a reader only unmaps. Every operation is cold path: creation,
/// attachment and teardown happen once per process or daemon lifetime.
///
/// @note Spec `chrono/time-base`, requirement "Shared-memory publication
///       contract".
class TimeBaseSegment {
public:
    /// @brief Create the segment, or take over a stale one.
    ///
    /// @param name POSIX shared-memory name (`/` followed by the suite key).
    /// @param boot_token 64-bit prefix of the kernel boot id written into the block.
    /// @return An owning segment, `Conflict` when a live owner holds the lock,
    ///         `InvalidArgument` for a malformed name, or `SystemError` when an
    ///         OS call fails.
    /// @note Cold path only.
    [[nodiscard]] static core::Result<TimeBaseSegment> create(std::string_view name,
                                                              std::uint64_t boot_token) noexcept;

    /// @brief Attach read-only to an existing segment.
    ///
    /// @param name POSIX shared-memory name of the segment.
    /// @return A reader segment, `NotFound` when the segment does not exist, or
    ///         `CorruptData` when its header is not a compatible block.
    /// @note Cold path only.
    [[nodiscard]] static core::Result<TimeBaseSegment> attach(std::string_view name) noexcept;

    /// @brief Transfer ownership of the mapping and the descriptor.
    ///
    /// @param other Source segment; left disengaged.
    TimeBaseSegment(TimeBaseSegment&& other) noexcept;

    /// @brief Release the current mapping and take over `other`'s.
    ///
    /// @param other Source segment; left disengaged.
    /// @return `*this`.
    TimeBaseSegment& operator=(TimeBaseSegment&& other) noexcept;

    /// @brief Copying is deleted: the type owns a mapping and a descriptor.
    TimeBaseSegment(const TimeBaseSegment&) = delete;
    /// @brief Copy assignment is deleted: the type owns a mapping and a descriptor.
    TimeBaseSegment& operator=(const TimeBaseSegment&) = delete;

    /// @brief Unmap, close, and unlink when this object is the owner.
    ~TimeBaseSegment();

    /// @brief The mapped block, writable only for the owner's process.
    ///
    /// @return Pointer to the mapped block, or `nullptr` when disengaged.
    [[nodiscard]] TimeBaseBlock* block() noexcept { return block_; }

    /// @brief The mapped block, read-only view.
    ///
    /// @return Pointer to the mapped block, or `nullptr` when disengaged.
    [[nodiscard]] const TimeBaseBlock* block() const noexcept { return block_; }

    /// @brief Whether this object created (and still owns) the segment.
    ///
    /// @return `true` for the daemon-side owner.
    [[nodiscard]] bool is_owner() const noexcept { return owner_; }

    /// @brief The POSIX name this segment was opened with.
    ///
    /// @return Borrowed view valid for the segment's lifetime.
    [[nodiscard]] const std::string& name() const noexcept { return name_; }

    /// @brief Unlink the name so no new reader can attach.
    ///
    /// Existing mappings stay valid. Idempotent; only the owner acts.
    void unlink() noexcept;

private:
    /// @brief Adopt an already-mapped segment.
    ///
    /// @param fd Mapped descriptor.
    /// @param block Mapping base.
    /// @param name POSIX name.
    /// @param owner Whether this object holds the exclusive lock.
    TimeBaseSegment(int fd, TimeBaseBlock* block, std::string name, bool owner) noexcept;

    /// @brief Release the mapping and descriptor without touching the name.
    void release() noexcept;

    int fd_{-1};                    ///< Mapped descriptor; `-1` when disengaged.
    TimeBaseBlock* block_{nullptr}; ///< Mapping base; `nullptr` when disengaged.
    std::string name_{};            ///< POSIX name used for unlink.
    bool owner_{false};             ///< Holds the exclusive advisory lock.
};

} // namespace nexigen::chrono::detail
