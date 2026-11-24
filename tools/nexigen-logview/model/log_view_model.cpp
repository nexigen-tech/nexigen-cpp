#include "log_view_model.hpp"

#include <algorithm>
#include <format>

#include "time_formatter.hpp"

namespace nexigen::logview {

namespace {

/// @brief Copy of a record about to be evicted by the ring.
struct Eviction {
    bool any{false};                    ///< Whether a record is being evicted.
    std::uint64_t seq{0};               ///< Evicted absolute number.
    RecordKind kind{RecordKind::Frame}; ///< Evicted kind.
    nexigen::logging::LogFrame frame{}; ///< Evicted frame copy.
    std::int64_t prev{0};               ///< Delta or gap count.
};

Eviction capture_eviction(const RecordRing& ring) noexcept {
    Eviction out{};
    if (ring.size() < ring.capacity() || ring.size() == 0U) {
        return out;
    }
    out.seq = ring.head();
    const auto view = ring.find(out.seq);
    if (!view.has_value()) {
        return out;
    }
    out.any = true;
    out.kind = view->kind;
    out.frame = *view->frame;
    out.prev =
        view->is_frame() ? view->prev_same_app_ns : static_cast<std::int64_t>(view->gap_count);
    return out;
}

RecordView eviction_view(const Eviction& eviction) noexcept {
    RecordView view{};
    view.seq = eviction.seq;
    view.kind = eviction.kind;
    view.frame = &eviction.frame;
    if (eviction.kind == RecordKind::Frame) {
        view.prev_same_app_ns = eviction.prev;
    } else {
        view.gap_count = static_cast<std::uint64_t>(eviction.prev);
    }
    return view;
}

} // namespace

nexigen::core::Result<std::unique_ptr<LogViewModel>>
LogViewModel::create(std::uint64_t budget_bytes, LogViewConfig config) {
    const auto validated = config.validate();
    if (!validated) {
        return std::unexpected(validated.error());
    }
    auto ring = RecordRing::create_for_budget(budget_bytes);
    if (!ring) {
        return std::unexpected(ring.error());
    }
    const bool expect_default_view = !config.views_saved;
    auto model = std::unique_ptr<LogViewModel>(new LogViewModel(std::move(*ring), config));
    if (expect_default_view && model->view_count() == 0U) {
        return std::unexpected(nexigen::core::ErrorCode::ResourceExhausted);
    }
    return model;
}

LogViewModel::LogViewModel(RecordRing ring, LogViewConfig config)
    : ring_(std::move(ring)), colors_(config.colors), runtime_service_(config.service_name),
      runtime_memory_mb_(config.max_memory_mb), runtime_capture_thread_(config.capture_thread),
      runtime_export_thread_(config.export_thread), config_(std::move(config)) {
    base_time_.anomaly_threshold_ns = static_cast<std::int64_t>(config_.anomaly_threshold_ns);
    guard_ = ResourceGuard(ResourceGuardConfig{
        .cpu_limit_percent = config_.cpu_limit_percent,
        .entry_windows = 3,
        .exit_windows = 2,
        .exit_ratio = 0.7,
        .storm_rate = config_.storm_rate,
        .window_ns = 1'000'000'000ULL,
    });
    pending_hidden_ = config_.hidden_apps;
    if (config_.views_saved) {
        for (const ViewConfig& definition : config_.views) {
            if (definition.live) {
                static_cast<void>(restore_view(definition));
            }
        }
    } else {
        static_cast<void>(create_default_all());
    }
    choose_focus();
}

LogView* LogViewModel::focused_view() noexcept {
    for (const auto& view : views_) {
        if (view->id() == focused_id_) {
            return view.get();
        }
    }
    return nullptr;
}

const LogView* LogViewModel::focused_view() const noexcept {
    for (const auto& view : views_) {
        if (view->id() == focused_id_) {
            return view.get();
        }
    }
    return nullptr;
}

void LogViewModel::focus_view(ViewId id) noexcept {
    if (view(id) != nullptr) {
        focused_id_ = id;
    }
}

LogView* LogViewModel::view(ViewId id) noexcept {
    for (const auto& view : views_) {
        if (view->id() == id) {
            return view.get();
        }
    }
    return nullptr;
}

const LogView* LogViewModel::view(ViewId id) const noexcept {
    for (const auto& view : views_) {
        if (view->id() == id) {
            return view.get();
        }
    }
    return nullptr;
}

LogView* LogViewModel::view_at(std::size_t index) noexcept {
    return index < views_.size() ? views_[index].get() : nullptr;
}

const LogView* LogViewModel::view_at(std::size_t index) const noexcept {
    return index < views_.size() ? views_[index].get() : nullptr;
}

std::size_t LogViewModel::live_view_count() const noexcept {
    return static_cast<std::size_t>(
        std::count_if(views_.begin(), views_.end(), [](const auto& view) { return view->live(); }));
}

std::size_t LogViewModel::paused_view_count() const noexcept {
    return views_.size() - live_view_count();
}

ViewDefaults LogViewModel::defaults_for(ViewKind kind, std::string_view source) const noexcept {
    ViewDefaults defaults{};
    defaults.criteria.levels = config_.level_visible;
    defaults.criteria.text = config_.text_filter;
    defaults.criteria.apps.fill(true);
    defaults.criteria.app_filter_active = false;
    if (kind == ViewKind::Source) {
        defaults.criteria.source = std::string(source.substr(0, 16));
    }
    defaults.timestamp_mode = config_.timestamp_mode;
    defaults.delta_mode = config_.delta_mode;
    defaults.anomaly_threshold_ns = config_.anomaly_threshold_ns;
    return defaults;
}

void LogViewModel::choose_focus() noexcept {
    if (view(focused_id_) != nullptr) {
        return;
    }
    focused_id_ = views_.empty() ? 0U : views_.front()->id();
}

nexigen::core::Result<ViewId> LogViewModel::create_default_all() noexcept {
    for (const auto& view : views_) {
        if (view->live() && view->kind() == ViewKind::All) {
            focused_id_ = view->id();
            return view->id();
        }
    }
    return create_view(ViewKind::All, {});
}

nexigen::core::Result<ViewId> LogViewModel::restore_view(const ViewConfig& definition) noexcept {
    if (live_view_count() >= kMaxLiveViews) {
        return std::unexpected(nexigen::core::ErrorCode::InvalidArgument);
    }
    if (!allocation_allowed(ring_.capacity())) {
        return std::unexpected(nexigen::core::ErrorCode::ResourceExhausted);
    }
    const ViewKind kind = definition.source.empty() ? ViewKind::All : ViewKind::Source;
    ViewDefaults defaults = defaults_for(kind, definition.source);
    defaults.criteria.levels = definition.levels;
    defaults.criteria.text = definition.text;
    defaults.timestamp_mode = definition.timestamp_mode;
    defaults.delta_mode = definition.delta_mode;
    defaults.anomaly_threshold_ns = definition.anomaly_threshold_ns;

    ViewId id = definition.id == 0U ? next_id_ : definition.id;
    if (id >= next_id_) {
        next_id_ = id + 1U;
    }
    const std::string title = definition.title.empty()
                                  ? (definition.source.empty() ? "All logs" : definition.source)
                                  : definition.title;
    auto view = std::make_unique<LogView>();
    view->init_live(id, kind, definition.source.substr(0, 16), title, ring_, ring_.apps(),
                    base_time_, defaults);
    const ViewId created = id;
    views_.push_back(std::move(view));
    focused_id_ = created;
    for (const std::string& hidden : definition.hidden_apps) {
        pending_hidden_.push_back(hidden);
    }
    for (std::size_t slot = 0; slot < AppRegistry::capacity; ++slot) {
        const AppSlot& app = ring_.apps().at(slot);
        if (app.used) {
            apply_pending_hidden(slot);
        }
    }
    return created;
}

nexigen::core::Result<ViewId> LogViewModel::create_view(ViewKind kind, std::string source) {
    if (kind == ViewKind::All) {
        for (const auto& view : views_) {
            if (view->live() && view->kind() == ViewKind::All) {
                focused_id_ = view->id();
                return view->id();
            }
        }
    }
    if (live_view_count() >= kMaxLiveViews) {
        return std::unexpected(nexigen::core::ErrorCode::InvalidArgument);
    }
    if (!allocation_allowed(ring_.capacity())) {
        return std::unexpected(nexigen::core::ErrorCode::ResourceExhausted);
    }
    if (kind == ViewKind::Source && source.empty()) {
        return std::unexpected(nexigen::core::ErrorCode::InvalidArgument);
    }
    if (source.size() > 16U) {
        source.resize(16);
    }
    const ViewId id = next_id_++;
    const std::string title = kind == ViewKind::All ? "All logs" : source;
    auto view = std::make_unique<LogView>();
    view->init_live(id, kind, source, title, ring_, ring_.apps(), base_time_,
                    defaults_for(kind, source));
    views_.push_back(std::move(view));
    focused_id_ = id;
    return id;
}

nexigen::core::Result<ViewId> LogViewModel::clone_view(ViewId source_id, CloneMode mode,
                                                       const CloneOptions& options) {
    last_clone_status_.clear();
    LogView* source = view(source_id);
    if (source == nullptr) {
        return std::unexpected(nexigen::core::ErrorCode::InvalidArgument);
    }
    if (mode == CloneMode::Live) {
        if (live_view_count() >= kMaxLiveViews) {
            return std::unexpected(nexigen::core::ErrorCode::InvalidArgument);
        }
        if (!allocation_allowed(ring_.capacity())) {
            return std::unexpected(nexigen::core::ErrorCode::ResourceExhausted);
        }
        const ViewId id = next_id_++;
        auto clone = std::make_unique<LogView>();
        clone->init_live(id, source->kind(), source->source(), source->title(), ring_, ring_.apps(),
                         base_time_, defaults_for(source->kind(), source->source()));
        last_clone_status_ = clone->copy_state_from(*source, options).to_string();
        views_.push_back(std::move(clone));
        focused_id_ = id;
        return id;
    }

    if (paused_view_count() >= kMaxPausedViews) {
        return std::unexpected(nexigen::core::ErrorCode::InvalidArgument);
    }
    std::shared_ptr<const FrozenHistory> frozen;
    if (!source->live() && source->frozen() != nullptr) {
        frozen = source->frozen();
    } else {
        frozen = FrozenHistory::capture(ring_, source->source(), source->kind() == ViewKind::All);
    }
    if (frozen == nullptr) {
        return std::unexpected(nexigen::core::ErrorCode::SystemError);
    }
    if (!allocation_allowed(frozen->size())) {
        return std::unexpected(nexigen::core::ErrorCode::ResourceExhausted);
    }
    const ViewId id = next_id_++;
    auto clone = std::make_unique<LogView>();
    clone->init_paused(id, source->kind(), source->source(), source->title(), std::move(frozen),
                       ring_.apps(), base_time_, defaults_for(source->kind(), source->source()));
    last_clone_status_ = clone->copy_state_from(*source, options).to_string();
    views_.push_back(std::move(clone));
    focused_id_ = id;
    return id;
}

nexigen::core::Status LogViewModel::close_view(ViewId id) noexcept {
    const auto it = std::find_if(views_.begin(), views_.end(),
                                 [id](const auto& view) { return view->id() == id; });
    if (it == views_.end()) {
        return std::unexpected(nexigen::core::ErrorCode::InvalidArgument);
    }
    views_.erase(it);
    choose_focus();
    return {};
}

std::size_t LogViewModel::index_bytes_for(std::size_t slots) noexcept {
    const std::size_t words = (slots + 63U) / 64U;
    return slots * sizeof(std::uint64_t) + 2U * words * sizeof(std::uint64_t);
}

std::vector<ViewConfig> LogViewModel::view_configs() const {
    std::vector<ViewConfig> out;
    for (const auto& view : views_) {
        if (!view->live()) {
            continue;
        }
        ViewConfig definition{};
        definition.id = view->id();
        definition.title = view->title();
        definition.source = view->source();
        definition.live = true;
        definition.levels = view->filter().criteria().levels;
        definition.text = view->filter().criteria().text;
        definition.timestamp_mode = view->timestamp_mode();
        definition.delta_mode = view->delta_mode();
        definition.anomaly_threshold_ns =
            static_cast<std::uint64_t>(view->time().anomaly_threshold_ns);
        if (view->kind() == ViewKind::All) {
            for (std::size_t slot = 0; slot < AppRegistry::capacity; ++slot) {
                const AppSlot& app = ring_.apps().at(slot);
                if (app.used && !view->filter().criteria().apps[slot]) {
                    definition.hidden_apps.emplace_back(app.text());
                }
            }
        }
        out.push_back(std::move(definition));
    }
    return out;
}

void LogViewModel::apply_pending_hidden(std::size_t slot) noexcept {
    if (slot >= AppRegistry::capacity || pending_hidden_.empty()) {
        return;
    }
    const std::string_view name = ring_.apps().at(slot).text();
    for (const std::string& hidden : pending_hidden_) {
        if (name == std::string_view(hidden).substr(0, 16)) {
            for (const auto& view : views_) {
                if (view->live() && view->kind() == ViewKind::All) {
                    view->filter().criteria().apps[slot] = false;
                    view->filter().criteria().app_filter_active = true;
                }
            }
            return;
        }
    }
}

void LogViewModel::notify_eviction(std::uint64_t seq, const RecordView& view) noexcept {
    for (const auto& candidate : views_) {
        if (candidate->live()) {
            candidate->on_evict(seq, view);
        }
    }
}

void LogViewModel::push_frame(const nexigen::logging::LogFrame& frame) noexcept {
    const Eviction eviction = capture_eviction(ring_);
    const std::uint64_t seq = ring_.push_frame(frame);
    const std::size_t app_slot = ring_.apps().find(frame.app_id_text());
    apply_pending_hidden(app_slot);
    if (eviction.any) {
        notify_eviction(eviction.seq, eviction_view(eviction));
    }
    for (const auto& view : views_) {
        if (view->live()) {
            view->note_ingest(seq);
            static_cast<void>(view->append_live(seq));
        }
    }
    ++received_;
    ++ingested_since_tick_;
    if ((received_ & 0x3FFULL) == 0ULL) {
        for (const auto& view : views_) {
            if (view->live() &&
                view->filter().stale(view->history()) * 4U > view->filter().visible_count()) {
                static_cast<void>(view->compact());
            }
        }
    }
}

void LogViewModel::push_gap(RecordKind kind, std::uint64_t count,
                            std::uint64_t timestamp_ns) noexcept {
    const Eviction eviction = capture_eviction(ring_);
    const std::uint64_t seq = ring_.push_gap(kind, count, timestamp_ns);
    if (eviction.any) {
        notify_eviction(eviction.seq, eviction_view(eviction));
    }
    for (const auto& view : views_) {
        if (view->live()) {
            view->note_ingest(seq);
            static_cast<void>(view->append_live(seq));
        }
    }
    ++synthetic_;
    ++ingested_since_tick_;
}

void LogViewModel::ingest_frame(const nexigen::logging::LogFrame& frame,
                                std::uint64_t lost_before) noexcept {
    if (lost_before > 0U) {
        push_gap(RecordKind::TransportGap, lost_before, frame.timestamp_ns);
    }
    if (frame.dropped_counter > 0U) {
        push_gap(RecordKind::PublisherDrop, frame.dropped_counter, frame.timestamp_ns);
    }
    push_frame(frame);
}

void LogViewModel::ingest_transport_gap(std::uint64_t count, std::uint64_t timestamp_ns) noexcept {
    if (count > 0U) {
        push_gap(RecordKind::TransportGap, count, timestamp_ns);
    }
}

void LogViewModel::ingest_gap(RecordKind kind, std::uint64_t count,
                              std::uint64_t timestamp_ns) noexcept {
    if (kind != RecordKind::Frame && count > 0U) {
        push_gap(kind, count, timestamp_ns);
    }
}

std::size_t LogViewModel::drain(nexigen::core::SpscRingBuffer<IngressRecord, 65536>& queue,
                                std::uint64_t overflow_total) noexcept {
    if (overflow_total > last_overflow_seen_) {
        const std::uint64_t dropped = overflow_total - last_overflow_seen_;
        last_overflow_seen_ = overflow_total;
        std::uint64_t stamp = 0;
        if (ring_.tail() > ring_.head()) {
            if (const auto last = ring_.find(ring_.tail() - 1U); last.has_value()) {
                stamp = last->frame->timestamp_ns;
            }
        }
        push_gap(RecordKind::IngressOverflow, dropped, stamp);
    }
    std::size_t ingested = 0;
    IngressRecord record;
    while (queue.try_pop(record)) {
        if (record.kind == RecordKind::Frame) {
            ingest_frame(record.frame, record.lost_before);
        } else {
            ingest_transport_gap(record.lost_before, record.frame.timestamp_ns);
        }
        ++ingested;
    }
    return ingested;
}

void LogViewModel::begin_filter_rebuild() noexcept {
    if (LogView* view = focused_view(); view != nullptr) {
        begin_filter_rebuild(*view);
    }
}

void LogViewModel::begin_filter_rebuild(LogView& view) noexcept {
    view.begin_rebuild();
}

void LogViewModel::step_filter_rebuild(std::size_t max_records) noexcept {
    if (LogView* view = focused_view(); view != nullptr) {
        view->step_rebuild(max_records);
    }
}

void LogViewModel::step_filter_rebuilds(std::size_t max_records) noexcept {
    std::size_t active = 0;
    for (const auto& view : views_) {
        active += view->rebuilding() ? 1U : 0U;
    }
    if (active == 0U) {
        return;
    }
    const std::size_t per_view = std::max<std::size_t>(1U, max_records / active);
    for (const auto& view : views_) {
        if (view->rebuilding()) {
            view->step_rebuild(per_view);
        }
    }
}

void LogViewModel::begin_find(std::string_view query) noexcept {
    if (LogView* view = focused_view(); view != nullptr) {
        begin_find(*view, query);
    }
}

void LogViewModel::begin_find(LogView& view, std::string_view query) noexcept {
    view.find().begin(query, view.filter());
}

void LogViewModel::step_find(std::size_t max_records) noexcept {
    if (LogView* view = focused_view(); view != nullptr) {
        static_cast<void>(view->find().step(view->filter(), view->history(), max_records));
    }
}

void LogViewModel::step_finds(std::size_t max_records) noexcept {
    std::size_t active = 0;
    for (const auto& view : views_) {
        active += view->find().active() ? 1U : 0U;
    }
    if (active == 0U) {
        return;
    }
    const std::size_t per_view = std::max<std::size_t>(1U, max_records / active);
    for (const auto& view : views_) {
        if (view->find().active()) {
            static_cast<void>(view->find().step(view->filter(), view->history(), per_view));
        }
    }
}

std::optional<std::uint64_t> LogViewModel::find_next() noexcept {
    if (LogView* view = focused_view(); view != nullptr) {
        const auto seq = view->find().next();
        if (seq.has_value()) {
            view->select_only(*seq);
        }
        return seq;
    }
    return std::nullopt;
}

std::optional<std::uint64_t> LogViewModel::find_previous() noexcept {
    if (LogView* view = focused_view(); view != nullptr) {
        const auto seq = view->find().previous();
        if (seq.has_value()) {
            view->select_only(*seq);
        }
        return seq;
    }
    return std::nullopt;
}

void LogViewModel::set_paused(bool paused) noexcept {
    if (LogView* view = focused_view(); view != nullptr) {
        view->set_paused(paused);
    }
}

void LogViewModel::select_only(std::uint64_t seq) noexcept {
    if (LogView* view = focused_view(); view != nullptr) {
        view->select_only(seq);
    }
}

void LogViewModel::toggle_selection(std::uint64_t seq) noexcept {
    if (LogView* view = focused_view(); view != nullptr) {
        view->toggle_selection(seq);
    }
}

void LogViewModel::select_to(std::uint64_t seq) noexcept {
    if (LogView* view = focused_view(); view != nullptr) {
        view->select_to(seq);
    }
}

void LogViewModel::select_all_visible() noexcept {
    if (LogView* view = focused_view(); view != nullptr) {
        view->select_all_visible();
    }
}

void LogViewModel::clear_selection() noexcept {
    if (LogView* view = focused_view(); view != nullptr) {
        view->clear_selection();
    }
}

void LogViewModel::clear_history() noexcept {
    if (exporting_) {
        cancel_export();
    }
    import_session_.reset();
    importing_ = false;
    ring_.clear();
    for (const auto& view : views_) {
        if (view->live()) {
            view->reset_after_clear();
        }
    }
    received_ = 0;
    synthetic_ = 0;
    rejected_ = 0;
    last_overflow_seen_ = 0;
    ingested_since_tick_ = 0;
    rate_ = 0.0;
    import_stats_ = ImportStats{};
    base_time_.mixed_boot = false;
    base_time_.clear_anchor();
    rate_window_start_ns_ = 0;
}

bool LogViewModel::toggle_bookmark(std::uint64_t seq) noexcept {
    if (LogView* view = focused_view(); view != nullptr) {
        return view->toggle_bookmark(seq);
    }
    return false;
}

std::optional<std::uint64_t>
LogViewModel::next_bookmark(std::optional<std::uint64_t> seq) const noexcept {
    const LogView* view = focused_view();
    return view != nullptr ? view->next_bookmark(seq) : std::nullopt;
}

std::optional<std::uint64_t>
LogViewModel::previous_bookmark(std::optional<std::uint64_t> seq) const noexcept {
    const LogView* view = focused_view();
    return view != nullptr ? view->previous_bookmark(seq) : std::nullopt;
}

std::optional<std::uint64_t>
LogViewModel::next_attention(std::optional<std::uint64_t> seq) const noexcept {
    const LogView* view = focused_view();
    return view != nullptr ? view->next_attention(seq) : std::nullopt;
}

std::optional<std::uint64_t>
LogViewModel::previous_attention(std::optional<std::uint64_t> seq) const noexcept {
    const LogView* view = focused_view();
    return view != nullptr ? view->previous_attention(seq) : std::nullopt;
}

LogViewModel::Counters LogViewModel::counters() const noexcept {
    Counters out{};
    out.received = received_;
    out.synthetic = synthetic_;
    out.evicted = ring_.evictions();
    out.visible = display_count();
    out.total = ring_.size();
    out.rejected = rejected_;
    return out;
}

ResourceMode LogViewModel::tick(std::uint64_t now_ns, std::uint64_t process_cpu_ns) noexcept {
    if (rate_window_start_ns_ == 0U) {
        rate_window_start_ns_ = now_ns;
    }
    const std::uint64_t elapsed = now_ns - rate_window_start_ns_;
    if (elapsed >= 1'000'000'000ULL) {
        rate_ = static_cast<double>(ingested_since_tick_) * 1'000'000'000.0 /
                static_cast<double>(elapsed);
        rate_window_start_ns_ = now_ns;
        ingested_since_tick_ = 0;
    }
    for (const auto& view : views_) {
        view->sync_time_base(base_time_);
    }
    const ResourceMode mode =
        guard_.observe(now_ns, process_cpu_ns, static_cast<std::uint64_t>(rate_));
    rate_ = guard_.rate_ewma();
    return mode;
}

nexigen::core::Status LogViewModel::start_export(const std::string& path,
                                                 ExportFormat format) noexcept {
    return start_export(focused_id_, path, format);
}

nexigen::core::Status LogViewModel::start_export(ViewId view_id, const std::string& path,
                                                 ExportFormat format) noexcept {
    if (exporting_) {
        return std::unexpected(nexigen::core::ErrorCode::InvalidArgument);
    }
    LogView* source = view(view_id);
    if (source == nullptr) {
        return std::unexpected(nexigen::core::ErrorCode::InvalidArgument);
    }
    export_seqs_.clear();
    for (std::size_t i = 0; i < source->history().size(); ++i) {
        const std::uint64_t seq = source->history().seq_at(i);
        if (source->is_selected(seq)) {
            export_seqs_.push_back(seq);
        }
    }
    if (export_seqs_.empty()) {
        return std::unexpected(nexigen::core::ErrorCode::InvalidArgument);
    }
    const auto started =
        export_.start(path, format, export_seqs_.size(), source->time(), config_.export_thread);
    if (!started) {
        return started;
    }
    exporting_ = true;
    export_cursor_ = 0;
    export_previous_stamp_ = 0;
    export_have_previous_ = false;
    export_history_ = source->history();
    export_frozen_ = source->frozen();
    return {};
}

void LogViewModel::step_export(std::size_t max_records) noexcept {
    if (!exporting_) {
        return;
    }
    const std::size_t chunk = max_records == 0U ? 1U : max_records;
    std::size_t visited = 0;
    while (export_cursor_ < export_seqs_.size() && visited < chunk) {
        const std::uint64_t seq = export_seqs_[export_cursor_];
        const auto view = export_history_.find(seq);
        if (!view.has_value()) {
            export_.record_missing();
            ++export_cursor_;
            ++visited;
            continue;
        }
        ExportRecord record{};
        record.frame = *view->frame;
        record.seq = seq;
        record.kind = view->kind;
        record.previous_app_ns = view->prev_same_app_ns;
        if (view->is_frame()) {
            if (export_have_previous_) {
                record.previous_visible_ns = static_cast<std::int64_t>(view->frame->timestamp_ns) -
                                             static_cast<std::int64_t>(export_previous_stamp_);
            }
            export_previous_stamp_ = view->frame->timestamp_ns;
            export_have_previous_ = true;
        }
        if (!export_.try_enqueue(record)) {
            break;
        }
        ++export_cursor_;
        ++visited;
    }
    if (export_cursor_ >= export_seqs_.size()) {
        static_cast<void>(export_.finish());
        exporting_ = false;
        export_seqs_.clear();
        export_history_ = HistorySource{};
        export_frozen_.reset();
    }
}

void LogViewModel::cancel_export() noexcept {
    if (exporting_) {
        export_.cancel();
        static_cast<void>(export_.finish());
        exporting_ = false;
        export_seqs_.clear();
        export_history_ = HistorySource{};
        export_frozen_.reset();
    }
}

ExportStats LogViewModel::export_stats() const noexcept {
    return export_.stats();
}

nexigen::core::Status LogViewModel::import_now(const std::string& path,
                                               ImportStats& stats) noexcept {
    const auto status = import_file(path, ring_, stats);
    if (!status) {
        return status;
    }
    base_time_.mixed_boot = stats.mixed_boot;
    if (stats.utc_offset_ns.has_value()) {
        base_time_.utc_offset_ns = *stats.utc_offset_ns;
    }
    rebuild_live_views();
    return {};
}

nexigen::core::Status LogViewModel::begin_import(const std::string& path,
                                                 ImportStats& stats) noexcept {
    if (importing_) {
        return std::unexpected(nexigen::core::ErrorCode::InvalidArgument);
    }
    auto session = ImportSession::open(path);
    if (!session) {
        return std::unexpected(session.error());
    }
    import_session_ = std::move(*session);
    import_stats_ = ImportStats{};
    stats = ImportStats{};
    importing_ = true;
    return {};
}

void LogViewModel::step_import(std::size_t max_lines) noexcept {
    if (!importing_ || !import_session_) {
        return;
    }
    if (import_session_->step(ring_, import_stats_, max_lines)) {
        base_time_.mixed_boot = import_stats_.mixed_boot;
        if (import_stats_.utc_offset_ns.has_value()) {
            base_time_.utc_offset_ns = *import_stats_.utc_offset_ns;
        }
        import_session_.reset();
        importing_ = false;
        rebuild_live_views();
    }
}

double LogViewModel::import_progress() const noexcept {
    return import_session_ ? import_session_->ratio() : (importing_ ? 0.0 : 1.0);
}

void LogViewModel::rebuild_live_views() noexcept {
    for (const auto& view : views_) {
        if (view->live()) {
            view->begin_rebuild();
        }
    }
}

void LogViewModel::reset_colors() noexcept {
    colors_.reset();
    ring_.apps().reset_palette();
}

void LogViewModel::reset_configuration() noexcept {
    config_ = LogViewConfig{};
    colors_.reset();
    pending_hidden_.clear();
    base_time_.clear_anchor();
    base_time_.anomaly_threshold_ns = static_cast<std::int64_t>(config_.anomaly_threshold_ns);
    for (const auto& view : views_) {
        view->reset_to_defaults(defaults_for(view->kind(), view->source()));
    }
    rate_ = 0.0;
    ingested_since_tick_ = 0;
    rate_window_start_ns_ = 0;
    guard_ = ResourceGuard(ResourceGuardConfig{
        .cpu_limit_percent = config_.cpu_limit_percent,
        .entry_windows = 3,
        .exit_windows = 2,
        .exit_ratio = 0.7,
        .storm_rate = config_.storm_rate,
        .window_ns = 1'000'000'000ULL,
    });
}

bool LogViewModel::restart_required() const noexcept {
    return config_.service_name != runtime_service_ ||
           config_.max_memory_mb != runtime_memory_mb_ ||
           config_.capture_thread != runtime_capture_thread_ ||
           config_.export_thread != runtime_export_thread_;
}

std::string LogViewModel::to_string() const {
    return std::format("LogViewModel({}, views={}, focused={}, received={}, synthetic={}, "
                       "rate={:.0f})",
                       ring_.to_string(), views_.size(), focused_id_, received_, synthetic_, rate_);
}

std::string to_string(const LogViewModel& model) {
    return model.to_string();
}

} // namespace nexigen::logview
