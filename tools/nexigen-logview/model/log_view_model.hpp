// nexigen-logview — headless model shared by the GUI and tests.
#pragma once

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include <nexigen/core/result.hpp>
#include <nexigen/core/spsc_ring_buffer.hpp>
#include <nexigen/logging/log_frame.hpp>

#include "color_manager.hpp"
#include "export_writer.hpp"
#include "filter.hpp"
#include "find_state.hpp"
#include "importer.hpp"
#include "ingress_record.hpp"
#include "logview_config.hpp"
#include "record_ring.hpp"
#include "resource_guard.hpp"
#include "time_context.hpp"

namespace nexigen::logview {

/// @brief Owns the history, filter index, selection, time state, and export job.
///
/// The model is single-threaded: the UI thread calls every method. The only
/// shared input is the SPSC capture queue, drained explicitly by `drain()`.
class LogViewModel {
public:
    /// @brief Create a model sized from a memory budget.
    /// @param budget_bytes Model budget in bytes.
    /// @param config Persisted configuration.
    /// @return The model, or an allocation/invalid-argument failure.
    [[nodiscard]] static nexigen::core::Result<std::unique_ptr<LogViewModel>>
    create(std::uint64_t budget_bytes, LogViewConfig config);

    /// @brief History ring.
    /// @return Mutable reference.
    [[nodiscard]] RecordRing& ring() noexcept { return ring_; }
    /// @brief History ring.
    /// @return Const reference.
    [[nodiscard]] const RecordRing& ring() const noexcept { return ring_; }
    /// @brief Filter index.
    /// @return Mutable reference.
    [[nodiscard]] FilterIndex& filter() noexcept { return filter_; }
    /// @brief Filter index.
    /// @return Const reference.
    [[nodiscard]] const FilterIndex& filter() const noexcept { return filter_; }
    /// @brief Find state.
    /// @return Mutable reference.
    [[nodiscard]] FindState& find() noexcept { return find_; }
    /// @brief Find state.
    /// @return Const reference.
    [[nodiscard]] const FindState& find() const noexcept { return find_; }
    /// @brief Colors.
    /// @return Mutable reference.
    [[nodiscard]] ColorManager& colors() noexcept { return colors_; }
    /// @brief Colors.
    /// @return Const reference.
    [[nodiscard]] const ColorManager& colors() const noexcept { return colors_; }
    /// @brief Time state.
    /// @return Mutable reference.
    [[nodiscard]] TimeContext& time() noexcept { return time_; }
    /// @brief Time state.
    /// @return Const reference.
    [[nodiscard]] const TimeContext& time() const noexcept { return time_; }
    /// @brief Active configuration.
    /// @return Mutable reference.
    [[nodiscard]] LogViewConfig& config() noexcept { return config_; }
    /// @brief Active configuration.
    /// @return Const reference.
    [[nodiscard]] const LogViewConfig& config() const noexcept { return config_; }
    /// @brief Resource guard.
    /// @return Mutable reference.
    [[nodiscard]] ResourceGuard& guard() noexcept { return guard_; }
    /// @brief Resource guard.
    /// @return Const reference.
    [[nodiscard]] const ResourceGuard& guard() const noexcept { return guard_; }

    /// @brief Ingest one frame from the transport.
    /// @param frame Received frame.
    /// @param lost_before Transport-reported missing frames before it.
    void ingest_frame(const nexigen::logging::LogFrame& frame, std::uint64_t lost_before) noexcept;
    /// @brief Ingest one transport gap marker.
    /// @param count Missing frames.
    /// @param timestamp_ns Reference timestamp.
    void ingest_transport_gap(std::uint64_t count, std::uint64_t timestamp_ns) noexcept;
    /// @brief Ingest one synthetic gap attributed to ingress or publisher loss.
    /// @param kind Gap kind.
    /// @param count Missing records.
    /// @param timestamp_ns Reference timestamp.
    void ingest_gap(RecordKind kind, std::uint64_t count, std::uint64_t timestamp_ns) noexcept;

    /// @brief Drain a capture queue and ingest every record.
    /// @param queue SPSC hand-off queue.
    /// @param overflow_total Capture overflow counter after the drain.
    /// @return Number of records ingested.
    std::size_t drain(nexigen::core::SpscRingBuffer<IngressRecord, 65536>& queue,
                      std::uint64_t overflow_total) noexcept;

    /// @brief Start a chunked rebuild after a criterion change.
    void begin_filter_rebuild() noexcept;
    /// @brief Apply one bounded rebuild chunk.
    /// @param max_records Records per frame.
    void step_filter_rebuild(std::size_t max_records) noexcept;
    /// @brief Filter rebuild progress.
    /// @return Progress snapshot.
    [[nodiscard]] FilterIndex::Progress filter_progress() const noexcept {
        return filter_.progress();
    }

    /// @brief Start a chunked find.
    /// @param query Substring; empty clears.
    void begin_find(std::string_view query) noexcept;
    /// @brief Apply one bounded find chunk.
    /// @param max_records Visible entries per frame.
    void step_find(std::size_t max_records) noexcept;
    /// @brief Select the next find match.
    /// @return Absolute record number.
    std::optional<std::uint64_t> find_next() noexcept;
    /// @brief Select the previous find match.
    /// @return Absolute record number.
    std::optional<std::uint64_t> find_previous() noexcept;

    /// @brief Pause or resume the displayed range.
    /// @param paused New pause state.
    void set_paused(bool paused) noexcept;
    /// @brief Whether the view is paused.
    /// @return Pause state.
    [[nodiscard]] bool paused() const noexcept { return paused_; }
    /// @brief Records ingested since the pause started.
    /// @return Count.
    [[nodiscard]] std::uint64_t new_since_pause() const noexcept {
        return paused_ && ring_.tail() > pause_tail_ ? ring_.tail() - pause_tail_ : 0U;
    }
    /// @brief Displayed records overwritten while paused.
    /// @return Count.
    [[nodiscard]] std::uint64_t overwritten_while_paused() const noexcept {
        return pause_overwritten_;
    }
    /// @brief Oldest displayed absolute number.
    /// @return View floor.
    [[nodiscard]] std::uint64_t view_floor() const noexcept {
        return paused_ ? pause_floor_ : ring_.head();
    }
    /// @brief One past the newest displayed absolute number.
    /// @return View end.
    [[nodiscard]] std::uint64_t view_end() const noexcept {
        return paused_ ? std::min(pause_tail_, ring_.tail()) : ring_.tail();
    }
    /// @brief Number of visible entries in the displayed range.
    /// @return Displayed visible count.
    [[nodiscard]] std::size_t display_count() const noexcept;

    /// @brief Select only one record.
    /// @param seq Absolute record number.
    void select_only(std::uint64_t seq) noexcept;
    /// @brief Toggle one record's selection.
    /// @param seq Absolute record number.
    void toggle_selection(std::uint64_t seq) noexcept;
    /// @brief Select every record between the anchor and `seq` in visible order.
    /// @param seq Absolute record number.
    void select_to(std::uint64_t seq) noexcept;
    /// @brief Select every visible record.
    void select_all_visible() noexcept;
    /// @brief Clear selection and anchor.
    void clear_selection() noexcept;
    /// @brief Last clicked record.
    /// @return Absolute number, or `std::nullopt`.
    [[nodiscard]] std::optional<std::uint64_t> last_selected() const noexcept {
        return last_selected_seq_;
    }
    /// @brief Drop the history, selection, bookmarks, and session counters.
    void clear_history() noexcept;
    /// @brief Reset every setting to its built-in default.
    ///
    /// Service name, memory budget, and thread placement cannot change in a
    /// running process; `restart_required()` reports when the persisted
    /// defaults differ from the active runtime values.
    void reset_configuration() noexcept;

    /// @brief Whether a restart is required to apply the persisted defaults.
    /// @return `true` when service, memory, or thread placement differ from the
    ///         values the current process started with.
    [[nodiscard]] bool restart_required() const noexcept;
    /// @brief Service name the running capture uses.
    /// @return Borrowed runtime service name.
    [[nodiscard]] const std::string& runtime_service_name() const noexcept {
        return runtime_service_;
    }
    /// @brief Memory budget the running ring was sized from.
    /// @return Runtime memory budget in MiB.
    [[nodiscard]] std::uint32_t runtime_memory_mb() const noexcept { return runtime_memory_mb_; }
    /// @brief Anchor record selected by the last plain click.
    /// @return Absolute number, or `std::nullopt`.
    [[nodiscard]] std::optional<std::uint64_t> selection_anchor() const noexcept {
        return selection_anchor_;
    }

    /// @brief Toggle a bookmark on one record.
    /// @param seq Absolute record number.
    /// @return New bookmark state.
    bool toggle_bookmark(std::uint64_t seq) noexcept;
    /// @brief Next bookmarked record after `seq`.
    /// @param seq Current record, or `std::nullopt` to start from the floor.
    /// @return Absolute number, or `std::nullopt`.
    std::optional<std::uint64_t> next_bookmark(std::optional<std::uint64_t> seq) const noexcept;
    /// @brief Previous bookmarked record before `seq`.
    /// @param seq Current record, or `std::nullopt` to start from the tail.
    /// @return Absolute number, or `std::nullopt`.
    std::optional<std::uint64_t> previous_bookmark(std::optional<std::uint64_t> seq) const noexcept;

    /// @brief Next Warning/Error/Fatal record after `seq`.
    /// @param seq Current record, or `std::nullopt` to start from the floor.
    /// @return Absolute number, or `std::nullopt` when none matches.
    std::optional<std::uint64_t> next_attention(std::optional<std::uint64_t> seq) const noexcept;
    /// @brief Previous Warning/Error/Fatal record before `seq`.
    /// @param seq Current record, or `std::nullopt` to start from the tail.
    /// @return Absolute number, or `std::nullopt` when none matches.
    std::optional<std::uint64_t>
    previous_attention(std::optional<std::uint64_t> seq) const noexcept;

    /// @brief Counters for the toolbar.
    struct Counters {
        std::uint64_t received{0};  ///< Received frames.
        std::uint64_t synthetic{0}; ///< Gap/overflow records.
        std::uint64_t evicted{0};   ///< Evictions from the ring.
        std::uint64_t visible{0};   ///< Displayed visible rows.
        std::uint64_t total{0};     ///< Retained records.
        std::uint64_t rejected{0};  ///< Protocol rejections from capture.
    };
    /// @brief Read the toolbar counters.
    /// @return Snapshot.
    [[nodiscard]] Counters counters() const noexcept;
    /// @brief Record protocol rejections from capture.
    /// @param count Number of rejected frames.
    void note_rejected(std::uint64_t count = 1) noexcept { rejected_ += count; }
    /// @brief Last measured incoming rate.
    /// @return Messages per second.
    [[nodiscard]] double rate() const noexcept { return rate_; }

    /// @brief Feed the process CPU clock and refresh the rate/ECO state.
    /// @param now_ns Monotonic time.
    /// @param process_cpu_ns Process CPU time.
    /// @return Resource mode after the sample.
    ResourceMode tick(std::uint64_t now_ns, std::uint64_t process_cpu_ns) noexcept;

    /// @brief Start an asynchronous export of the selected records.
    /// @param path Destination file.
    /// @param format Output format.
    /// @return `Status{}` when the job started.
    [[nodiscard]] nexigen::core::Status start_export(const std::string& path,
                                                     ExportFormat format) noexcept;
    /// @brief Queue one bounded chunk of the export.
    /// @param max_records Maximum records queued in this call.
    void step_export(std::size_t max_records) noexcept;
    /// @brief Cancel the export.
    void cancel_export() noexcept;
    /// @brief Export progress.
    /// @return Stats snapshot.
    [[nodiscard]] ExportStats export_stats() const noexcept;
    /// @brief Whether an export is running.
    /// @return `true` between `start_export` and completion.
    [[nodiscard]] bool exporting() const noexcept { return exporting_; }

    /// @brief Import a file into the current history and start a rebuild.
    /// @param path File to read.
    /// @param stats Import counters.
    /// @return `Status{}` on success.
    [[nodiscard]] nexigen::core::Status import_now(const std::string& path,
                                                   ImportStats& stats) noexcept;
    /// @brief Start a chunked import (the file is opened immediately).
    /// @param path File to read.
    /// @param stats Destination counters.
    /// @return `Status{}` on success.
    [[nodiscard]] nexigen::core::Status begin_import(const std::string& path,
                                                     ImportStats& stats) noexcept;
    /// @brief Import one bounded chunk.
    /// @param max_lines Maximum lines read in this call.
    void step_import(std::size_t max_lines) noexcept;
    /// @brief Whether a chunked import is running.
    /// @return `true` until the file is exhausted.
    [[nodiscard]] bool importing() const noexcept { return importing_; }
    /// @brief Live import counters.
    /// @return Borrowed counters.
    [[nodiscard]] const ImportStats& import_stats() const noexcept { return import_stats_; }
    /// @brief Import completion ratio.
    /// @return Progress in `0.0..1.0`.
    [[nodiscard]] double import_progress() const noexcept;

    /// @brief Reset colors to defaults.
    void reset_colors() noexcept;

    /// @brief Cold-path diagnostic rendering.
    /// @return Text with ring and filter counters.
    [[nodiscard]] std::string to_string() const;

private:
    /// @brief Construct a model from an allocated ring.
    LogViewModel(RecordRing ring, LogViewConfig config);

    /// @brief Apply pending hidden applications to a freshly observed source.
    void apply_pending_hidden(std::size_t slot) noexcept;
    /// @brief Internal frame ingestion after gap emission.
    void push_frame(const nexigen::logging::LogFrame& frame) noexcept;
    /// @brief Common gap ingestion.
    void push_gap(RecordKind kind, std::uint64_t count, std::uint64_t timestamp_ns) noexcept;
    /// @brief Update pause bookkeeping after an ingest/eviction.
    void refresh_pause() noexcept;

    RecordRing ring_;                                    ///< History.
    FilterIndex filter_{};                               ///< Visible index.
    FindState find_{};                                   ///< Find state.
    ColorManager colors_{};                              ///< Colors.
    TimeContext time_{};                                 ///< Time state.
    std::string runtime_service_{};                      ///< Service active for this process.
    std::uint32_t runtime_memory_mb_{0};                 ///< Ring budget active for this process.
    nexigen::core::ThreadSpec runtime_capture_thread_{}; ///< Active capture placement.
    nexigen::core::ThreadSpec runtime_export_thread_{};  ///< Active export placement.
    LogViewConfig config_{};                             ///< Configuration.
    ResourceGuard guard_{};                              ///< ECO guard.
    ExportWriter export_{};                              ///< Export job.
    std::vector<std::string> pending_hidden_{};          ///< Hidden sources not yet observed.

    std::uint64_t received_{0};             ///< Received frames.
    std::uint64_t synthetic_{0};            ///< Synthetic records.
    std::uint64_t rejected_{0};             ///< Rejected frames.
    std::uint64_t last_overflow_seen_{0};   ///< Last capture overflow counter.
    std::uint64_t ingested_since_tick_{0};  ///< Records since the last rate sample.
    std::uint64_t rate_window_start_ns_{0}; ///< Rate window start.
    double rate_{0.0};                      ///< Last measured rate.

    bool paused_{false};                 ///< Pause state.
    std::uint64_t pause_tail_{0};        ///< Tail at pause start.
    std::uint64_t pause_floor_{0};       ///< Current displayed floor.
    std::uint64_t pause_overwritten_{0}; ///< Displayed records overwritten.
    std::size_t pause_visible_count_{0}; ///< Visible count frozen at pause.

    std::unique_ptr<ImportSession> import_session_{}; ///< Chunked import.
    ImportStats import_stats_{};                      ///< Import counters.
    bool importing_{false};                           ///< Chunked import running.

    std::optional<std::uint64_t> selection_anchor_{};  ///< Shift-click anchor.
    std::optional<std::uint64_t> last_selected_seq_{}; ///< Last clicked record.

    bool exporting_{false};                  ///< Export job active.
    std::uint64_t export_cursor_{0};         ///< Next absolute record number to examine.
    std::uint64_t export_target_tail_{0};    ///< Tail captured when the export started.
    std::uint64_t export_previous_stamp_{0}; ///< Previous visible timestamp.
    bool export_have_previous_{false};       ///< Whether the previous stamp is valid.
};

/// @brief Free diagnostic mirror of `LogViewModel::to_string`.
/// @param model Model to render.
/// @return The same text.
[[nodiscard]] std::string to_string(const LogViewModel& model);

} // namespace nexigen::logview
