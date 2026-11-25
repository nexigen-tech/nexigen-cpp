// nexigen-canview — EDS file cache with change detection.
#pragma once

#include <cstddef>
#include <cstdint>
#include <map>
#include <memory>
#include <string>
#include <vector>

#include "eds_parser.hpp"

namespace nexigen::canview {

/// @brief Outcome of `EdsLibrary::load`.
struct EdsLoadResult {
    std::shared_ptr<const EdsDictionary> dictionary{}; ///< Cached or freshly parsed dictionary.
    std::vector<EdsDiagnostic> diagnostics{};          ///< Parser/cache diagnostics.
    bool from_cache{false};                            ///< File unchanged since the previous load.
    bool reloaded{false};                              ///< File changed and was reparsed.
    bool failed{false};                                ///< No usable dictionary was produced.

    /// @brief Cold-path diagnostic rendering.
    /// @return Text with cache state and diagnostics count.
    [[nodiscard]] std::string to_string() const;
};

/// @brief Bounded cache of parsed EDS dictionaries.
///
/// The cache key is the path; size and mtime detect changes. A changed file is
/// reparsed and the previous dictionary is replaced only on success, so an
/// existing binding keeps working when the new file is malformed.
class EdsLibrary {
public:
    /// @brief Maximum cached files.
    static constexpr std::size_t max_files{32U};
    /// @brief Default EDS memory budget (32 MiB).
    static constexpr std::uint64_t default_budget_bytes{32ULL * 1024ULL * 1024ULL};

    /// @brief Construct a library.
    /// @param budget_bytes Total file-byte budget.
    /// @param limits Parser limits.
    explicit EdsLibrary(std::uint64_t budget_bytes = default_budget_bytes, EdsLimits limits = {})
        : budget_bytes_(budget_bytes), limits_(limits) {}

    /// @brief Load or refresh one file.
    /// @param path File path.
    /// @return Dictionary/cache outcome.
    EdsLoadResult load(const std::string& path);

    /// @brief Look up a previously loaded dictionary.
    /// @param path File path.
    /// @return Dictionary or `nullptr`.
    [[nodiscard]] std::shared_ptr<const EdsDictionary> find(const std::string& path) const;

    /// @brief Drop every cached dictionary.
    void clear() noexcept;

    /// @brief Cached file count.
    /// @return Count.
    [[nodiscard]] std::size_t file_count() const noexcept { return cache_.size(); }
    /// @brief Bytes used by cached files.
    /// @return Byte count.
    [[nodiscard]] std::uint64_t total_bytes() const noexcept { return total_bytes_; }
    /// @brief Configured byte budget.
    /// @return Budget.
    [[nodiscard]] std::uint64_t budget_bytes() const noexcept { return budget_bytes_; }

private:
    /// @brief One cached file.
    /// @brief One value/name row.

    struct Entry {
        std::shared_ptr<const EdsDictionary> dictionary{}; ///< Parsed dictionary.
        std::size_t size{0};                               ///< File size at parse time.
        std::int64_t mtime{0};                             ///< File mtime at parse time.
    };

    /// @brief Read the size and mtime of one file.
    /// @param path File path.
    /// @param size Receives the size.
    /// @param mtime Receives the mtime.
    /// @return `true` when `stat` succeeded.
    [[nodiscard]] static bool stat_file(const std::string& path, std::size_t& size,
                                        std::int64_t& mtime) noexcept;

    std::map<std::string, Entry> cache_{};             ///< Path -> cached dictionary.
    std::uint64_t budget_bytes_{default_budget_bytes}; ///< Byte budget.
    std::uint64_t total_bytes_{0};                     ///< Bytes used.
    EdsLimits limits_{};                               ///< Parser limits.
};

} // namespace nexigen::canview
