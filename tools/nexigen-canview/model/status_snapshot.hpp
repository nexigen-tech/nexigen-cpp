// nexigen-canview — aggregated status and diagnostics.
#pragma once

#include <cstddef>
#include <cstdint>
#include <string>

#include "capture_stats.hpp"
#include "can_record_ring.hpp"
#include "canopen_reindex.hpp"
#include "eds_library.hpp"
#include "overwrite_table.hpp"
#include "session_export.hpp"
#include "session_import.hpp"

namespace nexigen::canview {

/// @brief Optional components folded into a status snapshot.
struct StatusSources {
    const CaptureStats* capture{nullptr};            ///< Capture counters.
    const CanRecordRing* ring{nullptr};              ///< History.
    const OverwriteTable* overwrite{nullptr};        ///< Overwrite rows.
    const EdsLibrary* eds{nullptr};                  ///< EDS cache.
    const CanOpenReindexer* reindex{nullptr};        ///< Binding reindex.
    const AsyncExportWriter* export_writer{nullptr}; ///< Active export.
    const ImportResult* import{nullptr};             ///< Last import.
    const std::uint64_t* rejected{nullptr};          ///< Viewer-rejected invalid frames.
};

/// @brief Complete diagnostic counter set.
struct StatusSnapshot {
    std::uint32_t active_channels{0};     ///< Channels that started.
    std::uint32_t failed_channels{0};     ///< Channels that failed to start.
    std::uint64_t ingress_size{0};        ///< Current ingress occupancy.
    std::uint64_t history_size{0};        ///< Retained history records.
    std::uint64_t history_capacity{0};    ///< History capacity.
    std::uint64_t history_evictions{0};   ///< History evictions.
    std::uint64_t received{0};            ///< Valid frames accepted.
    std::uint64_t bus_idle{0};            ///< Bus-idle heartbeats.
    std::uint64_t synthetic{0};           ///< Synthetic loss markers.
    std::uint64_t overflow{0};            ///< Ingress overflows.
    std::uint64_t mismatched{0};          ///< Wrong-channel frames.
    std::uint64_t duplicates{0};          ///< Duplicate/reorder observations.
    std::uint64_t resets{0};              ///< Sequence resets.
    std::uint64_t hardware_gaps{0};       ///< Hardware-sequence gaps.
    std::uint64_t transport_losses{0};    ///< Transport losses.
    std::uint64_t ingress_losses{0};      ///< Ingress queue losses.
    std::uint64_t driver_drops{0};        ///< Driver-reported drops.
    std::uint64_t overwrite_rows{0};      ///< Overwrite rows.
    std::uint64_t overwrite_evictions{0}; ///< Overwrite evictions.
    std::uint64_t eds_files{0};           ///< Cached EDS files.
    std::uint64_t eds_bytes{0};           ///< Cached EDS bytes.
    std::uint64_t reindex_scanned{0};     ///< Reindex scanned records.
    std::uint64_t reindex_total{0};       ///< Reindex target records.
    bool reindex_active{false};           ///< Reindex running.
    std::uint64_t export_written{0};      ///< Export rows written.
    std::uint64_t export_skipped{0};      ///< Export rows skipped.
    bool export_active{false};            ///< Export running.
    std::uint64_t import_records{0};      ///< Imported records.
    std::uint64_t import_malformed{0};
    std::uint64_t rejected{0};     ///< Viewer-rejected invalid frames.
    bool eco{false};               ///< ECO mode active.
    std::string eco_reason{};      ///< ECO reason.
    std::uint64_t memory_bytes{0}; ///< Mapped model bytes.

    /// @brief Cold-path diagnostic rendering.
    /// @return Multi-field summary.
    [[nodiscard]] std::string to_string() const;
};

/// @brief Fold the available components into a snapshot.
/// @param sources Optional component pointers.
/// @return Status snapshot.
[[nodiscard]] StatusSnapshot collect_status(const StatusSources& sources) noexcept;

} // namespace nexigen::canview
