#include "log_view_model.hpp"

#include <algorithm>
#include <format>

#include "time_formatter.hpp"

namespace nexigen::logview {

namespace {

std::size_t index_of_visible(const RecordRing& ring, std::uint64_t seq,
                             std::size_t limit) noexcept {
    for (std::size_t i = 0; i < limit; ++i) {
        if (ring.visible_at(i) == seq) {
            return i;
        }
    }
    return limit;
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
    return std::unique_ptr<LogViewModel>(new LogViewModel(std::move(*ring), std::move(config)));
}

LogViewModel::LogViewModel(RecordRing ring, LogViewConfig config)
    : ring_(std::move(ring)), colors_(config.colors), runtime_service_(config.service_name),
      runtime_memory_mb_(config.max_memory_mb), runtime_capture_thread_(config.capture_thread),
      runtime_export_thread_(config.export_thread), config_(std::move(config)) {
    time_.anomaly_threshold_ns = static_cast<std::int64_t>(config_.anomaly_threshold_ns);
    guard_ = ResourceGuard(ResourceGuardConfig{
        .cpu_limit_percent = config_.cpu_limit_percent,
        .entry_windows = 3,
        .exit_windows = 2,
        .exit_ratio = 0.7,
        .storm_rate = config_.storm_rate,
        .window_ns = 1'000'000'000ULL,
    });
    filter_.criteria().levels = config_.level_visible;
    filter_.criteria().text = config_.text_filter;
    pending_hidden_ = config_.hidden_apps;
    find_.init(ring_.capacity());
    if (!filter_.criteria().text.empty()) {
        begin_filter_rebuild();
    }
}

void LogViewModel::apply_pending_hidden(std::size_t slot) noexcept {
    if (slot >= AppRegistry::capacity || pending_hidden_.empty()) {
        return;
    }
    const std::string_view name = ring_.apps().at(slot).text();
    for (const std::string& hidden : pending_hidden_) {
        if (name == std::string_view(hidden).substr(0, 16)) {
            filter_.criteria().apps[slot] = false;
            filter_.criteria().app_filter_active = true;
            return;
        }
    }
}

void LogViewModel::push_frame(const nexigen::logging::LogFrame& frame) noexcept {
    const std::uint64_t seq = ring_.push_frame(frame);
    const std::size_t app_slot = ring_.apps().find(frame.app_id_text());
    apply_pending_hidden(app_slot);
    filter_.append_if_visible(ring_, seq);
    ++received_;
    ++ingested_since_tick_;
    if ((received_ & 0x3FFULL) == 0ULL && ring_.visible_count() > 0U &&
        ring_.stale_visible() * 4U > ring_.visible_count()) {
        static_cast<void>(ring_.compact_visible());
    }
    refresh_pause();
}

void LogViewModel::push_gap(RecordKind kind, std::uint64_t count,
                            std::uint64_t timestamp_ns) noexcept {
    const std::uint64_t seq = ring_.push_gap(kind, count, timestamp_ns);
    filter_.append_if_visible(ring_, seq);
    ++synthetic_;
    ++ingested_since_tick_;
    refresh_pause();
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
    filter_.begin_rebuild(ring_);
    find_.clear();
}

void LogViewModel::step_filter_rebuild(std::size_t max_records) noexcept {
    static_cast<void>(filter_.step_rebuild(ring_, max_records));
    if (!filter_.rebuilding()) {
        static_cast<void>(ring_.compact_visible());
        if (paused_) {
            pause_visible_count_ = ring_.visible_count();
        }
    }
}

void LogViewModel::begin_find(std::string_view query) noexcept {
    find_.begin(query, ring_);
}

void LogViewModel::step_find(std::size_t max_records) noexcept {
    static_cast<void>(find_.step(ring_, max_records));
}

std::optional<std::uint64_t> LogViewModel::find_next() noexcept {
    const auto seq = find_.next();
    if (seq.has_value()) {
        select_only(*seq);
    }
    return seq;
}

std::optional<std::uint64_t> LogViewModel::find_previous() noexcept {
    const auto seq = find_.previous();
    if (seq.has_value()) {
        select_only(*seq);
    }
    return seq;
}

void LogViewModel::set_paused(bool paused) noexcept {
    if (paused_ == paused) {
        return;
    }
    paused_ = paused;
    if (paused_) {
        pause_tail_ = ring_.tail();
        pause_floor_ = ring_.head();
        pause_overwritten_ = 0;
        pause_visible_count_ = ring_.visible_count();
    }
}

std::size_t LogViewModel::display_count() const noexcept {
    if (!paused_) {
        return ring_.visible_count();
    }
    if (view_end() <= view_floor()) {
        return 0U;
    }
    return std::min(pause_visible_count_, ring_.visible_count());
}

void LogViewModel::select_only(std::uint64_t seq) noexcept {
    ring_.clear_selection();
    if (ring_.set_selected(seq, true)) {
        selection_anchor_ = seq;
        last_selected_seq_ = seq;
    }
}

void LogViewModel::toggle_selection(std::uint64_t seq) noexcept {
    static_cast<void>(ring_.toggle_selected(seq));
    last_selected_seq_ = seq;
}

void LogViewModel::select_to(std::uint64_t seq) noexcept {
    if (!selection_anchor_.has_value()) {
        select_only(seq);
        return;
    }
    const std::size_t limit = display_count();
    const std::size_t first = index_of_visible(ring_, *selection_anchor_, limit);
    const std::size_t second = index_of_visible(ring_, seq, limit);
    if (first == limit || second == limit) {
        select_only(seq);
        return;
    }
    const std::size_t begin = std::min(first, second);
    const std::size_t end = std::max(first, second);
    for (std::size_t i = begin; i <= end; ++i) {
        static_cast<void>(ring_.set_selected(ring_.visible_at(i), true));
    }
    last_selected_seq_ = seq;
}

void LogViewModel::select_all_visible() noexcept {
    ring_.clear_selection();
    const std::size_t limit = display_count();
    for (std::size_t i = 0; i < limit; ++i) {
        static_cast<void>(ring_.set_selected(ring_.visible_at(i), true));
    }
}

void LogViewModel::clear_selection() noexcept {
    ring_.clear_selection();
    selection_anchor_.reset();
    last_selected_seq_.reset();
}

void LogViewModel::clear_history() noexcept {
    if (exporting_) {
        cancel_export();
    }
    import_session_.reset();
    importing_ = false;
    ring_.clear();
    filter_.clear(ring_);
    find_.clear();
    clear_selection();
    received_ = 0;
    synthetic_ = 0;
    rejected_ = 0;
    last_overflow_seen_ = 0;
    ingested_since_tick_ = 0;
    rate_ = 0.0;
    paused_ = false;
    pause_tail_ = 0;
    pause_floor_ = 0;
    pause_overwritten_ = 0;
    pause_visible_count_ = 0;
    import_stats_ = ImportStats{};
    time_.mixed_boot = false;
    time_.clear_anchor();
    rate_window_start_ns_ = 0;
}

bool LogViewModel::toggle_bookmark(std::uint64_t seq) noexcept {
    return ring_.toggle_bookmark(seq);
}

std::optional<std::uint64_t>
LogViewModel::next_bookmark(std::optional<std::uint64_t> seq) const noexcept {
    const std::size_t limit = display_count();
    if (limit == 0U) {
        return std::nullopt;
    }
    std::size_t start = 0;
    if (seq.has_value()) {
        const std::size_t position = index_of_visible(ring_, *seq, limit);
        start = position < limit ? position + 1U : 0U;
    }
    for (std::size_t step = 0; step < limit; ++step) {
        const std::size_t index = (start + step) % limit;
        if (ring_.is_bookmarked(ring_.visible_at(index))) {
            return ring_.visible_at(index);
        }
    }
    return std::nullopt;
}

std::optional<std::uint64_t>
LogViewModel::next_attention(std::optional<std::uint64_t> seq) const noexcept {
    const std::size_t limit = display_count();
    if (limit == 0U) {
        return std::nullopt;
    }
    std::size_t start = 0;
    if (seq.has_value()) {
        const std::size_t position = index_of_visible(ring_, *seq, limit);
        start = position < limit ? position + 1U : 0U;
    }
    for (std::size_t step = 0; step < limit; ++step) {
        const std::size_t index = (start + step) % limit;
        const auto view = ring_.find(ring_.visible_at(index));
        if (view.has_value() && view->is_frame() &&
            view->frame->log_level >= nexigen::core::LogLevel::Warning) {
            return view->seq;
        }
    }
    return std::nullopt;
}

std::optional<std::uint64_t>
LogViewModel::previous_attention(std::optional<std::uint64_t> seq) const noexcept {
    const std::size_t limit = display_count();
    if (limit == 0U) {
        return std::nullopt;
    }
    std::size_t start = limit - 1U;
    if (seq.has_value()) {
        const std::size_t position = index_of_visible(ring_, *seq, limit);
        start = position > 0U ? position - 1U : limit - 1U;
    }
    for (std::size_t step = 0; step < limit; ++step) {
        const std::size_t index = (start + limit - step) % limit;
        const auto view = ring_.find(ring_.visible_at(index));
        if (view.has_value() && view->is_frame() &&
            view->frame->log_level >= nexigen::core::LogLevel::Warning) {
            return view->seq;
        }
    }
    return std::nullopt;
}

std::optional<std::uint64_t>
LogViewModel::previous_bookmark(std::optional<std::uint64_t> seq) const noexcept {
    const std::size_t limit = display_count();
    if (limit == 0U) {
        return std::nullopt;
    }
    std::size_t start = limit - 1U;
    if (seq.has_value()) {
        const std::size_t position = index_of_visible(ring_, *seq, limit);
        start = position > 0U ? position - 1U : limit - 1U;
    }
    for (std::size_t step = 0; step < limit; ++step) {
        const std::size_t index = (start + limit - step) % limit;
        if (ring_.is_bookmarked(ring_.visible_at(index))) {
            return ring_.visible_at(index);
        }
    }
    return std::nullopt;
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
    const ResourceMode mode =
        guard_.observe(now_ns, process_cpu_ns, static_cast<std::uint64_t>(rate_));
    rate_ = guard_.rate_ewma();
    return mode;
}

nexigen::core::Status LogViewModel::start_export(const std::string& path,
                                                 ExportFormat format) noexcept {
    if (exporting_) {
        return std::unexpected(nexigen::core::ErrorCode::InvalidArgument);
    }
    const std::uint64_t total = ring_.selected_count();
    if (total == 0U) {
        return std::unexpected(nexigen::core::ErrorCode::InvalidArgument);
    }
    const auto started = export_.start(path, format, total, time_, config_.export_thread);
    if (!started) {
        return started;
    }
    exporting_ = true;
    export_cursor_ = ring_.head();
    export_target_tail_ = ring_.tail();
    export_previous_stamp_ = 0;
    export_have_previous_ = false;
    return {};
}

void LogViewModel::step_export(std::size_t max_records) noexcept {
    if (!exporting_) {
        return;
    }
    const std::size_t chunk = max_records == 0U ? 1U : max_records;
    std::size_t visited = 0;
    while (export_cursor_ < export_target_tail_ && visited < chunk) {
        const std::uint64_t seq = export_cursor_;
        const auto view = ring_.find(seq);
        if (!view.has_value()) {
            ++export_cursor_;
            ++visited;
            continue;
        }
        if (ring_.is_selected(seq)) {
            ExportRecord record{};
            record.frame = *view->frame;
            record.seq = seq;
            record.kind = view->kind;
            record.previous_app_ns = view->prev_same_app_ns;
            if (view->is_frame()) {
                if (export_have_previous_) {
                    record.previous_visible_ns =
                        static_cast<std::int64_t>(view->frame->timestamp_ns) -
                        static_cast<std::int64_t>(export_previous_stamp_);
                }
                export_previous_stamp_ = view->frame->timestamp_ns;
                export_have_previous_ = true;
            }
            if (!export_.try_enqueue(record)) {
                break;
            }
        }
        ++export_cursor_;
        ++visited;
    }
    if (export_cursor_ >= export_target_tail_) {
        static_cast<void>(export_.finish());
        exporting_ = false;
    }
}

void LogViewModel::cancel_export() noexcept {
    if (exporting_) {
        export_.cancel();
        static_cast<void>(export_.finish());
        exporting_ = false;
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
    time_.mixed_boot = stats.mixed_boot;
    if (stats.utc_offset_ns.has_value()) {
        time_.utc_offset_ns = *stats.utc_offset_ns;
    }
    begin_filter_rebuild();
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
        time_.mixed_boot = import_stats_.mixed_boot;
        if (import_stats_.utc_offset_ns.has_value()) {
            time_.utc_offset_ns = *import_stats_.utc_offset_ns;
        }
        import_session_.reset();
        importing_ = false;
        begin_filter_rebuild();
    }
}

double LogViewModel::import_progress() const noexcept {
    return import_session_ ? import_session_->ratio() : (importing_ ? 0.0 : 1.0);
}

void LogViewModel::reset_colors() noexcept {
    colors_.reset();
    ring_.apps().reset_palette();
}

void LogViewModel::refresh_pause() noexcept {
    if (!paused_) {
        return;
    }
    if (ring_.head() > pause_floor_) {
        pause_overwritten_ += ring_.head() - pause_floor_;
        pause_floor_ = ring_.head();
    }
}

void LogViewModel::reset_configuration() noexcept {
    config_ = LogViewConfig{};
    colors_.reset();
    pending_hidden_.clear();
    filter_.criteria() = FilterCriteria{};
    begin_filter_rebuild();
    find_.clear();
    time_.clear_anchor();
    time_.anomaly_threshold_ns = static_cast<std::int64_t>(config_.anomaly_threshold_ns);
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
    return std::format("LogViewModel({}, {}, received={}, synthetic={}, rate={:.0f})",
                       ring_.to_string(), filter_.to_string(), received_, synthetic_, rate_);
}

std::string to_string(const LogViewModel& model) {
    return model.to_string();
}

} // namespace nexigen::logview
