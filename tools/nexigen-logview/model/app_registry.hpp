// nexigen-logview — fixed 256-slot application registry.
#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>

namespace nexigen::logview {

/// @brief One application identity observed by the viewer.
struct AppSlot {
    std::array<char, 16> id{};      ///< Sixteen-byte application code, zero-filled.
    std::uint64_t hash{0};          ///< FNV-1a hash of the stored bytes.
    std::uint64_t record_count{0};  ///< Retained records from this application.
    std::uint64_t last_stamp_ns{0}; ///< Timestamp of the newest retained record.
    std::uint16_t color_index{0};   ///< Palette index assigned on discovery.
    bool used{false};               ///< Slot currently holds an application.

    /// @brief The stored application code as a bounded view.
    /// @return Up to sixteen bytes, trimmed at the first NUL.
    [[nodiscard]] std::string_view text() const noexcept {
        std::size_t n = 0;
        while (n < id.size() && id[n] != '\0') {
            ++n;
        }
        return std::string_view(id.data(), n);
    }

    /// @brief Cold-path diagnostic rendering.
    /// @return Text naming the application and its counters.
    [[nodiscard]] std::string to_string() const;
};

/// @brief Fixed-capacity application table with open addressing.
///
/// The table never grows: a new application is placed in the first free probe
/// slot, and a table full of 256 distinct applications drops further unknown
/// identities while keeping the frame record itself. Lookup and insertion are
/// cold enough for ingest (no allocation, one hash, a short probe chain).
class AppRegistry {
public:
    /// @brief Fixed slot count.
    static constexpr std::size_t capacity = 256;

    /// @brief Reset every slot and the palette cursor.
    void clear() noexcept;

    /// @brief Find or insert an application and update its counters.
    ///
    /// @param app Application code; longer input is truncated to sixteen bytes.
    /// @param stamp_ns Record timestamp in the monotonic domain.
    /// @return Slot index, or `capacity` when the table is full.
    [[nodiscard]] std::size_t observe(std::string_view app, std::uint64_t stamp_ns) noexcept;

    /// @brief Find an application without inserting it.
    /// @param app Application code to look up.
    /// @return Slot index, or `capacity` when absent.
    [[nodiscard]] std::size_t find(std::string_view app) const noexcept;

    /// @brief Record that one retained record from `slot` was evicted.
    /// @param slot Slot index returned by `observe`.
    void on_evict(std::size_t slot) noexcept;

    /// @brief Read one slot.
    /// @param slot Slot index; out-of-range returns an empty static slot.
    /// @return Borrowed slot.
    [[nodiscard]] const AppSlot& at(std::size_t slot) const noexcept;

    /// @brief The slot's assigned palette index.
    /// @param slot Slot index.
    /// @return Palette index in `0..255`.
    [[nodiscard]] std::uint16_t color_index(std::size_t slot) const noexcept;

    /// @brief Number of occupied slots.
    /// @return Count in `0..capacity`.
    [[nodiscard]] std::size_t size() const noexcept { return used_; }

    /// @brief Reset palette assignment so the next new application starts at zero.
    void reset_palette() noexcept { next_color_ = 0; }

    /// @brief Cold-path diagnostic rendering.
    /// @return Text with the occupied slot count.
    [[nodiscard]] std::string to_string() const;

private:
    /// @brief Hash one bounded application code.
    [[nodiscard]] static std::uint64_t hash_of(std::string_view app) noexcept;

    std::array<AppSlot, capacity> slots_{}; ///< Fixed table.
    std::size_t used_{0};                   ///< Occupied slot count.
    std::uint16_t next_color_{0};           ///< Next palette index to hand out.
};

/// @brief Free diagnostic mirror of `AppRegistry::to_string`.
/// @param registry Registry to render.
/// @return The same text.
[[nodiscard]] std::string to_string(const AppRegistry& registry);

} // namespace nexigen::logview
