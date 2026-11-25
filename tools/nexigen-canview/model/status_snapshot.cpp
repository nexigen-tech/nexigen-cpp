#include "status_snapshot.hpp"

#include <format>

namespace nexigen::canview {

StatusSnapshot collect_status(const StatusSources& sources) noexcept {
    StatusSnapshot out{};
    if (sources.capture != nullptr) {
        out.active_channels = sources.capture->active_channels;
        out.failed_channels = sources.capture->failed_channels;
        out.ingress_size = sources.capture->ingress_size;
        out.received = sources.capture->received;
        out.bus_idle = sources.capture->bus_idle;
        out.synthetic = sources.capture->synthetic;
        out.overflow = sources.capture->overflow;
        out.mismatched = sources.capture->mismatched;
        out.duplicates = sources.capture->duplicates;
        out.resets = sources.capture->resets;
        out.hardware_gaps = sources.capture->hardware_gaps;
        out.transport_losses = sources.capture->transport_losses;
        out.ingress_losses = sources.capture->ingress_losses;
        out.driver_drops = sources.capture->driver_drops;
    }
    if (sources.ring != nullptr) {
        out.history_size = sources.ring->size();
        out.history_capacity = sources.ring->capacity();
        out.history_evictions = sources.ring->evictions();
        out.memory_bytes = sources.ring->mapped_bytes();
    }
    if (sources.overwrite != nullptr) {
        out.overwrite_rows = sources.overwrite->size();
        out.overwrite_evictions = sources.overwrite->evictions();
    }
    if (sources.eds != nullptr) {
        out.eds_files = sources.eds->file_count();
        out.eds_bytes = sources.eds->total_bytes();
    }
    if (sources.reindex != nullptr) {
        const auto progress = sources.reindex->progress();
        out.reindex_scanned = progress.scanned;
        out.reindex_total = progress.total;
        out.reindex_active = progress.active;
    }
    if (sources.export_writer != nullptr) {
        const auto progress = sources.export_writer->progress();
        out.export_written = progress.written;
        out.export_skipped = progress.skipped;
        out.export_active = progress.active;
    }
    if (sources.rejected != nullptr) {
        out.rejected = *sources.rejected;
    }
    if (sources.import != nullptr) {
        out.import_records = sources.import->records.size();
        out.import_malformed = sources.import->malformed;
    }
    return out;
}

std::string StatusSnapshot::to_string() const {
    return std::format(
        "Status(channels={}/{} failed={}, ingress={}, history={}/{}, evictions={}, received={}, "
        "bus_idle={}, synthetic={}, overflow={}, mismatched={}, dup={}, resets={}, "
        "hw_gaps={}, transport={}, ingress_losses={}, driver_drops={}, "
        "overwrite={} evictions={}, eds={}/{} B, reindex={}/{} active={}, export={} skipped={} "
        "active={}, import={} malformed={}, rejected={}, eco={} ({}), memory={} B)",
        active_channels, active_channels + failed_channels, failed_channels, ingress_size,
        history_size, history_capacity, history_evictions, received, bus_idle, synthetic, overflow,
        mismatched, duplicates, resets, hardware_gaps, transport_losses, ingress_losses,
        driver_drops, overwrite_rows, overwrite_evictions, eds_files, eds_bytes, reindex_scanned,
        reindex_total, reindex_active, export_written, export_skipped, export_active,
        import_records, import_malformed, rejected, eco, eco_reason, memory_bytes);
}

} // namespace nexigen::canview
