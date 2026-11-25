#include "can_view_model.hpp"

#include <format>
#include <utility>

#include <nexigen/core/detail/json_field.hpp>
#include <nexigen/core/enum_traits.hpp>

namespace nexigen::canview {

namespace {

template <class Enum>
bool read_required_enum(const nlohmann::json& object, const char* key, Enum& target) noexcept {
    const auto it = object.find(key);
    if (it == object.end() || !it->is_string()) {
        return false;
    }
    return from_string(it->get_ref<const nlohmann::json::string_t&>(), target);
}

ViewDefaults read_defaults(const nlohmann::json& object, ViewDefaults defaults) noexcept {
    core::mandate::read_enum(object, "table_mode", defaults.table_mode);
    core::mandate::read_enum(object, "timestamp_mode", defaults.timestamp_mode);
    core::mandate::read_enum(object, "delta_mode", defaults.delta_mode);
    core::mandate::read_enum(object, "time_columns", defaults.time_columns);
    static_cast<void>(core::detail::read_bool(object, "auto_scroll", defaults.auto_scroll));
    static_cast<void>(
        core::detail::read_signed(object, "anomaly_threshold_ns", defaults.anomaly_threshold_ns));
    return defaults;
}

void apply_settings(CanView& view, const nlohmann::json& object) noexcept {
    TableMode table = view.table_mode();
    core::mandate::read_enum(object, "table_mode", table);
    view.set_table_mode(table);
    TimestampMode timestamp = view.timestamp_mode();
    core::mandate::read_enum(object, "timestamp_mode", timestamp);
    view.set_timestamp_mode(timestamp);
    DeltaMode delta = view.delta_mode();
    core::mandate::read_enum(object, "delta_mode", delta);
    view.set_delta_mode(delta);
    TimeColumnMode columns = view.time_columns();
    core::mandate::read_enum(object, "time_columns", columns);
    view.set_time_columns(columns);
    bool auto_scroll = view.auto_scroll();
    if (core::detail::read_bool(object, "auto_scroll", auto_scroll)) {
        view.set_auto_scroll(auto_scroll);
    }
    std::int64_t threshold = view.anomaly_threshold_ns();
    if (core::detail::read_signed(object, "anomaly_threshold_ns", threshold)) {
        view.set_anomaly_threshold_ns(threshold);
    }
}

} // namespace

std::uint32_t CanViewModel::add_live(ViewKind kind, std::uint16_t channel_id, std::uint8_t node_id,
                                     std::optional<std::uint16_t> node_slot, std::string title,
                                     const ViewDefaults& defaults) {
    auto view = std::make_unique<CanView>();
    const std::uint32_t id = next_id_++;
    view->init_live(id, kind, channel_id, node_id, node_slot, std::move(title), source_,
                    ring_.capacity(), defaults);
    view->begin_rebuild();
    views_.push_back(std::move(view));
    return id;
}

nexigen::core::Result<std::uint32_t>
CanViewModel::create_view(ViewKind kind, std::uint16_t channel_id, std::uint8_t node_id,
                          std::string title, const ViewDefaults& defaults,
                          std::optional<std::uint16_t> node_slot) {
    if (live_count() >= max_live_views) {
        return std::unexpected(nexigen::core::ErrorCode::Conflict);
    }
    if (fail_view_) {
        return std::unexpected(nexigen::core::ErrorCode::ResourceExhausted);
    }
    std::optional<std::uint16_t> slot;
    if (kind == ViewKind::Node) {
        if (node_id == 0U || node_id > 127U) {
            return std::unexpected(nexigen::core::ErrorCode::InvalidArgument);
        }
        slot = node_slot;
        if (!slot.has_value() && resolver_ != nullptr) {
            slot = resolver_(channel_id, node_id, resolver_context_);
        }
    }
    return add_live(kind, channel_id, node_id, slot, std::move(title), defaults);
}

nexigen::core::Result<std::uint32_t>
CanViewModel::add_paused(const CanView& source_view, std::string title, const CloneOptions& options,
                         const ViewDefaults& defaults, CloneAdjustments* adjustments) {
    auto snapshot = std::make_unique<SnapshotSource>();
    snapshot->build(source_view.source(), snapshot_bytes);
    auto view = std::make_unique<CanView>();
    const std::uint32_t id = next_id_++;
    view->init_paused(id, source_view.kind(), source_view.channel_id(), source_view.node_id(),
                      source_view.node_slot(), std::move(title), std::move(snapshot),
                      ring_.capacity(), defaults);
    const CloneAdjustments applied = view->copy_state_from(source_view, options);
    if (adjustments != nullptr) {
        *adjustments = applied;
    }
    view->rebuild_all();
    views_.push_back(std::move(view));
    return id;
}

nexigen::core::Result<std::uint32_t> CanViewModel::clone_view(std::uint32_t source_id, bool paused,
                                                              std::string title,
                                                              const CloneOptions& options,
                                                              const ViewDefaults& defaults,
                                                              CloneAdjustments* adjustments) {
    const CanView* source_view = view(source_id);
    if (source_view == nullptr) {
        return std::unexpected(nexigen::core::ErrorCode::NotFound);
    }
    if (paused) {
        if (paused_count() >= max_paused_) {
            return std::unexpected(nexigen::core::ErrorCode::Conflict);
        }
        if (fail_snapshot_) {
            return std::unexpected(nexigen::core::ErrorCode::ResourceExhausted);
        }
        return add_paused(*source_view, std::move(title), options, defaults, adjustments);
    }
    if (live_count() >= max_live_views) {
        return std::unexpected(nexigen::core::ErrorCode::Conflict);
    }
    const std::uint32_t id =
        add_live(source_view->kind(), source_view->channel_id(), source_view->node_id(),
                 source_view->node_slot(), std::move(title), defaults);
    CanView* created = view(id);
    if (created == nullptr) {
        return std::unexpected(nexigen::core::ErrorCode::NotInitialized);
    }
    const CloneAdjustments applied = created->copy_state_from(*source_view, options);
    if (adjustments != nullptr) {
        *adjustments = applied;
    }
    created->rebuild_all();
    return id;
}

bool CanViewModel::remove_view(std::uint32_t id) {
    for (auto it = views_.begin(); it != views_.end(); ++it) {
        if ((*it)->id() == id) {
            if (focus_id_ == id) {
                focus_id_.reset();
            }
            views_.erase(it);
            return true;
        }
    }
    return false;
}

void CanViewModel::clear_views() {
    views_.clear();
    focus_id_.reset();
}

CanView* CanViewModel::view(std::uint32_t id) noexcept {
    for (auto& entry : views_) {
        if (entry->id() == id) {
            return entry.get();
        }
    }
    return nullptr;
}

const CanView* CanViewModel::view(std::uint32_t id) const noexcept {
    for (const auto& entry : views_) {
        if (entry->id() == id) {
            return entry.get();
        }
    }
    return nullptr;
}

std::size_t CanViewModel::live_count() const noexcept {
    std::size_t count = 0;
    for (const auto& entry : views_) {
        if (entry->live()) {
            ++count;
        }
    }
    return count;
}

std::size_t CanViewModel::paused_count() const noexcept {
    return views_.size() - live_count();
}

void CanViewModel::ingest(std::uint64_t seq) {
    for (auto& entry : views_) {
        if (entry->live() && !entry->orphaned()) {
            static_cast<void>(entry->append_if_visible(seq));
        }
    }
}

void CanViewModel::step_rebuilds(std::size_t max_records) {
    for (auto& entry : views_) {
        if (entry->index().rebuilding()) {
            static_cast<void>(entry->step_rebuild(max_records));
        }
    }
}

void CanViewModel::rebuild_all() {
    for (auto& entry : views_) {
        entry->rebuild_all();
    }
}

void CanViewModel::set_node_resolver(NodeResolver resolver, void* context) noexcept {
    resolver_ = resolver;
    resolver_context_ = context;
}

void CanViewModel::refresh_node_views() {
    if (resolver_ == nullptr) {
        return;
    }
    for (auto& entry : views_) {
        if (entry->kind() != ViewKind::Node) {
            continue;
        }
        const bool was_orphaned = entry->orphaned();
        const auto slot = resolver_(entry->channel_id(), entry->node_id(), resolver_context_);
        entry->set_node_binding(slot);
        if (slot.has_value() && was_orphaned) {
            entry->begin_rebuild();
        }
    }
}

void CanViewModel::focus(std::optional<std::uint32_t> id) noexcept {
    if (id.has_value() && view(*id) == nullptr) {
        return;
    }
    focus_id_ = id;
}

CanView* CanViewModel::focused() noexcept {
    return focus_id_.has_value() ? view(*focus_id_) : nullptr;
}

bool CanViewModel::set_focused_channel_visible(std::uint16_t channel_id, bool visible) noexcept {
    CanView* target = focused();
    if (target == nullptr) {
        return false;
    }
    const std::uint32_t bit = FilterCriteria::channel_bit(channel_id);
    if (visible) {
        target->filter().channel_mask |= bit;
    } else {
        target->filter().channel_mask &= ~bit;
    }
    target->begin_rebuild();
    return true;
}

bool CanViewModel::set_focused_node_visible(std::uint16_t slot, bool visible) noexcept {
    CanView* target = focused();
    if (target == nullptr || slot >= 128U) {
        return false;
    }
    target->filter().set_node_visible(slot, visible);
    target->filter().node_filter_active = true;
    target->begin_rebuild();
    return true;
}

nlohmann::json CanViewModel::to_json() const {
    nlohmann::json views = nlohmann::json::array();
    for (const auto& entry : views_) {
        if (!entry->live()) {
            continue;
        }
        views.push_back(nlohmann::json{{"kind", entry->kind()},
                                       {"channel_id", entry->channel_id()},
                                       {"node_id", entry->node_id()},
                                       {"title", entry->title()},
                                       {"table_mode", entry->table_mode()},
                                       {"timestamp_mode", entry->timestamp_mode()},
                                       {"delta_mode", entry->delta_mode()},
                                       {"time_columns", entry->time_columns()},
                                       {"auto_scroll", entry->auto_scroll()},
                                       {"anomaly_threshold_ns", entry->anomaly_threshold_ns()},
                                       {"filter", entry->filter()}});
    }
    return nlohmann::json{{"views", std::move(views)}};
}

CanViewModel::LoadReport CanViewModel::load_json(const nlohmann::json& document,
                                                 const ViewDefaults& defaults) {
    LoadReport report{};
    clear_views();
    if (!document.is_object()) {
        report.failed = 1U;
        return report;
    }
    const auto it = document.find("views");
    if (it == document.end() || !it->is_array()) {
        report.failed = 1U;
        return report;
    }
    for (const auto& entry : *it) {
        if (!entry.is_object()) {
            ++report.failed;
            continue;
        }
        ViewKind kind = ViewKind::All;
        if (!read_required_enum(entry, "kind", kind)) {
            ++report.failed;
            continue;
        }
        std::uint16_t channel_id = 0U;
        std::uint8_t node_id = 0U;
        std::string title;
        static_cast<void>(core::detail::read_unsigned(entry, "channel_id", channel_id));
        static_cast<void>(core::detail::read_unsigned(entry, "node_id", node_id));
        static_cast<void>(core::detail::read_string(entry, "title", title));
        const ViewDefaults view_defaults = read_defaults(entry, defaults);
        const auto created =
            create_view(kind, channel_id, node_id, std::move(title), view_defaults);
        if (!created.has_value()) {
            if (created.error() == nexigen::core::ErrorCode::Conflict) {
                ++report.skipped;
            } else {
                ++report.failed;
            }
            continue;
        }
        ++report.loaded;
        CanView* created_view = view(*created);
        if (created_view == nullptr) {
            continue;
        }
        const auto filter_it = entry.find("filter");
        if (filter_it != entry.end()) {
            from_json(*filter_it, created_view->filter());
        }
        apply_settings(*created_view, entry);
        created_view->begin_rebuild();
    }
    return report;
}

std::string CanViewModel::to_string() const {
    return std::format("CanViewModel(views={}, live={}, paused={}, focused={})", views_.size(),
                       live_count(), paused_count(),
                       focus_id_.has_value() ? static_cast<int>(*focus_id_) : -1);
}

} // namespace nexigen::canview
