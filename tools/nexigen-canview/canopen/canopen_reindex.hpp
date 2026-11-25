// nexigen-canview — bounded CANopen binding reindex.
#pragma once

#include <cstddef>
#include <cstdint>
#include <string>

#include "can_record_ring.hpp"
#include "cob_id_router.hpp"

namespace nexigen::canview {

/// @brief Progress of one CANopen reindex pass.
struct ReindexProgress {
    std::uint64_t scanned{0}; ///< Records examined.
    std::uint64_t total{0};   ///< Records targeted.
    bool active{false};       ///< Reindex running.

    /// @brief Completion ratio.
    /// @return Scanned divided by total, or `1.0`.
    [[nodiscard]] double ratio() const noexcept;
    /// @brief Cold-path rendering.
    /// @return Text with scanned, total and active state.
    [[nodiscard]] std::string to_string() const;
};

/// @brief Reassigns `co_node_index` after bindings change.
///
/// One call scans at most the configured chunk, so a full 2M-record history
/// never stalls a frame. Records that stop matching are cleared back to
/// `CanRecordView::no_node`.
class CanOpenReindexer {
public:
    /// @brief Default records per step.
    static constexpr std::size_t default_chunk{20'000U};

    /// @brief Start a pass over the retained history.
    /// @param ring History to scan.
    /// @param router Routes built from the current registry.
    void begin(const CanRecordRing& ring, const CobIdRouter& router) noexcept;

    /// @brief Scan one bounded chunk.
    /// @param ring History to update.
    /// @param max_records Chunk size.
    /// @return Progress after the chunk.
    ReindexProgress step(CanRecordRing& ring, std::size_t max_records = default_chunk) noexcept;

    /// @brief Current progress.
    /// @return Progress snapshot.
    [[nodiscard]] ReindexProgress progress() const noexcept;
    /// @brief Whether a pass is running.
    /// @return `true` until the history tail is reached.
    [[nodiscard]] bool active() const noexcept { return active_; }

    /// @brief Records newly associated with a binding.
    /// @return Counter.
    [[nodiscard]] std::uint64_t assigned() const noexcept { return assigned_; }
    /// @brief Records whose association was cleared.
    /// @return Counter.
    [[nodiscard]] std::uint64_t cleared() const noexcept { return cleared_; }

private:
    const CobIdRouter* router_{nullptr}; ///< Routes used by the pass.
    std::uint64_t start_{0};             ///< Head captured by the pass.
    std::uint64_t scan_{0};              ///< Next absolute sequence.
    std::uint64_t target_{0};            ///< Tail captured by the pass.
    std::uint64_t assigned_{0};          ///< Newly associated records.
    std::uint64_t cleared_{0};           ///< Cleared associations.
    bool active_{false};                 ///< Pass running.
};

} // namespace nexigen::canview
