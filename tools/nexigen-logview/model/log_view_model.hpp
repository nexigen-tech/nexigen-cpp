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
#include "log_view.hpp"
#include "logview_config.hpp"
#include "record_ring.hpp"
#include "resource_guard.hpp"
#include "time_context.hpp"

namespace nexigen::logview {

/// @brief Shared capture session plus independent log views.
///
/// The model is single-threaded: the UI thread calls every method. The only
/// shared input is the SPSC capture queue, drained explicitly by `drain()`.
/// The ring, application registry, colors, resource guard, time base, and
/// export writer are shared; every `LogView` owns its filters, find state,
/// selection, bookmarks, minimap, pause position, and time anchor.
class LogViewModel {
public:
    /// @brief Create a model sized from a memory budget.
    /// @param budget_bytes Model budget in bytes.
    /// @param config Persisted configuration.
    /// @return The model, or an allocation/invalid-argument failure.
    [[nodiscard]] static nexigen::core::Result<std::unique_ptr<LogViewModel>>
    create(std::uint64_t budget_bytes, LogViewConfig config);

    /// @brief History ring shared by every LIVE view.
    /// @return Mutable reference.
    [[nodiscard]] RecordRing& ring() noexcept { return ring_; }
    /// @brief History ring.
    /// @return Const reference.
    [[nodiscard]] const RecordRing& ring() const noexcept { return ring_; }
    /// @brief Shared application registry.
    /// @return Const reference.
    [[nodiscard]] const AppRegistry& apps() const noexcept { return ring_.apps(); }
    /// @brief Colors shared by every view.
    /// @return Mutable reference.
    [[nodiscard]] ColorManager& colors() noexcept { return colors_; }
    /// @brief Colors.
    /// @return Const reference.
    [[nodiscard]] const ColorManager& colors() const noexcept { return colors_; }
    /// @brief Shared time base before per-view T0 and thresholds.
    /// @return Mutable reference.
    [[nodiscard]] TimeContext& base_time() noexcept { return base_time_; }
    /// @brief Shared time base.
    /// @return Const reference.
    [[nodiscard]] const TimeContext& base_time() const noexcept { return base_time_; }
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

    /// @brief Focused view for keyboard and inspector actions.
    /// @return Borrowed view, or `nullptr` when the workspace is empty.
    [[nodiscard]] LogView* focused_view() noexcept;
    /// @brief Focused view.
    /// @return Borrowed view, or `nullptr` when the workspace is empty.
    [[nodiscard]] const LogView* focused_view() const noexcept;
    /// @brief Focused view identity.
    /// @return View id, or `0` when none is focused.
    [[nodiscard]] ViewId focused_id() const noexcept { return focused_id_; }
    /// @brief Focus one existing view.
    /// @param id View identity.
    void focus_view(ViewId id) noexcept;
    /// @brief Look up a view by identity.
    /// @param id View identity.
    /// @return Borrowed view, or `nullptr`.
    [[nodiscard]] LogView* view(ViewId id) noexcept;
    /// @brief Look up a view by identity.
    /// @param id View identity.
    /// @return Borrowed view, or `nullptr`.
    [[nodiscard]] const LogView* view(ViewId id) const noexcept;
    /// @brief View by creation order.
    /// @param index Zero-based index.
    /// @return Borrowed view, or `nullptr`.
    [[nodiscard]] LogView* view_at(std::size_t index) noexcept;
    /// @brief View by creation order.
    /// @param index Zero-based index.
    /// @return Borrowed view, or `nullptr`.
    [[nodiscard]] const LogView* view_at(std::size_t index) const noexcept;
    /// @brief Number of open views.
    /// @return Live plus paused view count.
    [[nodiscard]] std::size_t view_count() const noexcept { return views_.size(); }
    /// @brief Number of LIVE views including `All`.
    /// @return LIVE count.
    [[nodiscard]] std::size_t live_view_count() const noexcept;
    /// @brief Number of PAUSED views.
    /// @return PAUSED count.
    [[nodiscard]] std::size_t paused_view_count() const noexcept;

    /// @brief Create a LIVE source view or reopen `All`.
    /// @param kind Scope kind.
    /// @param source Fixed application code; ignored for `All`.
    /// @return New view id, or a limit/allocation failure.
    [[nodiscard]] nexigen::core::Result<ViewId> create_view(ViewKind kind, std::string source);
    /// @brief Clone one view as LIVE or PAUSED.
    /// @param source_id View to clone.
    /// @param mode Clone mode.
    /// @param options State groups to copy.
    /// @return New view id, or a limit/snapshot failure.
    [[nodiscard]] nexigen::core::Result<ViewId> clone_view(ViewId source_id, CloneMode mode,
                                                           const CloneOptions& options = {});
    /// @brief Close one view and release its state.
    /// @param id View identity.
    /// @return `Status{}` when the view existed.
    [[nodiscard]] nexigen::core::Status close_view(ViewId id) noexcept;
    /// @brief Persisted definitions of every LIVE view.
    /// @return View definitions in creation order.
    [[nodiscard]] std::vector<ViewConfig> view_configs() const;
    /// @brief Clipping status recorded by the most recent successful clone.
    /// @return Empty when the clone copied everything requested.
    [[nodiscard]] const std::string& last_clone_status() const noexcept {
        return last_clone_status_;
    }
    /// @brief Limit future per-view index allocations.
    /// @param bytes Maximum bytes for one view index and its bitsets.
    void set_view_index_budget(std::size_t bytes) noexcept { view_index_budget_ = bytes; }
    /// @brief Current per-view index allocation limit.
    /// @return Byte limit.
    [[nodiscard]] std::size_t view_index_budget() const noexcept { return view_index_budget_; }
    /// @brief Bytes needed for one live view index and its bitsets.
    /// @param slots Addressable slots.
    /// @return Required bytes.
    [[nodiscard]] static std::size_t index_bytes_for(std::size_t slots) noexcept;

    /// @brief Focused view's filter index.
    /// @return Mutable reference.
    [[nodiscard]] FilterIndex& filter() noexcept { return active_view().filter(); }
    /// @brief Focused view's filter index.
    /// @return Const reference.
    [[nodiscard]] const FilterIndex& filter() const noexcept { return active_view().filter(); }
    /// @brief Focused view's find state.
    /// @return Mutable reference.
    [[nodiscard]] FindState& find() noexcept { return active_view().find(); }
    /// @brief Focused view's find state.
    /// @return Const reference.
    [[nodiscard]] const FindState& find() const noexcept { return active_view().find(); }
    /// @brief Focused view's time state.
    /// @return Mutable reference.
    [[nodiscard]] TimeContext& time() noexcept { return active_view().time(); }
    /// @brief Focused view's time state.
    /// @return Const reference.
    [[nodiscard]] const TimeContext& time() const noexcept { return active_view().time(); }

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

    /// @brief Start a chunked rebuild on the focused view.
    void begin_filter_rebuild() noexcept;
    /// @brief Start a chunked rebuild on one view.
    /// @param view Target view.
    void begin_filter_rebuild(LogView& view) noexcept;
    /// @brief Apply one bounded rebuild chunk to the focused view.
    /// @param max_records Records per frame.
    void step_filter_rebuild(std::size_t max_records) noexcept;
    /// @brief Apply a shared rebuild budget across every active view.
    /// @param max_records Total records per frame.
    void step_filter_rebuilds(std::size_t max_records) noexcept;
    /// @brief Focused view's rebuild progress.
    /// @return Progress snapshot.
    [[nodiscard]] FilterIndex::Progress filter_progress() const noexcept {
        return active_view().filter().progress();
    }

    /// @brief Start a chunked find on the focused view.
    /// @param query Substring; empty clears.
    void begin_find(std::string_view query) noexcept;
    /// @brief Start a chunked find on one view.
    /// @param view Target view.
    /// @param query Substring; empty clears.
    void begin_find(LogView& view, std::string_view query) noexcept;
    /// @brief Apply one bounded find chunk to the focused view.
    /// @param max_records Visible entries per frame.
    void step_find(std::size_t max_records) noexcept;
    /// @brief Apply a shared find budget across every active view.
    /// @param max_records Total visible entries per frame.
    void step_finds(std::size_t max_records) noexcept;
    /// @brief Select the next find match in the focused view.
    /// @return Absolute record number.
    std::optional<std::uint64_t> find_next() noexcept;
    /// @brief Select the previous find match in the focused view.
    /// @return Absolute record number.
    std::optional<std::uint64_t> find_previous() noexcept;

    /// @brief Pause or resume the focused view.
    /// @param paused New pause state.
    void set_paused(bool paused) noexcept;
    /// @brief Whether the focused view is paused.
    /// @return Pause state.
    [[nodiscard]] bool paused() const noexcept { return active_view().paused(); }
    /// @brief Focused view's new-record count while paused.
    /// @return Count.
    [[nodiscard]] std::uint64_t new_since_pause() const noexcept {
        return active_view().new_since_pause();
    }
    /// @brief Focused view's overwritten count while paused.
    /// @return Count.
    [[nodiscard]] std::uint64_t overwritten_while_paused() const noexcept {
        return active_view().overwritten_while_paused();
    }
    /// @brief Focused view's oldest displayed absolute number.
    /// @return View floor.
    [[nodiscard]] std::uint64_t view_floor() const noexcept { return active_view().view_floor(); }
    /// @brief Focused view's displayed end.
    /// @return View end.
    [[nodiscard]] std::uint64_t view_end() const noexcept { return active_view().view_end(); }
    /// @brief Focused view's displayed visible count.
    /// @return Displayed count.
    [[nodiscard]] std::size_t display_count() const noexcept {
        return active_view().display_count();
    }

    /// @brief Select only one record in the focused view.
    /// @param seq Absolute record number.
    void select_only(std::uint64_t seq) noexcept;
    /// @brief Toggle one record's selection in the focused view.
    /// @param seq Absolute record number.
    void toggle_selection(std::uint64_t seq) noexcept;
    /// @brief Select from the anchor to `seq` in the focused view.
    /// @param seq Absolute record number.
    void select_to(std::uint64_t seq) noexcept;
    /// @brief Select every visible record in the focused view.
    void select_all_visible() noexcept;
    /// @brief Clear the focused view's selection.
    void clear_selection() noexcept;
    /// @brief Focused view's last clicked record.
    /// @return Absolute number, or `std::nullopt`.
    [[nodiscard]] std::optional<std::uint64_t> last_selected() const noexcept {
        return active_view().last_selected();
    }
    /// @brief Drop the live history and every LIVE view's derived state.
    void clear_history() noexcept;
    /// @brief Reset every setting to its built-in default.
    void reset_configuration() noexcept;

    /// @brief Whether a restart is required to apply the persisted defaults.
    /// @return `true` when service, memory, or thread placement differ.
    [[nodiscard]] bool restart_required() const noexcept;
    /// @brief Service name the running capture uses.
    /// @return Borrowed runtime service name.
    [[nodiscard]] const std::string& runtime_service_name() const noexcept {
        return runtime_service_;
    }
    /// @brief Memory budget the running ring was sized from.
    /// @return Runtime memory budget in MiB.
    [[nodiscard]] std::uint32_t runtime_memory_mb() const noexcept { return runtime_memory_mb_; }
    /// @brief Focused view's shift-click anchor.
    /// @return Absolute number, or `std::nullopt`.
    [[nodiscard]] std::optional<std::uint64_t> selection_anchor() const noexcept {
        return active_view().selection_anchor();
    }

    /// @brief Toggle a bookmark in the focused view.
    /// @param seq Absolute record number.
    /// @return New bookmark state.
    bool toggle_bookmark(std::uint64_t seq) noexcept;
    /// @brief Next bookmarked record in the focused view.
    /// @param seq Current record, or `std::nullopt`.
    /// @return Absolute number, or `std::nullopt`.
    std::optional<std::uint64_t> next_bookmark(std::optional<std::uint64_t> seq) const noexcept;
    /// @brief Previous bookmarked record in the focused view.
    /// @param seq Current record, or `std::nullopt`.
    /// @return Absolute number, or `std::nullopt`.
    std::optional<std::uint64_t> previous_bookmark(std::optional<std::uint64_t> seq) const noexcept;
    /// @brief Next attention record in the focused view.
    /// @param seq Current record, or `std::nullopt`.
    /// @return Absolute number, or `std::nullopt`.
    std::optional<std::uint64_t> next_attention(std::optional<std::uint64_t> seq) const noexcept;
    /// @brief Previous attention record in the focused view.
    /// @param seq Current record, or `std::nullopt`.
    /// @return Absolute number, or `std::nullopt`.
    std::optional<std::uint64_t>
    previous_attention(std::optional<std::uint64_t> seq) const noexcept;

    /// @brief Counters for the toolbar.
    struct Counters {
        std::uint64_t received{0};  ///< Received frames.
        std::uint64_t synthetic{0}; ///< Gap/overflow records.
        std::uint64_t evicted{0};   ///< Evictions from the ring.
        std::uint64_t visible{0};   ///< Focused view's displayed visible rows.
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

    /// @brief Start an asynchronous export of the focused view's selection.
    /// @param path Destination file.
    /// @param format Output format.
    /// @return `Status{}` when the job started.
    [[nodiscard]] nexigen::core::Status start_export(const std::string& path,
                                                     ExportFormat format) noexcept;
    /// @brief Start an asynchronous export of one view's selection.
    /// @param view_id View whose selection is copied.
    /// @param path Destination file.
    /// @param format Output format.
    /// @return `Status{}` when the job started.
    [[nodiscard]] nexigen::core::Status start_export(ViewId view_id, const std::string& path,
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

    /// @brief Import a file into the shared history and rebuild every LIVE view.
    /// @param path File to read.
    /// @param stats Import counters.
    /// @return `Status{}` on success.
    [[nodiscard]] nexigen::core::Status import_now(const std::string& path,
                                                   ImportStats& stats) noexcept;
    /// @brief Start a chunked import.
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
    /// @return Text with ring, view, and filter counters.
    [[nodiscard]] std::string to_string() const;

private:
    /// @brief Construct a model from an allocated ring.
    LogViewModel(RecordRing ring, LogViewConfig config);

    /// @brief Focused view or a safe empty fallback.
    /// @return Borrowed view.
    [[nodiscard]] LogView& active_view() noexcept {
        return focused_view() ? *focused_view() : fallback_;
    }
    /// @brief Focused view or a safe empty fallback.
    /// @return Borrowed view.
    [[nodiscard]] const LogView& active_view() const noexcept {
        return focused_view() ? *focused_view() : fallback_;
    }
    /// @brief Resolve the focused id after a close.
    void choose_focus() noexcept;
    /// @brief Build a view from a persisted definition.
    /// @return New view id, or a limit failure.
    [[nodiscard]] nexigen::core::Result<ViewId> restore_view(const ViewConfig& definition) noexcept;
    /// @brief Build the default `All` view.
    /// @return New view id, or an allocation failure.
    [[nodiscard]] nexigen::core::Result<ViewId> create_default_all() noexcept;
    /// @brief Default filters and modes for a new view.
    /// @return Defaults derived from the persisted configuration.
    [[nodiscard]] ViewDefaults defaults_for(ViewKind kind, std::string_view source) const noexcept;
    /// @brief Apply pending hidden applications to a freshly observed source.
    void apply_pending_hidden(std::size_t slot) noexcept;
    /// @brief Internal frame ingestion after gap emission.
    void push_frame(const nexigen::logging::LogFrame& frame) noexcept;
    /// @brief Common gap ingestion.
    void push_gap(RecordKind kind, std::uint64_t count, std::uint64_t timestamp_ns) noexcept;
    /// @brief Notify every LIVE view of one eviction.
    void notify_eviction(std::uint64_t seq, const RecordView& view) noexcept;
    /// @brief Begin rebuilds on every LIVE view.
    void rebuild_live_views() noexcept;
    /// @brief Whether a view allocation fits the configured limit.
    /// @param slots Addressable slots.
    /// @return `true` when allowed.
    [[nodiscard]] bool allocation_allowed(std::size_t slots) const noexcept {
        return index_bytes_for(slots) <= view_index_budget_;
    }

    RecordRing ring_;                                      ///< Shared history.
    ColorManager colors_{};                                ///< Shared colors.
    TimeContext base_time_{};                              ///< Shared time base.
    std::string runtime_service_{};                        ///< Service active for this process.
    std::uint32_t runtime_memory_mb_{0};                   ///< Ring budget active for this process.
    nexigen::core::ThreadSpec runtime_capture_thread_{};   ///< Active capture placement.
    nexigen::core::ThreadSpec runtime_export_thread_{};    ///< Active export placement.
    LogViewConfig config_{};                               ///< Configuration.
    ResourceGuard guard_{};                                ///< ECO guard.
    ExportWriter export_{};                                ///< Export job.
    HistorySource export_history_{};                       ///< History the export reads.
    std::shared_ptr<const FrozenHistory> export_frozen_{}; ///< Pinned export snapshot.
    std::vector<std::uint64_t> export_seqs_{};             ///< Selection copied at export start.
    std::vector<std::unique_ptr<LogView>> views_{};        ///< Open views in creation order.
    LogView fallback_{};                                   ///< Safe empty view.
    ViewId focused_id_{0};                                 ///< Focused view identity.
    ViewId next_id_{1};                                    ///< Next view identity.
    std::string last_clone_status_{};                      ///< Last clone clipping summary.
    std::size_t view_index_budget_{static_cast<std::size_t>(-1)}; ///< Per-view index limit.
    std::vector<std::string> pending_hidden_{}; ///< Hidden sources not yet observed.

    std::uint64_t received_{0};             ///< Received frames.
    std::uint64_t synthetic_{0};            ///< Synthetic records.
    std::uint64_t rejected_{0};             ///< Rejected frames.
    std::uint64_t last_overflow_seen_{0};   ///< Last capture overflow counter.
    std::uint64_t ingested_since_tick_{0};  ///< Records since the last rate sample.
    std::uint64_t rate_window_start_ns_{0}; ///< Rate window start.
    double rate_{0.0};                      ///< Last measured rate.

    std::unique_ptr<ImportSession> import_session_{}; ///< Chunked import.
    ImportStats import_stats_{};                      ///< Import counters.
    bool importing_{false};                           ///< Chunked import running.

    bool exporting_{false};                  ///< Export job active.
    std::size_t export_cursor_{0};           ///< Next copied selection entry.
    std::uint64_t export_previous_stamp_{0}; ///< Previous visible timestamp.
    bool export_have_previous_{false};       ///< Whether the previous stamp is valid.
};

/// @brief Shared capture session; the historical name is kept for its callers.
using LogSession = LogViewModel;

/// @brief Free diagnostic mirror of `LogViewModel::to_string`.
/// @param model Model to render.
/// @return The same text.
[[nodiscard]] std::string to_string(const LogViewModel& model);

} // namespace nexigen::logview
