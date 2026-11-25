#include "canview_window.hpp"

#include <algorithm>
#include <array>
#include <charconv>
#include <atomic>
#include <chrono>
#include <csignal>
#include <cstdio>
#include <cstring>
#include <ctime>
#include <filesystem>
#include <future>
#include <limits>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

#include <GL/gl.h>

#include <GLFW/glfw3.h>
#include <imgui.h>
#include <imgui_impl_glfw.h>
#include <imgui_impl_opengl3.h>
#include <imgui_internal.h>
#include <implot.h>

#include "jetbrains_mono_asset.hpp"
#include "nexigen_splash_asset.hpp"

#include "backend/capture_thread.hpp"
#include "budget.hpp"
#include "can_record_ring.hpp"
#include "can_view_model.hpp"
#include "bit_activity.hpp"
#include <nexigen/chrono/time_base_clock.hpp>

#include "canopen_decoder.hpp"
#include "canopen_reindex.hpp"
#include "canopen_registry.hpp"
#include "cob_id_router.hpp"
#include "eds_library.hpp"
#include "validate_for_view.hpp"
#include "canview_config.hpp"
#include "heatmap.hpp"
#include "time_presenter.hpp"
#include "record_meta.hpp"
#include "resource_guard.hpp"
#include "session_replay.hpp"
#include "session_export.hpp"
#include "session_import.hpp"
#include "session_replay.hpp"
#include "splash_image.hpp"
#include "status_snapshot.hpp"
#include "theme.hpp"

namespace nexigen::canview {

namespace {

std::atomic<bool> g_stop{false};

void on_signal(int) {
    g_stop.store(true, std::memory_order_relaxed);
}

std::uint64_t process_cpu_ns() {
    timespec value{};
    if (::clock_gettime(CLOCK_PROCESS_CPUTIME_ID, &value) != 0) {
        return 0U;
    }
    return static_cast<std::uint64_t>(value.tv_sec) * 1'000'000'000ULL +
           static_cast<std::uint64_t>(value.tv_nsec);
}

std::uint64_t monotonic_ns() {
    return static_cast<std::uint64_t>(std::chrono::duration_cast<std::chrono::nanoseconds>(
                                          std::chrono::steady_clock::now().time_since_epoch())
                                          .count());
}

const char* channel_name(const std::vector<CanViewChannel>& channels, std::uint16_t channel_id) {
    for (const auto& channel : channels) {
        if (channel.channel_id == channel_id) {
            return channel.name.c_str();
        }
    }
    return "?";
}

struct UiState {
    struct LiveStatus {
        bool seen{false};                            ///< Whether a status sample arrived.
        nexigen::hw::can::CanChannelStatus status{}; ///< Latest driver snapshot.
    };

    std::unique_ptr<CanRecordRing> ring{};
    std::unique_ptr<RingRecordSource> source{};
    std::unique_ptr<CanViewModel> model{};
    std::unique_ptr<CaptureThread> capture{};
    OverwriteTable overwrite{};
    HeatmapWindow heatmap{};
    BitActivity bit_activity{};
    TimeContext time{};
    std::string status{"ready"};
    std::uint64_t last_heatmap_ns{0};
    std::uint16_t heatmap_channel{1};
    bool dock_built{false};
    bool splash_done{false};
    bool clone_modal{false};
    bool clone_paused{false};
    CloneOptions clone_options{};
    bool show_configure{false};
    ResourceGuard guard{};
    std::uint64_t last_cpu_ns{0};
    std::uint64_t last_rate_count{0};
    std::uint64_t last_rate_time{0};
    MemoryPlan plan{};
    std::uint64_t rejected{0};
    std::unique_ptr<EdsLibrary> eds_library{};
    std::unique_ptr<CanOpenRegistry> registry{};
    std::unique_ptr<CobIdRouter> router{};
    std::unique_ptr<CanOpenDecoder> decoder{};
    std::uint32_t filter_synced_id{0};
    char filter_text[96]{};
    char filter_id[24]{};
    char filter_mask[24]{"7ff"};
    bool filter_errors{false};
    bool filter_idle{false};
    bool filter_gaps{true};
    std::uint8_t filter_origin_mask{0x07};
    char find_text[96]{};
    std::vector<OverwriteKey> overwrite_matches{};
    std::size_t overwrite_cursor{0};
    std::optional<OverwriteKey> overwrite_anchor{};
    std::unique_ptr<AsyncExportWriter> export_writer{};
    std::vector<std::uint64_t> export_seqs{};
    std::size_t export_cursor{0};
    std::uint64_t export_skipped{0};
    std::uint64_t export_total{0};
    std::uint64_t export_previous_hw{0};
    std::uint64_t export_local_seq{0};
    std::uint16_t export_previous_channel{0};
    bool export_has_previous{false};
    std::unique_ptr<CanOpenRegistry> export_registry{};
    std::unique_ptr<CobIdRouter> export_router{};
    std::unique_ptr<CanOpenDecoder> export_decoder{};
    char export_path[256]{"/tmp/canview-export.jsonl"};
    int export_format{0};
    std::uint64_t boot_token{1};
    std::unique_ptr<CanOpenReindexer> reindexer{};
    char eds_channel[16]{};
    char eds_node[16]{};
    char eds_path[256]{};
    bool focus_find{false};
    std::unique_ptr<OfflineCanopen> offline{};
    std::optional<ImportResult> import_result{};
    std::unordered_map<std::uint64_t, std::size_t> offline_index{};
    char import_path[256]{};
    std::future<ImportResult> import_future{};
    std::atomic<bool> import_cancel{false};
    bool import_running{false};
    bool show_import{false};
    std::string import_banner{};
    bool import_banner_warning{false};
    std::vector<std::string> eds_diagnostics{};
    std::vector<CanViewChannel> channels{};
    std::array<LiveStatus, 32> live_status{};
    bool bind_confirm{false};
    std::uint16_t bind_channel{0};
    std::uint8_t bind_node{0};
    std::string bind_path{};
    bool shared_time_warning{false};
    std::string clone_summary{};
};

void check_shared_timebase(UiState& state);

void push_marker(UiState& state, const CanIngressRecord& ingress) {
    const std::uint8_t meta =
        pack_gap_meta(ingress.kind, ingress.gap_reason(), ingress.gap_count_exact());
    const std::uint64_t seq =
        state.ring->push(ingress.msg, static_cast<std::int64_t>(ingress.lost_before),
                         CanRecordView::no_node, meta, ingress.time_epoch, ingress.co_map_epoch);
    state.model->ingest(seq);
}

void push_overflow(UiState& state, std::uint64_t count) {
    if (count == 0U) {
        return;
    }
    CanIngressRecord marker{};
    marker.kind = CanRecordKind::IngressOverflow;
    marker.lost_before = count;
    marker.flags = CanIngressRecord::pack_gap_flags(GapReason::IngressOverflow, false);
    marker.msg.hardware_timestamp_ns = monotonic_ns();
    push_marker(state, marker);
}

std::string flags_text(std::uint8_t meta) {
    std::string out;
    if (has_flag(meta, RecordFlags::InvalidStruct)) {
        out += "invalid ";
    }
    if (has_flag(meta, RecordFlags::TimestampInvalid)) {
        out += "no-ts ";
    }
    if (has_flag(meta, RecordFlags::DuplicateReorder)) {
        out += "dup ";
    }
    if (has_flag(meta, RecordFlags::SequenceReset)) {
        out += "reset ";
    }
    return out.empty() ? "-" : out;
}

void ingest_frame(UiState& state, const CanIngressRecord& record, std::uint64_t now_ns) {
    const ViewValidation validation = validate_for_view(record.msg);
    auto flags = static_cast<RecordFlags>(record.flags);
    if (!validation.valid) {
        flags = flags | RecordFlags::InvalidStruct;
        ++state.rejected;
    }
    if (!validation.timestamp_usable) {
        flags = flags | RecordFlags::TimestampInvalid;
    }
    const std::uint8_t meta = record.kind == CanRecordKind::BusIdleHeartbeat
                                  ? pack_frame_meta(CanRecordKind::BusIdleHeartbeat, flags)
                                  : pack_frame_meta(CanRecordKind::Frame, flags);
    std::uint16_t node_index = CanRecordView::no_node;
    if (state.router != nullptr && !record.msg.extended) {
        if (const auto route =
                state.router->lookup(record.msg.channel_id, record.msg.can_id, false);
            route.has_value()) {
            node_index = route->slot;
        }
    }
    const std::uint64_t seq =
        state.ring->push(record.msg, 0, node_index, meta, record.time_epoch, record.co_map_epoch);
    if (const auto view = state.ring->find(seq); view.has_value()) {
        state.time.observe(*view);
        state.model->ingest(seq);
        if (view->is_frame()) {
            static_cast<void>(state.overwrite.update(*view, now_ns));
        }
        check_shared_timebase(state);
    }
}

void ingest_ingress(UiState& state, std::uint64_t now_ns) {
    if (state.capture == nullptr) {
        return;
    }
    AnchorEvent anchor{};
    while (state.capture->anchors().try_pop(anchor)) {
        state.time.anchors.upsert(anchor);
    }
    StatusEvent status{};
    while (state.capture->statuses().try_pop(status)) {
        state.live_status[status.channel_id % state.live_status.size()] =
            UiState::LiveStatus{true, status.status};
    }
    CanIngressRecord record{};
    while (state.capture->ingress().try_pop(record)) {
        if (record.kind == CanRecordKind::TransportGap ||
            record.kind == CanRecordKind::IngressOverflow ||
            record.kind == CanRecordKind::DriverDrop) {
            push_marker(state, record);
            continue;
        }
        if (record.lost_before > 0U) {
            CanIngressRecord gap = record;
            gap.kind = CanRecordKind::TransportGap;
            gap.flags = CanIngressRecord::pack_gap_flags(GapReason::HardwareSequence, true);
            gap.lost_before = record.lost_before;
            push_marker(state, gap);
        }
        ingest_frame(state, record, now_ns);
    }
    push_overflow(state, state.capture->take_pending_overflow());
}

void generate_mock(UiState& state, const CanViewConfig& /*config*/, std::uint64_t count) {
    for (std::uint64_t index = 0U; index < count; ++index) {
        nexigen::hw::can::CanMessage message{};
        message.channel_id = state.channels.empty()
                                 ? static_cast<std::uint16_t>(1U + index % 2U)
                                 : state.channels[index % state.channels.size()].channel_id;
        message.can_id = static_cast<std::uint32_t>(0x100U + index % 8U);
        message.dlc = 8U;
        message.hardware_timestamp_ns = 1'000'000'000ULL + index * 1'000'000ULL;
        message.hardware_sequence = index;
        message.frame_flags.bits = nexigen::hw::can::CanFrameFlags::has_hardware_timestamp |
                                   nexigen::hw::can::CanFrameFlags::has_hardware_sequence;
        for (std::size_t byte = 0U; byte < message.data.size(); ++byte) {
            message.data[byte] = static_cast<std::uint8_t>((index + byte) & 0xFFU);
        }
        const std::uint8_t meta = pack_frame_meta(CanRecordKind::Frame);
        const std::uint64_t seq = state.ring->push(message, static_cast<std::int64_t>(index),
                                                   CanRecordView::no_node, meta, 0U, 0U);
        if (const auto view = state.ring->find(seq); view.has_value()) {
            state.time.observe(*view);
            state.model->ingest(seq);
            static_cast<void>(state.overwrite.update(*view, monotonic_ns()));
            check_shared_timebase(state);
        }
    }
}

void initialize_views(UiState& state, const CanViewConfig& config) {
    const std::uint32_t count = config.view_count == 0U ? 1U : config.view_count;
    for (std::uint32_t index = 0U; index < count; ++index) {
        const auto id = state.model->create_view(
            ViewKind::All, 0U, 0U, index == 0U ? "All channels" : "All " + std::to_string(index),
            config.defaults);
        if (id.has_value()) {
            if (CanView* view = state.model->view(*id); view != nullptr) {
                view->filter() = config.filter;
                view->begin_rebuild();
            }
        }
    }
    if (const auto& views = state.model->views(); !views.empty()) {
        state.model->focus(views.front()->id());
    }
    state.model->rebuild_all();
}

std::optional<std::uint16_t> resolve_binding(std::uint16_t channel_id, std::uint8_t node_id,
                                             void* context) {
    auto* registry = static_cast<CanOpenRegistry*>(context);
    if (registry == nullptr) {
        return std::nullopt;
    }
    const NodeBinding* binding = registry->find(channel_id, node_id);
    return binding != nullptr ? std::optional<std::uint16_t>(binding->registry_slot) : std::nullopt;
}

void refresh_registry_views(UiState& state) {
    if (state.model == nullptr || state.registry == nullptr) {
        return;
    }
    state.model->set_node_resolver(&resolve_binding, state.registry.get());
    state.model->refresh_node_views();
}

void initialize_canopen(UiState& state, const CanViewConfig& config) {
    state.registry = std::make_unique<CanOpenRegistry>();
    state.router = std::make_unique<CobIdRouter>();
    state.eds_library = std::make_unique<EdsLibrary>();
    if (state.plan.eds_enabled) {
        for (const SessionEdsBinding& binding_source : config.eds) {
            auto loaded = state.eds_library->load(binding_source.path);
            const auto bound = state.registry->bind(
                binding_source.channel_id, binding_source.node_id, binding_source.alias,
                binding_source.colour, loaded.dictionary, binding_source.path, true);
            if (!bound.has_value() && state.status == "ready") {
                state.status = "EDS binding rejected";
            }
        }
    }
    state.router->rebuild(*state.registry);
    state.decoder = std::make_unique<CanOpenDecoder>(*state.registry, *state.router);
    state.reindexer = std::make_unique<CanOpenReindexer>();
    refresh_registry_views(state);
}

void merge_import_channels(UiState& state, const ImportResult& imported);
void observe_time(UiState& state);

bool load_offline(UiState& state, const std::string& path) {
    auto imported = SessionImporter::load(path);
    if (imported.unsupported_format) {
        state.status = "CSV is not a session format";
        return false;
    }
    if (imported.malformed > 0U && imported.records.empty()) {
        state.status = "cannot open offline session";
        return false;
    }
    if (imported.mixed_boot) {
        state.status = "warning: mixed boot sessions; deltas are not comparable";
    }
    merge_import_channels(state, imported);
    static_cast<void>(replay_into_ring(imported, *state.ring));
    observe_time(state);
    std::uint64_t record_seq = 0U;
    for (std::size_t index = 0; index < imported.records.size(); ++index) {
        const auto type = imported.records[index].type;
        if (type == CanViewRecordType::Frame || type == CanViewRecordType::Gap) {
            state.offline_index[record_seq++] = index;
        }
    }
    state.import_result = std::move(imported);
    return true;
}

void start_capture(UiState& state, const CanViewConfig& /*config*/) {
    std::vector<CanViewChannel> enabled;
    for (const auto& channel : state.channels) {
        if (channel.enabled) {
            enabled.push_back(channel);
        }
    }
    if (enabled.empty()) {
        return;
    }
    state.capture = std::make_unique<CaptureThread>(enabled);
    if (!state.capture->start()) {
        state.status = "capture unavailable: " + state.capture->last_failure();
    }
}

void restore_views(UiState& state, const CanViewConfig& config) {
    if (config.views.is_object() && config.views.contains("views") &&
        config.views["views"].is_array() && !config.views["views"].empty()) {
        static_cast<void>(state.model->load_json(config.views, config.defaults));
        state.model->rebuild_all();
    } else {
        initialize_views(state, config);
    }
}

void rebuild_overwrite(UiState& state) {
    const std::uint64_t now = monotonic_ns();
    for (std::uint64_t seq = state.ring->head(); seq < state.ring->tail(); ++seq) {
        if (const auto record = state.ring->find(seq); record.has_value() && record->is_frame()) {
            static_cast<void>(state.overwrite.update(*record, now));
        }
    }
}

void merge_import_channels(UiState& state, const ImportResult& imported) {
    for (const auto& imported_channel : imported.header.channels) {
        const auto existing = std::find_if(
            state.channels.begin(), state.channels.end(), [&](const CanViewChannel& channel) {
                return channel.channel_id == imported_channel.channel_id;
            });
        if (existing == state.channels.end()) {
            state.channels.push_back(imported_channel);
            continue;
        }
        if (existing->name.empty()) {
            existing->name = imported_channel.name;
        }
        if (existing->nominal_bitrate == 0U) {
            existing->nominal_bitrate = imported_channel.nominal_bitrate;
        }
        if (existing->data_bitrate == 0U) {
            existing->data_bitrate = imported_channel.data_bitrate;
        }
        existing->fd = existing->fd || imported_channel.fd;
    }
}

void observe_time(UiState& state) {
    for (std::uint64_t seq = state.ring->head(); seq < state.ring->tail(); ++seq) {
        if (const auto record = state.ring->find(seq); record.has_value()) {
            state.time.observe(*record);
        }
    }
    check_shared_timebase(state);
}

void apply_import(UiState& state, ImportResult imported) {
    if (imported.unsupported_format) {
        state.import_banner = "CSV is not a session format";
        state.import_banner_warning = true;
        return;
    }
    if (imported.malformed > 0U && imported.records.empty()) {
        state.import_banner = "cannot open session";
        state.import_banner_warning = true;
        return;
    }
    merge_import_channels(state, imported);
    state.show_configure = state.channels.empty();
    state.offline_index.clear();
    static_cast<void>(replay_into_ring(imported, *state.ring));
    observe_time(state);
    std::uint64_t record_seq = 0U;
    for (std::size_t index = 0; index < imported.records.size(); ++index) {
        const auto type = imported.records[index].type;
        if (type == CanViewRecordType::Frame || type == CanViewRecordType::Gap) {
            state.offline_index[record_seq++] = index;
        }
    }
    if (imported.cancelled) {
        state.import_banner =
            "import cancelled; " + std::to_string(imported.records.size()) + " records kept";
        state.import_banner_warning = true;
    } else {
        state.import_banner = imported.mixed_boot
                                  ? "warning: mixed boot sessions; deltas are not comparable"
                                  : "session imported";
    }
    state.import_banner_warning = imported.cancelled || imported.mixed_boot;
    state.import_result = std::move(imported);
    state.offline = std::make_unique<OfflineCanopen>();
    state.offline->build(*state.import_result, state.eds_library.get());
    for (const auto& warning : state.offline->warnings()) {
        state.import_banner = warning;
        state.import_banner_warning = true;
    }
    rebuild_overwrite(state);
    state.model->rebuild_all();
}

void start_import(UiState& state) {
    if (state.import_running) {
        return;
    }
    state.import_cancel.store(false, std::memory_order_relaxed);
    state.import_running = true;
    state.import_banner.clear();
    const std::string path = state.import_path;
    auto* cancelled = &state.import_cancel;
    state.import_future = std::async(std::launch::async, [path, cancelled]() {
        return SessionImporter::load(path, SessionImporter::default_max_line,
                                     SessionImporter::default_max_records, cancelled);
    });
}

void pump_import(UiState& state) {
    if (!state.import_running || !state.import_future.valid()) {
        return;
    }
    if (state.import_future.wait_for(std::chrono::seconds(0)) != std::future_status::ready) {
        return;
    }
    auto imported = state.import_future.get();
    state.import_running = false;
    apply_import(state, std::move(imported));
}

bool initialize(UiState& state, const CliOptions& options, const CanViewConfig& config) {
    state.plan = plan_for_budget(config.memory_mb);
    state.channels = config.channels;
    state.time.assume_shared_timebase = config.shared_timebase;
    state.boot_token = static_cast<std::uint64_t>(std::time(nullptr));
    state.show_configure =
        std::none_of(state.channels.begin(), state.channels.end(),
                     [](const CanViewChannel& channel) { return channel.enabled; });
    auto ring = CanRecordRing::create(state.plan.ring_capacity);
    if (!ring) {
        state.status = "history allocation failed";
        return false;
    }
    state.ring = std::make_unique<CanRecordRing>(std::move(*ring));
    state.source = std::make_unique<RingRecordSource>(*state.ring);
    state.model = std::make_unique<CanViewModel>(*state.ring);
    state.overwrite.init(state.plan.overwrite_rows);
    state.heatmap.init(state.plan.heatmap_columns);
    state.model->set_paused_limit(state.plan.paused_snapshots);

    if (options.offline) {
        if (!load_offline(state, options.offline_file)) {
            return false;
        }
    } else if (options.mock_records > 0U) {
        generate_mock(state, config, options.mock_records);
    } else if (!state.channels.empty()) {
        start_capture(state, config);
    }
    restore_views(state, config);
    initialize_canopen(state, config);
    if (state.import_result.has_value()) {
        state.offline = std::make_unique<OfflineCanopen>();
        state.offline->build(*state.import_result, state.eds_library.get());
        for (const auto& warning : state.offline->warnings()) {
            state.status = warning;
        }
    }
    rebuild_overwrite(state);
    if (state.import_result.has_value()) {
        state.show_configure = false;
    }
    return true;
}

CanView* ensure_focused(UiState& state) {
    CanView* view = state.model->focused();
    if (view == nullptr && !state.model->views().empty()) {
        view = state.model->views().front().get();
        state.model->focus(view->id());
    }
    return view;
}

int script_actions(UiState& state, CanView& view) {
    const std::uint64_t seq =
        view.index().visible_count() > 0U ? view.index().visible_seq_at(0U, view.source()) : 0U;
    if (view.display_count() > 0U) {
        view.select_only(seq);
        static_cast<void>(view.toggle_bookmark(seq));
    }
    view.set_table_mode(TableMode::Overwrite);
    if (!state.overwrite.rows().empty()) {
        state.overwrite.select_key(state.overwrite.rows().front().key);
    }
    std::printf("UI_ACTIONS selected=%d bookmarks=%llu records=%zu overwrite=%zu keys=%zu\n",
                view.last_selected().has_value() ? 1 : 0,
                static_cast<unsigned long long>(view.bookmarked_count()), view.display_count(),
                state.overwrite.rows().size(), state.overwrite.selected_keys().size());
    return view.last_selected().has_value() ? 0 : 1;
}

int script_views(UiState& state, CanView& view) {
    static_cast<void>(
        state.model->clone_view(view.id(), true, "Paused clone", CloneOptions{}, ViewDefaults{}));
    std::printf("UI_VIEWS live=%zu paused=%zu\n", state.model->live_count(),
                state.model->paused_count());
    return 0;
}

int script_focus(UiState& state, CanView& view) {
    const bool changed = state.model->set_focused_channel_visible(1U, false);
    std::printf("UI_FOCUS focused=%u changed=%d mask=0x%08x\n", view.id(), changed ? 1 : 0,
                view.filter().channel_mask);
    return changed ? 0 : 1;
}

int script_input(CanView& view) {
    view.set_paused(true);
    view.set_auto_scroll(false);
    const std::uint64_t seq =
        view.index().visible_count() > 0U ? view.index().visible_seq_at(0U, view.source()) : 0U;
    if (view.display_count() > 0U) {
        view.select_only(seq);
    }
    std::printf("UI_INPUT paused=%d autoscroll=%d selected=%d\n", view.paused() ? 1 : 0,
                view.auto_scroll() ? 1 : 0, view.last_selected().has_value() ? 1 : 0);
    return view.paused() && !view.auto_scroll() ? 0 : 1;
}

int run_model_script(const CliOptions& options, UiState& state) {
    if (state.model->views().empty()) {
        std::printf("UI_SCRIPT nodata=1\n");
        return 1;
    }
    CanView* view = ensure_focused(state);
    if (view == nullptr) {
        return 1;
    }
    if (options.ui_actions_test) {
        return script_actions(state, *view);
    }
    if (options.ui_view_test) {
        return script_views(state, *view);
    }
    if (options.ui_focus_test) {
        return script_focus(state, *view);
    }
    if (options.ui_input_test) {
        return script_input(*view);
    }
    return 0;
}

void draw_import_panel(UiState& state);

void draw_toolbar(UiState& state, const CanViewConfig& /*config*/, const CliOptions& options) {
    if (!ImGui::Begin("Toolbar")) {
        ImGui::End();
        return;
    }
    ImGui::TextUnformatted("nexigen-canview");
    ImGui::SameLine();
    ImGui::TextDisabled("%s", state.status.c_str());
    ImGui::SameLine();
    if (ImGui::Button("Pause") && state.model->focused() != nullptr) {
        state.model->focused()->set_paused(!state.model->focused()->paused());
    }
    ImGui::SameLine();
    if (ImGui::Button("Clone LIVE") && state.model->focused() != nullptr) {
        state.clone_paused = false;
        state.clone_modal = true;
    }
    ImGui::SameLine();
    if (ImGui::Button("Clone PAUSED") && state.model->focused() != nullptr) {
        state.clone_paused = true;
        state.clone_modal = true;
    }
    ImGui::SameLine();
    if (ImGui::Button("Import session")) {
        state.show_import = true;
    }
    ImGui::SameLine();
    ImGui::Text("channels=%zu views=%zu", state.channels.size(), state.model->views().size());
    if (state.shared_time_warning) {
        ImGui::TextColored(ImVec4(1.0F, 0.7F, 0.2F, 1.0F),
                           "warning: channel hardware clocks diverge");
    }
    if (!state.import_banner.empty()) {
        if (state.import_banner_warning) {
            ImGui::TextColored(ImVec4(1.0F, 0.7F, 0.2F, 1.0F), "%s", state.import_banner.c_str());
        } else {
            ImGui::TextDisabled("%s", state.import_banner.c_str());
        }
    }
    if (options.ui_smoke) {
        ImGui::SameLine();
        ImGui::TextDisabled("smoke");
    }
    ImGui::End();
}

void draw_channel_rows(UiState& state, const CanViewConfig& /*config*/) {
    for (const auto& channel : state.channels) {
        bool visible = true;
        if (CanView* view = state.model->focused(); view != nullptr) {
            visible = view->filter().channel_visible(channel.channel_id);
        }
        if (ImGui::Checkbox(channel.name.c_str(), &visible)) {
            static_cast<void>(
                state.model->set_focused_channel_visible(channel.channel_id, visible));
        }
        ImGui::SameLine();
        ImGui::TextDisabled("id=%u %s", channel.channel_id, channel.enabled ? "enabled" : "off");
    }
}

void draw_configure_panel(UiState& state, const CanViewConfig& /*config*/) {
    if (!state.show_configure) {
        return;
    }
    ImGui::SetNextWindowSize(ImVec2(360, 220), ImGuiCond_FirstUseEver);
    if (ImGui::Begin("Configure channels", &state.show_configure)) {
        for (const auto& channel : state.channels) {
            ImGui::Text("%s (id %u)", channel.name.c_str(), channel.channel_id);
        }
        if (state.channels.empty()) {
            ImGui::TextDisabled("no profiles");
        }
    }
    ImGui::End();
}

void draw_capture_controls(UiState& state) {
    if (state.capture != nullptr && ImGui::Button("Restart capture")) {
        state.capture->stop();
        state.status = state.capture->start() ? "capture restarted" : "capture restart failed";
    }
    if (state.capture == nullptr) {
        ImGui::TextDisabled("capture stopped");
    }
    ImGui::SameLine();
    if (ImGui::Button("Configure channels")) {
        state.show_configure = true;
    }
}

std::optional<std::uint32_t> parse_hex_u32(const char* text);

void reload_eds(UiState& state) {
    if (state.eds_library == nullptr) {
        return;
    }
    bool reloaded = false;
    for (std::uint16_t slot = 0; slot < CanOpenRegistry::capacity; ++slot) {
        const NodeBinding* binding = state.registry->at(slot);
        if (binding == nullptr || binding->eds_path.empty()) {
            continue;
        }
        const auto loaded = state.eds_library->load(binding->eds_path);
        if (loaded.reloaded) {
            reloaded = true;
        }
        for (const auto& diagnostic : loaded.diagnostics) {
            state.eds_diagnostics.push_back(diagnostic.message);
        }
        if (loaded.dictionary != nullptr) {
            static_cast<void>(state.registry->refresh_dictionary(
                binding->eds_path, loaded.dictionary, loaded.dictionary->content_hash()));
        }
    }
    state.router->rebuild(*state.registry);
    state.reindexer->begin(*state.ring, *state.router);
    refresh_registry_views(state);
    if (reloaded) {
        state.status = "warning: EDS changed; bindings refreshed";
        state.eds_diagnostics.push_back("EDS file changed; bindings refreshed");
    } else {
        state.status = "EDS unchanged";
    }
}

void draw_binding_row(UiState& state, std::uint16_t slot, const NodeBinding& binding) {
    ImGui::PushID(slot);
    ImGui::ColorButton("##colour", ImGui::ColorConvertU32ToFloat4(binding.colour),
                       ImGuiColorEditFlags_NoTooltip, ImVec2(14.0F, 14.0F));
    ImGui::SameLine();
    const std::uint64_t now_ns = monotonic_ns();
    const bool stale = binding.stale(now_ns);
    ImGui::Text("ch%u node%u %s [%s%s]", binding.channel_id, binding.node_id, binding.alias.c_str(),
                to_string(binding.nmt_state).data(), stale ? " stale" : "");
    if (binding.heartbeat_seen) {
        const std::uint64_t age =
            now_ns > binding.last_heartbeat_ns ? now_ns - binding.last_heartbeat_ns : 0U;
        ImGui::SameLine();
        ImGui::TextDisabled("hb %llu ms", age / 1'000'000ULL);
    }
    ImGui::SameLine();
    if (ImGui::Button("Open Node")) {
        const auto view = state.model->create_view(
            ViewKind::Node, binding.channel_id, binding.node_id,
            "Node " + std::to_string(binding.node_id), ViewDefaults{}, slot);
        if (view.has_value()) {
            state.model->focus(*view);
        }
    }
    ImGui::SameLine();
    if (ImGui::Button("Remove")) {
        static_cast<void>(state.registry->unbind_slot(slot));
        state.router->rebuild(*state.registry);
        state.reindexer->begin(*state.ring, *state.router);
        refresh_registry_views(state);
        state.status = "binding removed";
    }
    ImGui::PopID();
}

void apply_binding(UiState& state, std::uint16_t channel, std::uint8_t node,
                   const std::string& path) {
    state.eds_diagnostics.clear();
    const auto loaded = state.eds_library->load(path);
    for (const auto& diagnostic : loaded.diagnostics) {
        state.eds_diagnostics.push_back(diagnostic.message);
    }
    const auto bound =
        state.registry->bind(channel, node, "", 0xFF4FC3F7U, loaded.dictionary, path, true);
    if (!bound.has_value()) {
        if (bound.error() == nexigen::core::ErrorCode::ResourceExhausted) {
            state.status = "binding limit reached (128)";
        } else if (bound.error() == nexigen::core::ErrorCode::Conflict) {
            state.status = "binding exists";
        } else {
            state.status = "binding rejected";
        }
        return;
    }
    state.router->rebuild(*state.registry);
    state.reindexer->begin(*state.ring, *state.router);
    refresh_registry_views(state);
    state.status = "binding added";
}

bool parse_bind_inputs(UiState& state, std::uint16_t& channel, std::uint8_t& node) {
    const auto parsed_channel = parse_hex_u32(state.eds_channel);
    std::uint32_t parsed_node = 0U;
    if (state.eds_node[0] != '\0') {
        const auto result = std::from_chars(
            state.eds_node, state.eds_node + std::strlen(state.eds_node), parsed_node);
        if (result.ec != std::errc{}) {
            parsed_node = 0U;
        }
    }
    if (!parsed_channel.has_value() || parsed_node == 0U || parsed_node >= 128U ||
        state.eds_path[0] == '\0') {
        state.status = "binding input is invalid";
        return false;
    }
    channel = static_cast<std::uint16_t>(*parsed_channel);
    node = static_cast<std::uint8_t>(parsed_node);
    return true;
}

void draw_bind_confirm(UiState& state) {
    if (!state.bind_confirm) {
        return;
    }
    ImGui::OpenPopup("Replace binding");
    if (ImGui::BeginPopupModal("Replace binding", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
        ImGui::Text("A binding for ch%u node%u already exists.", state.bind_channel,
                    static_cast<unsigned>(state.bind_node));
        if (ImGui::Button("Replace")) {
            apply_binding(state, state.bind_channel, state.bind_node, state.bind_path);
            state.bind_confirm = false;
            ImGui::CloseCurrentPopup();
        }
        ImGui::SameLine();
        if (ImGui::Button("Cancel")) {
            state.bind_confirm = false;
            state.status = "binding cancelled";
            ImGui::CloseCurrentPopup();
        }
        ImGui::EndPopup();
    }
}

void draw_bind_form(UiState& state) {
    ImGui::SetNextItemWidth(60.0F);
    ImGui::InputText("Ch##eds", state.eds_channel, sizeof(state.eds_channel));
    ImGui::SameLine();
    ImGui::SetNextItemWidth(60.0F);
    ImGui::InputText("Node##eds", state.eds_node, sizeof(state.eds_node));
    ImGui::SameLine();
    if (ImGui::Button("Bind EDS")) {
        std::uint16_t channel = 0U;
        std::uint8_t node = 0U;
        if (parse_bind_inputs(state, channel, node)) {
            if (state.registry->find(channel, node) != nullptr) {
                state.bind_channel = channel;
                state.bind_node = node;
                state.bind_path = state.eds_path;
                state.bind_confirm = true;
            } else {
                apply_binding(state, channel, node, state.eds_path);
            }
        }
    }
    ImGui::SetNextItemWidth(200.0F);
    ImGui::InputText("EDS path", state.eds_path, sizeof(state.eds_path));
    draw_bind_confirm(state);
}

void draw_minimap(CanView& view) {
    view.rebuild_minimap();
    const Minimap& map = view.minimap();
    if (map.visible == 0U) {
        ImGui::TextDisabled("no visible records");
        return;
    }
    const float width = std::max(120.0F, ImGui::GetContentRegionAvail().x);
    constexpr float height = 54.0F;
    const ImVec2 origin = ImGui::GetCursorScreenPos();
    ImDrawList* draw = ImGui::GetWindowDrawList();
    draw->AddRectFilled(origin, ImVec2(origin.x + width, origin.y + height),
                        IM_COL32(24, 28, 32, 255));
    std::uint32_t peak = 1U;
    for (const std::uint32_t value : map.density) {
        peak = std::max(peak, value);
    }
    for (std::size_t index = 0U; index < Minimap::buckets; ++index) {
        if (map.density[index] == 0U) {
            continue;
        }
        const float x0 =
            origin.x + width * static_cast<float>(index) / static_cast<float>(Minimap::buckets);
        const float x1 = origin.x + width * static_cast<float>(index + 1U) /
                                        static_cast<float>(Minimap::buckets);
        const float bar =
            height * static_cast<float>(map.density[index]) / static_cast<float>(peak);
        draw->AddRectFilled(ImVec2(x0, origin.y + height - bar), ImVec2(x1, origin.y + height),
                            IM_COL32(90, 170, 230, 210));
    }
    const float viewport_start =
        width * static_cast<float>(view.scroll()) / static_cast<float>(map.visible);
    const float viewport_size =
        width * static_cast<float>(view.display_count()) / static_cast<float>(map.visible);
    draw->AddRect(
        ImVec2(origin.x + viewport_start, origin.y),
        ImVec2(origin.x + std::min(width, viewport_start + viewport_size), origin.y + height),
        IM_COL32(255, 200, 80, 230));
    ImGui::InvisibleButton("##minimap", ImVec2(width, height));
    if (ImGui::IsItemHovered() && ImGui::IsMouseClicked(0)) {
        const float ratio = std::clamp((ImGui::GetIO().MousePos.x - origin.x) / width, 0.0F, 1.0F);
        view.set_scroll(static_cast<std::size_t>(
            ratio * static_cast<float>(map.visible > 0U ? map.visible - 1U : 0U)));
        view.set_auto_scroll(false);
    }
}

void draw_eds_and_minimap(UiState& state, const CanViewConfig& /*config*/) {
    if (state.registry != nullptr) {
        ImGui::SeparatorText("EDS bindings");
        if (ImGui::Button("Reload EDS")) {
            state.eds_diagnostics.clear();
            reload_eds(state);
        }
        for (const auto& diagnostic : state.eds_diagnostics) {
            ImGui::TextColored(ImVec4(1.0F, 0.7F, 0.2F, 1.0F), "%s", diagnostic.c_str());
        }
        for (std::uint16_t slot = 0; slot < CanOpenRegistry::capacity; ++slot) {
            if (const NodeBinding* binding = state.registry->at(slot); binding != nullptr) {
                draw_binding_row(state, slot, *binding);
            }
        }
        draw_bind_form(state);
        if (state.reindexer != nullptr && state.reindexer->active()) {
            const auto progress = state.reindexer->progress();
            ImGui::Text("reindex %llu/%llu", static_cast<unsigned long long>(progress.scanned),
                        static_cast<unsigned long long>(progress.total));
        }
    }
    ImGui::SeparatorText("Minimap");
    if (CanView* view = state.model->focused(); view != nullptr) {
        draw_minimap(*view);
    }
}

std::optional<std::uint32_t> parse_hex_u32(const char* text) {
    if (text == nullptr || *text == '\0') {
        return std::nullopt;
    }
    std::string_view view{text};
    int base = 10;
    if (view.size() > 2U && view[0] == '0' && (view[1] == 'x' || view[1] == 'X')) {
        base = 16;
        view.remove_prefix(2U);
    }
    std::uint32_t value = 0;
    const auto result = std::from_chars(view.data(), view.data() + view.size(), value, base);
    if (result.ec != std::errc{} || result.ptr != view.data() + view.size()) {
        return std::nullopt;
    }
    return value;
}

void sync_filter_buffers(UiState& state, CanView& view) {
    if (state.filter_synced_id == view.id()) {
        return;
    }
    state.filter_synced_id = view.id();
    const auto& criteria = view.filter();
    std::snprintf(state.filter_text, sizeof(state.filter_text), "%s", criteria.text.c_str());
    std::snprintf(state.filter_id, sizeof(state.filter_id), "0x%x", criteria.id);
    std::snprintf(state.filter_mask, sizeof(state.filter_mask), "0x%x", criteria.id_mask);
    state.filter_errors = criteria.errors_only;
    state.filter_idle = criteria.show_bus_idle;
    state.filter_gaps = criteria.show_gaps;
    state.filter_origin_mask = criteria.origin_mask;
}

void apply_filter(UiState& state, CanView& view) {
    FilterCriteria& criteria = view.filter();
    criteria.set_text(state.filter_text);
    criteria.errors_only = state.filter_errors;
    criteria.show_bus_idle = state.filter_idle;
    criteria.show_gaps = state.filter_gaps;
    criteria.origin_mask = state.filter_origin_mask;
    if (const auto id = parse_hex_u32(state.filter_id); id.has_value()) {
        criteria.id = *id;
        criteria.id_filter_active = true;
    } else {
        criteria.id_filter_active = false;
    }
    if (const auto mask = parse_hex_u32(state.filter_mask); mask.has_value()) {
        criteria.id_mask = *mask;
    }
    view.begin_rebuild();
    state.status = "filter applied";
}

void apply_find(UiState& state, CanView& view) {
    state.overwrite_matches.clear();
    state.overwrite_cursor = 0U;
    if (view.table_mode() == TableMode::Overwrite) {
        FilterCriteria matcher{};
        matcher.show_bus_idle = true;
        matcher.show_gaps = true;
        matcher.kind_mask = 0xFFU;
        matcher.origin_mask = 0xFFU;
        matcher.set_text(state.find_text);
        for (const auto& row : state.overwrite.rows()) {
            CanRecordView record{};
            record.seq = row.last_seq;
            record.message = &row.latest;
            record.meta = pack_frame_meta(CanRecordKind::Frame);
            if (record.message != nullptr && state.router != nullptr && !record.message->extended) {
                if (const auto route = state.router->lookup(record.message->channel_id,
                                                            record.message->can_id, false);
                    route.has_value()) {
                    record.co_node_index = route->slot;
                }
            }
            if (view.filter().matches(record) && matcher.matches(record)) {
                state.overwrite_matches.push_back(row.key);
            }
        }
        if (!state.overwrite_matches.empty()) {
            static_cast<void>(state.overwrite.toggle_key(state.overwrite_matches.front()));
        }
    } else {
        view.find().set_query(state.find_text);
        view.find().begin_rebuild(view.index());
        if (!view.find().rebuilding()) {
            if (const auto match = view.find().current(); match.has_value()) {
                view.select_only(*match);
            }
        }
    }
    state.status = "find applied";
}

void pump_find(CanView& view) {
    if (view.find().rebuilding()) {
        static_cast<void>(
            view.find().step_rebuild(view.source(), view.index(), view.labels(), 20'000U));
        if (!view.find().rebuilding()) {
            if (const auto match = view.find().current(); match.has_value()) {
                view.select_only(*match);
            }
        }
    }
}

void find_step(UiState& state, CanView& view, bool forward) {
    if (view.find().rebuilding()) {
        return;
    }
    if (view.table_mode() == TableMode::Overwrite) {
        if (state.overwrite_matches.empty()) {
            return;
        }
        state.overwrite_cursor =
            forward ? (state.overwrite_cursor + 1U) % state.overwrite_matches.size()
                    : (state.overwrite_cursor + state.overwrite_matches.size() - 1U) %
                          state.overwrite_matches.size();
        const OverwriteKey& key = state.overwrite_matches[state.overwrite_cursor];
        static_cast<void>(state.overwrite.toggle_key(key));
        return;
    }
    const auto match = forward ? view.find().next() : view.find().previous();
    if (match.has_value()) {
        view.select_only(*match);
    }
}

void draw_filter_controls(UiState& state, CanView& view) {
    ImGui::SeparatorText("Filter");
    ImGui::SetNextItemWidth(180.0F);
    ImGui::InputText("Text/hex", state.filter_text, sizeof(state.filter_text));
    ImGui::SameLine();
    if (ImGui::Button("Apply filter")) {
        apply_filter(state, view);
    }
    ImGui::SetNextItemWidth(90.0F);
    ImGui::InputText("ID", state.filter_id, sizeof(state.filter_id));
    ImGui::SameLine();
    ImGui::SetNextItemWidth(90.0F);
    ImGui::InputText("Mask", state.filter_mask, sizeof(state.filter_mask));
    ImGui::Checkbox("Errors only", &state.filter_errors);
    ImGui::SameLine();
    ImGui::Checkbox("Bus idle", &state.filter_idle);
    ImGui::SameLine();
    ImGui::Checkbox("Gaps", &state.filter_gaps);
    bool external = (state.filter_origin_mask & 0x01U) != 0U;
    bool local = (state.filter_origin_mask & 0x02U) != 0U;
    bool unknown = (state.filter_origin_mask & 0x04U) != 0U;
    if (ImGui::Checkbox("External", &external)) {
        state.filter_origin_mask = static_cast<std::uint8_t>(
            external ? (state.filter_origin_mask | 0x01U) : (state.filter_origin_mask & ~0x01U));
    }
    ImGui::SameLine();
    if (ImGui::Checkbox("Local", &local)) {
        state.filter_origin_mask = static_cast<std::uint8_t>(
            local ? (state.filter_origin_mask | 0x02U) : (state.filter_origin_mask & ~0x02U));
    }
    ImGui::SameLine();
    if (ImGui::Checkbox("Unknown", &unknown)) {
        state.filter_origin_mask = static_cast<std::uint8_t>(
            unknown ? (state.filter_origin_mask | 0x04U) : (state.filter_origin_mask & ~0x04U));
    }
}

void draw_find_controls(UiState& state, CanView& view) {
    ImGui::SeparatorText("Find");
    ImGui::SetNextItemWidth(180.0F);
    if (state.focus_find) {
        ImGui::SetKeyboardFocusHere();
        state.focus_find = false;
    }
    if (ImGui::InputText("Query", state.find_text, sizeof(state.find_text)) &&
        ImGui::IsItemFocused()) {
        state.status = "query edited";
    }
    if (ImGui::IsItemFocused() && ImGui::IsKeyPressed(ImGuiKey_Enter)) {
        apply_find(state, view);
    }
    ImGui::SameLine();
    if (ImGui::Button("Find")) {
        apply_find(state, view);
    }
    ImGui::SameLine();
    if (ImGui::Button("Next")) {
        find_step(state, view, true);
    }
    ImGui::SameLine();
    if (ImGui::Button("Prev")) {
        find_step(state, view, false);
    }
    const std::size_t matches = view.table_mode() == TableMode::Overwrite
                                    ? state.overwrite_matches.size()
                                    : view.find().count();
    ImGui::TextDisabled("%zu matches", matches);
    if (state.router != nullptr && !state.router->conflicts().empty()) {
        ImGui::SeparatorText("COB-ID conflicts");
        for (const auto& conflict : state.router->conflicts()) {
            ImGui::Text("ch%u 0x%x kept %u rejected %u", conflict.channel_id, conflict.can_id,
                        conflict.kept_slot, conflict.rejected_slot);
        }
    }
}

void draw_filter_find(UiState& state, CanView& view) {
    sync_filter_buffers(state, view);
    draw_filter_controls(state, view);
    draw_find_controls(state, view);
}

void draw_import_panel(UiState& state) {
    if (!state.show_import) {
        return;
    }
    ImGui::SetNextWindowSize(ImVec2(420, 180), ImGuiCond_FirstUseEver);
    if (ImGui::Begin("Import session", &state.show_import)) {
        ImGui::SetNextItemWidth(320.0F);
        ImGui::InputText("Path", state.import_path, sizeof(state.import_path));
        if (state.import_running) {
            ImGui::TextDisabled("importing...");
            ImGui::SameLine();
            if (ImGui::Button("Cancel import")) {
                state.import_cancel.store(true, std::memory_order_relaxed);
            }
        } else {
            const bool enabled = state.import_path[0] != '\0';
            if (!enabled) {
                ImGui::BeginDisabled();
            }
            if (ImGui::Button("Import")) {
                start_import(state);
            }
            if (!enabled) {
                ImGui::EndDisabled();
            }
        }
        if (state.shared_time_warning) {
            ImGui::TextColored(ImVec4(1.0F, 0.7F, 0.2F, 1.0F),
                               "warning: channel hardware clocks diverge");
        }
        if (!state.import_banner.empty()) {
            if (state.import_banner_warning) {
                ImGui::TextColored(ImVec4(1.0F, 0.7F, 0.2F, 1.0F), "%s",
                                   state.import_banner.c_str());
            } else {
                ImGui::TextDisabled("%s", state.import_banner.c_str());
            }
        }
    }
    ImGui::End();
}

void draw_channels(UiState& state, const CanViewConfig& config) {
    if (!ImGui::Begin("Channels & CANopen")) {
        ImGui::End();
        return;
    }
    draw_channel_rows(state, config);
    draw_capture_controls(state);
    draw_import_panel(state);
    draw_configure_panel(state, config);
    if (CanView* view = state.model->focused(); view != nullptr) {
        draw_filter_find(state, *view);
        int colour_mode = static_cast<int>(state.decoder != nullptr ? state.decoder->colour_mode()
                                                                    : NodeColourMode::Node);
        if (ImGui::Combo("Node colour", &colour_mode, "Node\0Criticality\0") &&
            state.decoder != nullptr) {
            state.decoder->set_colour_mode(static_cast<NodeColourMode>(colour_mode));
        }
    }
    draw_eds_and_minimap(state, config);
    ImGui::End();
}

const NodeBinding* binding_for(UiState& state, const nexigen::hw::can::CanMessage& message) {
    if (state.router == nullptr || state.registry == nullptr || message.extended) {
        return nullptr;
    }
    const auto route = state.router->lookup(message.channel_id, message.can_id, false);
    if (!route.has_value() || route->slot >= CanOpenRegistry::capacity) {
        return nullptr;
    }
    return state.registry->at(route->slot);
}

std::string node_service_text(UiState& state, const nexigen::hw::can::CanMessage& message) {
    if (state.router == nullptr || message.extended) {
        return "-";
    }
    const auto route = state.router->lookup(message.channel_id, message.can_id, false);
    if (!route.has_value()) {
        return "-";
    }
    return std::format("{} {}", route->node_id, to_string(route->service));
}

std::uint32_t row_colour(UiState& state, const nexigen::hw::can::CanMessage& message) {
    const NodeBinding* binding = binding_for(state, message);
    if (state.decoder != nullptr && state.decoder->colour_mode() == NodeColourMode::Criticality &&
        (message.kind == nexigen::hw::can::CanMessageKind::Error ||
         message.kind == nexigen::hw::can::CanMessageKind::Overload)) {
        return 0xFFE53935U;
    }
    return binding != nullptr ? binding->colour : 0xFFFFFFFFU;
}

void setup_trace_columns(bool show_hw, bool show_utc) {
    ImGui::TableSetupColumn(" ");
    ImGui::TableSetupColumn("#");
    if (show_hw) {
        ImGui::TableSetupColumn("HW time");
    }
    if (show_utc) {
        ImGui::TableSetupColumn("UTC (est.)");
    }
    ImGui::TableSetupColumn("Delta");
    ImGui::TableSetupColumn("Ch");
    ImGui::TableSetupColumn("Node/Service");
    ImGui::TableSetupColumn("ID");
    ImGui::TableSetupColumn("Kind");
    ImGui::TableSetupColumn("Flags");
    ImGui::TableSetupColumn("Len");
    ImGui::TableSetupColumn("Data");
}

void check_shared_timebase(UiState& state) {
    if (!state.time.assume_shared_timebase || state.shared_time_warning) {
        return;
    }
    std::uint64_t minimum = 0U;
    std::uint64_t maximum = 0U;
    std::size_t count = 0U;
    for (std::size_t channel = 0; channel < TimeContext::channels; ++channel) {
        if (!state.time.session_started[channel]) {
            continue;
        }
        const std::uint64_t start = state.time.session_start[channel];
        if (count == 0U || start < minimum) {
            minimum = start;
        }
        if (count == 0U || start > maximum) {
            maximum = start;
        }
        ++count;
    }
    if (count > 1U && maximum > minimum && maximum - minimum > 1'000'000'000ULL) {
        state.shared_time_warning = true;
        state.status = "warning: channel hardware clocks diverge";
    }
}

std::string gap_text(const CanRecordView& record) {
    if (record.is_frame()) {
        return std::string(to_string(record.kind()));
    }
    return std::format("{} {} #{}", to_string(record.kind()),
                       to_string(meta_gap_reason(record.meta)), record.gap_count());
}

void draw_hw_time_cell(UiState& state, CanView& view, const CanRecordView& record, int& column) {
    ImGui::TableSetColumnIndex(column++);
    const auto cell = TimePresenter::hardware(record, view.timestamp_mode(), view, state.time);
    ImGui::TextUnformatted(cell.text.data());
    if (!cell.available && ImGui::IsItemHovered() && cell.reason[0] != '\0') {
        ImGui::SetTooltip("%s", cell.reason.data());
    }
}

void draw_utc_time_cell(UiState& state, const CanRecordView& record, int& column) {
    ImGui::TableSetColumnIndex(column++);
    const auto cell = TimePresenter::utc(record, state.time);
    ImGui::TextUnformatted(cell.text.data());
    if (!ImGui::IsItemHovered()) {
        return;
    }
    const auto anchor = TimePresenter::anchor(record, state.time);
    if (!anchor.has_value()) {
        ImGui::SetTooltip("%s", cell.reason.data());
        return;
    }
    const std::uint64_t age = record.message->hardware_timestamp_ns > anchor->valid_from_hw_ns
                                  ? record.message->hardware_timestamp_ns - anchor->valid_from_hw_ns
                                  : 0U;
    ImGui::SetTooltip(
        "source=%s offset=%lld ns drift=%d ppb age=%llu ns epoch=%u",
        core::enum_name(static_cast<nexigen::hw::can::CanTimestampSource>(anchor->source)).data(),
        static_cast<long long>(anchor->utc_offset_ns), static_cast<int>(anchor->drift_ppb),
        static_cast<unsigned long long>(age), static_cast<unsigned>(anchor->epoch));
}

void draw_delta_cell(UiState& state, CanView& view, const CanRecordView& record,
                     const CanRecordView* previous, int& column) {
    ImGui::TableSetColumnIndex(column++);
    const auto delta = TimePresenter::delta(record, previous, view.delta_mode(), view, state.time);
    const bool anomalous =
        delta.available && TimePresenter::is_anomalous(delta.value, view.anomaly_threshold_ns());
    ImGui::TextColored(anomalous ? ImVec4(1.0F, 0.65F, 0.2F, 1.0F)
                                 : ImGui::GetStyleColorVec4(ImGuiCol_Text),
                       "%s", delta.text.data());
    if (!delta.available && ImGui::IsItemHovered() && delta.reason[0] != '\0') {
        ImGui::SetTooltip("%s", delta.reason.data());
    }
}

void draw_trace_data_cell(UiState& state, const CanRecordView& record, int column) {
    ImGui::TableSetColumnIndex(column);
    const auto& message = *record.message;
    const ImU32 colour = row_colour(state, message);
    if (record.is_frame()) {
        ImGui::TextColored(ImColor(colour).Value, "%s", payload_to_hex(message).c_str());
    } else {
        ImGui::TextColored(ImColor(colour).Value, "reason=%s count=%llu exact=%s",
                           to_string(meta_gap_reason(record.meta)).data(),
                           static_cast<unsigned long long>(record.gap_count()),
                           meta_count_exact(record.meta) ? "yes" : "at least");
    }
    if (ImGui::IsItemHovered()) {
        ImGui::SetTooltip("%s", channel_name(state.channels, message.channel_id));
    }
}

void select_trace_range(CanView& view, std::uint64_t first, std::uint64_t last) {
    std::size_t first_order = view.display_count();
    std::size_t last_order = view.display_count();
    for (std::size_t order = 0; order < view.display_count(); ++order) {
        const std::uint64_t seq = view.display_at(order);
        if (seq == first) {
            first_order = order;
        }
        if (seq == last) {
            last_order = order;
        }
    }
    if (first_order == view.display_count() || last_order == view.display_count()) {
        return;
    }
    view.clear_selection();
    const std::size_t begin = std::min(first_order, last_order);
    const std::size_t end = std::max(first_order, last_order);
    for (std::size_t order = begin; order <= end; ++order) {
        const std::uint64_t seq = view.display_at(order);
        if (seq != 0U) {
            view.toggle_selection(seq);
        }
    }
}

void handle_trace_click(CanView& view, std::uint64_t seq) {
    const ImGuiIO& io = ImGui::GetIO();
    if (io.KeyCtrl) {
        view.toggle_selection(seq);
        return;
    }
    if (io.KeyShift) {
        if (const auto anchor = view.last_selected(); anchor.has_value()) {
            select_trace_range(view, *anchor, seq);
            return;
        }
    }
    view.select_only(seq);
}

void draw_trace_row(UiState& state, CanView& view, const CanViewConfig& /*config*/,
                    const CanRecordView& record, const CanRecordView* previous, bool show_hw,
                    bool show_utc) {
    const auto& message = *record.message;
    if (view.find().contains(record.seq)) {
        ImGui::TableSetBgColor(ImGuiTableBgTarget_RowBg0, IM_COL32(90, 80, 20, 180));
    }
    ImGui::TableNextRow();
    ImGui::TableSetColumnIndex(0);
    const char* bookmark = view.is_bookmarked(record.seq) ? "*" : " ";
    if (ImGui::Selectable(bookmark, view.is_selected(record.seq),
                          ImGuiSelectableFlags_SpanAllColumns)) {
        handle_trace_click(view, record.seq);
    }
    ImGui::TableSetColumnIndex(1);
    ImGui::Text("%llu", static_cast<unsigned long long>(record.seq));
    int column = 2;
    if (show_hw) {
        draw_hw_time_cell(state, view, record, column);
    }
    if (show_utc) {
        draw_utc_time_cell(state, record, column);
    }
    draw_delta_cell(state, view, record, previous, column);
    ImGui::TableSetColumnIndex(column++);
    ImGui::Text("%u", message.channel_id);
    ImGui::TableSetColumnIndex(column++);
    ImGui::TextUnformatted(node_service_text(state, message).c_str());
    ImGui::TableSetColumnIndex(column++);
    ImGui::Text("0x%x", message.can_id);
    ImGui::TableSetColumnIndex(column++);
    ImGui::TextUnformatted(gap_text(record).c_str());
    ImGui::TableSetColumnIndex(column++);
    ImGui::TextUnformatted(flags_text(record.meta).c_str());
    ImGui::TableSetColumnIndex(column++);
    ImGui::Text("%u", static_cast<unsigned>(message.dlc));
    draw_trace_data_cell(state, record, column);
}

void export_trace_selection(UiState& state, const CanView& view);

bool draw_trace_controls(UiState& state, CanView& view) {
    if (ImGui::Button("Trace")) {
        view.set_table_mode(TableMode::Trace);
    }
    ImGui::SameLine();
    if (ImGui::Button("Overwrite")) {
        view.set_table_mode(TableMode::Overwrite);
    }
    ImGui::SameLine();
    bool auto_scroll = view.auto_scroll();
    if (ImGui::Checkbox("Auto-scroll", &auto_scroll)) {
        view.set_auto_scroll(auto_scroll);
    }
    ImGui::SameLine();
    int timestamp = static_cast<int>(view.timestamp_mode());
    if (ImGui::Combo("Timestamp", &timestamp, "Driver\0Since start\0T0\0")) {
        view.set_timestamp_mode(static_cast<TimestampMode>(timestamp));
    }
    ImGui::SameLine();
    int columns = static_cast<int>(view.time_columns());
    if (ImGui::Combo("Time", &columns, "HW\0UTC\0Both\0")) {
        view.set_time_columns(static_cast<TimeColumnMode>(columns));
    }
    ImGui::SameLine();
    int delta = static_cast<int>(view.delta_mode());
    if (ImGui::Combo("Delta", &delta, "Previous\0Per ID\0T0\0")) {
        view.set_delta_mode(static_cast<DeltaMode>(delta));
    }
    if (view.table_mode() == TableMode::Trace) {
        ImGui::SameLine();
        if (ImGui::Button("Save selected")) {
            export_trace_selection(state, view);
        }
    }
    return view.table_mode() == TableMode::Overwrite;
}

void draw_trace(UiState& state, CanView& view, const CanViewConfig& config) {
    if (draw_trace_controls(state, view)) {
        return;
    }
    if (ImGui::IsWindowHovered(ImGuiHoveredFlags_ChildWindows) &&
        ImGui::GetIO().MouseWheel != 0.0F) {
        view.set_auto_scroll(false);
    }
    const bool show_hw = view.time_columns() != TimeColumnMode::UtcOnly;
    const bool show_utc = view.time_columns() != TimeColumnMode::HwOnly;
    constexpr ImGuiTableFlags flags = ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg |
                                      ImGuiTableFlags_ScrollY | ImGuiTableFlags_Resizable |
                                      ImGuiTableFlags_SizingFixedFit;
    const int column_count = 10 + (show_hw ? 1 : 0) + (show_utc ? 1 : 0);
    if (!ImGui::BeginTable("trace", column_count, flags)) {
        return;
    }
    setup_trace_columns(show_hw, show_utc);
    ImGui::TableHeadersRow();
    std::optional<CanRecordView> previous{};
    ImGuiListClipper clipper;
    clipper.Begin(static_cast<int>(view.display_count()));
    while (clipper.Step()) {
        for (int row_index = clipper.DisplayStart; row_index < clipper.DisplayEnd; ++row_index) {
            const std::uint64_t seq = view.display_at(static_cast<std::size_t>(row_index));
            const auto record = view.source().find(seq);
            if (!record.has_value() || record->message == nullptr) {
                continue;
            }
            draw_trace_row(state, view, config, *record,
                           previous.has_value() ? &*previous : nullptr, show_hw, show_utc);
            if (record->is_frame()) {
                previous = *record;
            }
        }
    }
    if (view.auto_scroll()) {
        ImGui::SetScrollY(ImGui::GetScrollMaxY());
    }
    ImGui::EndTable();
}

std::uint64_t system_time_ns() {
    return static_cast<std::uint64_t>(std::chrono::duration_cast<std::chrono::nanoseconds>(
                                          std::chrono::system_clock::now().time_since_epoch())
                                          .count());
}

CanViewSessionHeader make_export_header(UiState& state) {
    CanViewSessionHeader header{};
    header.created_ns = static_cast<std::int64_t>(system_time_ns());
    header.boot_token = state.boot_token;
    header.timebase = nexigen::chrono::TimeBaseClock::attached() ? "timebase" : "none";
    header.channels = state.channels;
    header.frame_count = 0U;
    header.gap_count = 0U;
    for (const std::uint64_t seq : state.export_seqs) {
        const auto record = state.source->find(seq);
        if (record.has_value()) {
            if (record->is_frame()) {
                ++header.frame_count;
            } else {
                ++header.gap_count;
            }
        }
    }
    if (state.registry != nullptr) {
        for (std::uint16_t slot = 0; slot < CanOpenRegistry::capacity; ++slot) {
            const NodeBinding* binding = state.registry->at(slot);
            if (binding == nullptr) {
                continue;
            }
            SessionEdsBinding eds{};
            eds.channel_id = binding->channel_id;
            eds.node_id = binding->node_id;
            eds.alias = binding->alias;
            eds.path = binding->eds_path;
            eds.hash = binding->eds_hash;
            eds.colour = binding->colour;
            header.eds.push_back(std::move(eds));
        }
    }
    return header;
}

std::optional<CoDecoded> export_decoded(UiState& state, const CanRecordView& record) {
    if (state.offline != nullptr && state.import_result.has_value()) {
        if (const auto it = state.offline_index.find(record.seq);
            it != state.offline_index.end() && it->second < state.import_result->records.size()) {
            const auto offline = state.offline->decode(state.import_result->records[it->second]);
            if (offline.source != OfflineDecodeSource::None) {
                return offline.decoded;
            }
        }
    }
    if (state.export_decoder != nullptr) {
        return state.export_decoder->decode(record, monotonic_ns());
    }
    return std::nullopt;
}

void enrich_export_time(UiState& state, const CanRecordView& record, SessionFrameRecord& frame) {
    const auto utc = TimePresenter::utc_ns(record, state.time);
    if (utc.has_value()) {
        frame.utc_est_ns = *utc;
        if (const auto anchor = TimePresenter::anchor(record, state.time); anchor.has_value()) {
            frame.utc_source =
                core::enum_name(static_cast<nexigen::hw::can::CanTimestampSource>(anchor->source));
        }
    }
    if (record.message == nullptr) {
        return;
    }
    const std::uint64_t hardware = record.message->hardware_timestamp_ns;
    if (state.export_has_previous && (state.time.assume_shared_timebase ||
                                      frame.message.channel_id == state.export_previous_channel)) {
        frame.delta_ns = static_cast<std::int64_t>(hardware) -
                         static_cast<std::int64_t>(state.export_previous_hw);
    }
    state.export_previous_hw = hardware;
    state.export_previous_channel = record.message->channel_id;
    state.export_has_previous = true;
}

void enrich_export_frame(UiState& state, const CanRecordView& record, SessionFrameRecord& frame) {
    if (frame.co_decoded.empty()) {
        const auto decoded = export_decoded(state, record);
        if (decoded.has_value() && decoded->status != CoDecodeStatus::Invalid &&
            decoded->status != CoDecodeStatus::Unavailable) {
            frame.co_decoded = co_inspector_text(*decoded);
        }
    }
    enrich_export_time(state, record, frame);
}

void finish_export(UiState& state) {
    const auto progress = state.export_writer->progress();
    if (progress.enqueued > progress.written + progress.skipped) {
        return;
    }
    state.export_writer->finish();
    state.export_writer.reset();
    state.export_decoder.reset();
    state.export_router.reset();
    state.export_registry.reset();
    state.status = "export complete";
}

void pump_export(UiState& state) {
    if (state.export_writer == nullptr) {
        return;
    }
    while (state.export_cursor < state.export_seqs.size()) {
        const std::uint64_t seq = state.export_seqs[state.export_cursor];
        const auto record = state.source->find(seq);
        if (!record.has_value() || record->message == nullptr) {
            ++state.export_skipped;
            ++state.export_cursor;
            continue;
        }
        const std::string_view name = channel_name(state.channels, record->message->channel_id);
        if (record->is_frame()) {
            SessionFrameRecord frame =
                session_frame_from_view(*record, name, state.export_local_seq + 1U);
            enrich_export_frame(state, *record, frame);
            if (!state.export_writer->enqueue(frame)) {
                return;
            }
        } else {
            SessionGapRecord gap =
                session_gap_from_view(*record, name, state.export_local_seq + 1U);
            if (!state.export_writer->enqueue(gap)) {
                return;
            }
        }
        ++state.export_local_seq;
        ++state.export_cursor;
    }
    finish_export(state);
}

void begin_export(UiState& state, std::vector<std::uint64_t> seqs, std::string_view label) {
    if (state.export_writer != nullptr) {
        state.status = "export already active";
        return;
    }
    if (seqs.empty()) {
        state.status = "nothing selected to export";
        return;
    }
    state.export_seqs = std::move(seqs);
    state.export_cursor = 0U;
    state.export_skipped = 0U;
    state.export_local_seq = 0U;
    state.export_has_previous = false;
    state.export_previous_hw = 0U;
    state.export_previous_channel = 0U;
    state.export_total = state.export_seqs.size();
    state.export_writer = std::make_unique<AsyncExportWriter>();
    const auto format = static_cast<SessionFormat>(std::clamp(state.export_format, 0, 2));
    CanViewSessionHeader header = make_export_header(state);
    if (!state.export_writer->start(state.export_path, format, std::move(header)).has_value()) {
        state.export_writer.reset();
        state.status = "export could not start";
        return;
    }
    if (state.registry != nullptr) {
        state.export_registry = std::make_unique<CanOpenRegistry>(*state.registry);
        state.export_router = std::make_unique<CobIdRouter>();
        state.export_router->rebuild(*state.export_registry);
        state.export_decoder =
            std::make_unique<CanOpenDecoder>(*state.export_registry, *state.export_router);
    }
    state.status = std::string(label) + " exporting";
    pump_export(state);
}

void export_selected_keys(UiState& state, bool all_frames) {
    std::vector<std::uint64_t> seqs;
    if (all_frames) {
        for (std::uint64_t seq = state.source->head(); seq < state.source->tail(); ++seq) {
            const auto record = state.source->find(seq);
            if (!record.has_value() || record->message == nullptr || !record->is_frame()) {
                continue;
            }
            OverwriteKey key{};
            key.channel_id = record->message->channel_id;
            key.can_id = record->message->can_id;
            key.extended = record->message->extended;
            key.kind = record->message->kind;
            if (state.overwrite.is_key_selected(key)) {
                seqs.push_back(seq);
            }
        }
    } else {
        for (const auto& key : state.overwrite.selected_keys()) {
            if (const OverwriteRow* row = state.overwrite.find(key); row != nullptr) {
                seqs.push_back(row->last_seq);
            }
        }
    }
    begin_export(state, std::move(seqs),
                 all_frames ? "all frames for selected keys" : "selected keys");
}

void export_trace_selection(UiState& state, const CanView& view) {
    std::vector<std::uint64_t> seqs;
    for (std::uint64_t seq = state.source->head(); seq < state.source->tail(); ++seq) {
        if (!view.is_selected(seq)) {
            continue;
        }
        const auto record = state.source->find(seq);
        if (!record.has_value()) {
            continue;
        }
        if (!record->is_frame() && record->kind() == CanRecordKind::BusIdleHeartbeat) {
            continue;
        }
        seqs.push_back(seq);
    }
    begin_export(state, std::move(seqs), "selected records");
}

bool overwrite_row_visible(const UiState& state, const CanView& view, const OverwriteRow& row) {
    CanRecordView record{};
    record.seq = row.last_seq;
    record.message = &row.latest;
    record.meta = pack_frame_meta(CanRecordKind::Frame);
    if (state.router != nullptr && !row.latest.extended) {
        if (const auto route =
                state.router->lookup(row.latest.channel_id, row.latest.can_id, false);
            route.has_value()) {
            record.co_node_index = route->slot;
        }
    }
    return view.filter().matches(record);
}

void select_overwrite_range(UiState& state, const std::vector<const OverwriteRow*>& visible,
                            const OverwriteKey& key) {
    if (!state.overwrite_anchor.has_value()) {
        static_cast<void>(state.overwrite.toggle_key(key));
        state.overwrite_anchor = key;
        return;
    }
    std::size_t first = visible.size();
    std::size_t last = visible.size();
    for (std::size_t index = 0; index < visible.size(); ++index) {
        if (visible[index]->key == *state.overwrite_anchor) {
            first = index;
        }
        if (visible[index]->key == key) {
            last = index;
        }
    }
    if (first == visible.size() || last == visible.size()) {
        static_cast<void>(state.overwrite.toggle_key(key));
        state.overwrite_anchor = key;
        return;
    }
    state.overwrite.clear_selection();
    const std::size_t begin = std::min(first, last);
    const std::size_t end = std::max(first, last);
    for (std::size_t index = begin; index <= end; ++index) {
        state.overwrite.select_key(visible[index]->key);
    }
    state.overwrite_anchor = key;
}

void draw_overwrite_payload(const OverwriteRow& row, std::uint64_t now_ns) {
    for (std::size_t byte = 0U; byte < row.latest.dlc && byte < 64U; ++byte) {
        if (byte > 0U) {
            ImGui::SameLine(0.0F, 0.0F);
        }
        const bool fading = row.byte_fading(byte, now_ns, 1'500'000'000ULL);
        ImGui::TextColored(fading ? ImVec4(0.4F, 1.0F, 0.6F, 1.0F)
                                  : ImGui::GetStyleColorVec4(ImGuiCol_Text),
                           "%02x", static_cast<unsigned>(row.latest.data[byte]));
    }
}

void draw_overwrite(UiState& state, CanView& view) {
    if (ImGui::Button("Save selected")) {
        export_selected_keys(state, false);
    }
    ImGui::SameLine();
    if (ImGui::Button("All frames")) {
        export_selected_keys(state, true);
    }
    if (state.overwrite.gap_pending()) {
        ImGui::SameLine();
        if (ImGui::Button("Next gap")) {
            static_cast<void>(state.overwrite.consume_gap());
            view.set_table_mode(TableMode::Trace);
        }
    }
    std::vector<const OverwriteRow*> visible;
    visible.reserve(state.overwrite.rows().size());
    for (const auto& row : state.overwrite.rows()) {
        if (overwrite_row_visible(state, view, row)) {
            visible.push_back(&row);
        }
    }
    const std::uint64_t now_ns = monotonic_ns();
    constexpr ImGuiTableFlags flags = ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg |
                                      ImGuiTableFlags_ScrollY | ImGuiTableFlags_Resizable;
    if (!ImGui::BeginTable("overwrite", 10, flags)) {
        return;
    }
    ImGui::TableSetupColumn("Ch");
    ImGui::TableSetupColumn("ID");
    ImGui::TableSetupColumn("Count");
    ImGui::TableSetupColumn("Msg/s");
    ImGui::TableSetupColumn("dt cur");
    ImGui::TableSetupColumn("dt min");
    ImGui::TableSetupColumn("dt max");
    ImGui::TableSetupColumn("dt avg");
    ImGui::TableSetupColumn("Jitter");
    ImGui::TableSetupColumn("Data");
    ImGui::TableHeadersRow();
    for (const OverwriteRow* entry : visible) {
        const OverwriteRow& row = *entry;
        ImGui::TableNextRow();
        ImGui::TableSetColumnIndex(0);
        const bool selected = state.overwrite.is_key_selected(row.key);
        if (ImGui::Selectable("##row", selected, ImGuiSelectableFlags_SpanAllColumns)) {
            const ImGuiIO& io = ImGui::GetIO();
            if (io.KeyShift) {
                select_overwrite_range(state, visible, row.key);
            } else if (io.KeyCtrl) {
                static_cast<void>(state.overwrite.toggle_key(row.key));
                state.overwrite_anchor = row.key;
            } else {
                state.overwrite.clear_selection();
                state.overwrite.select_key(row.key);
                state.overwrite_anchor = row.key;
            }
            view.select_only(row.last_seq);
        }
        ImGui::SameLine();
        ImGui::Text("%u", row.key.channel_id);
        ImGui::TableSetColumnIndex(1);
        ImGui::Text("0x%x", row.key.can_id);
        ImGui::TableSetColumnIndex(2);
        ImGui::Text("%llu", static_cast<unsigned long long>(row.count));
        const auto stats = row.stats(now_ns);
        ImGui::TableSetColumnIndex(3);
        ImGui::Text("%llu", static_cast<unsigned long long>(stats.msg_per_s));
        ImGui::TableSetColumnIndex(4);
        ImGui::Text("%lld", static_cast<long long>(stats.delta_cur));
        ImGui::TableSetColumnIndex(5);
        ImGui::Text("%lld", static_cast<long long>(stats.delta_min));
        ImGui::TableSetColumnIndex(6);
        ImGui::Text("%lld", static_cast<long long>(stats.delta_max));
        ImGui::TableSetColumnIndex(7);
        ImGui::Text("%lld", static_cast<long long>(stats.delta_avg));
        ImGui::TableSetColumnIndex(8);
        ImGui::Text("%lld", static_cast<long long>(stats.jitter));
        ImGui::TableSetColumnIndex(9);
        draw_overwrite_payload(row, now_ns);
    }
    ImGui::EndTable();
}

void draw_view(UiState& state, CanView& view, const CanViewConfig& config) {
    const std::string title = "Can: " + view.title();
    if (!ImGui::Begin(title.c_str(), nullptr, ImGuiWindowFlags_NoCollapse)) {
        ImGui::End();
        return;
    }
    if (ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows)) {
        state.model->focus(view.id());
    }
    if (view.orphaned()) {
        ImGui::TextColored(ImVec4(1.0F, 0.55F, 0.2F, 1.0F), "binding missing; view orphaned");
    }
    draw_trace(state, view, config);
    if (view.table_mode() == TableMode::Overwrite) {
        draw_overwrite(state, view);
    }
    ImGui::End();
}

void draw_frame_tab(UiState& state, const std::optional<CanRecordView>& record,
                    const std::optional<std::uint64_t>& seq) {
    if (!record.has_value() || record->message == nullptr) {
        ImGui::TextDisabled("select a record");
        return;
    }
    const auto& message = *record->message;
    ImGui::Text("seq %llu", static_cast<unsigned long long>(*seq));
    ImGui::Text("channel %u  id 0x%x  dlc %u", message.channel_id, message.can_id,
                static_cast<unsigned>(message.dlc));
    ImGui::Text("hw_ts %llu", static_cast<unsigned long long>(message.hardware_timestamp_ns));
    if (!record->is_frame()) {
        ImGui::Text("kind %s reason=%s count=%llu exact=%s", to_string(record->kind()).data(),
                    to_string(meta_gap_reason(record->meta)).data(),
                    static_cast<unsigned long long>(record->gap_count()),
                    meta_count_exact(record->meta) ? "yes" : "at least");
    } else {
        ImGui::Text("data %s", payload_to_hex(message).c_str());
    }
    if (ImGui::Button("Copy data")) {
        ImGui::SetClipboardText(payload_to_hex(message).c_str());
        state.status = "record copied";
    }
}

void draw_bit_tab(UiState& state, CanView& view, const std::optional<CanRecordView>& record) {
    if (!record.has_value() || record->message == nullptr) {
        ImGui::TextDisabled("select a record");
        return;
    }
    state.bit_activity.build(view.source(), record->message->channel_id, record->message->can_id,
                             record->message->extended);
    if (!state.bit_activity.available()) {
        ImGui::TextDisabled("no frames");
        return;
    }
    ImGui::Text("frames=%u bytes=%zu low_confidence=%d", state.bit_activity.frame_count(),
                state.bit_activity.bytes(), state.bit_activity.low_confidence() ? 1 : 0);
    for (std::size_t byte = 0; byte < state.bit_activity.bytes(); ++byte) {
        char line[16]{};
        for (std::size_t bit = 0; bit < 8U; ++bit) {
            line[bit] = state.bit_activity.at(byte, bit).transitions > 0U ? '#' : '.';
        }
        ImGui::Text("%02zu: %s", byte, line);
    }
}

struct DecodedSelection {
    std::optional<CoDecoded> decoded{};
    std::optional<OfflineDecodeSource> source{};
    bool hash_mismatch{false};
};

DecodedSelection load_decoded(UiState& state, const std::optional<std::uint64_t>& seq,
                              const std::optional<CanRecordView>& record) {
    DecodedSelection out{};
    if (state.offline != nullptr && seq.has_value() && state.import_result.has_value()) {
        if (const auto it = state.offline_index.find(*seq);
            it != state.offline_index.end() && it->second < state.import_result->records.size()) {
            const auto offline = state.offline->decode(state.import_result->records[it->second]);
            if (offline.source != OfflineDecodeSource::None) {
                out.decoded = offline.decoded;
                out.source = offline.source;
                out.hash_mismatch = offline.hash_mismatch;
            }
        }
    }
    if (!out.decoded.has_value() && state.decoder != nullptr && record.has_value()) {
        out.decoded = state.decoder->decode(*record, monotonic_ns());
    }
    return out;
}

void draw_canopen_tab(const DecodedSelection& selection, const NodeBinding* binding) {
    if (!selection.decoded.has_value() ||
        selection.decoded->status == CoDecodeStatus::Unavailable ||
        selection.decoded->status == CoDecodeStatus::Invalid) {
        ImGui::TextDisabled("no CANopen route for this record");
        return;
    }
    if (selection.source.has_value()) {
        ImGui::Text("source: %s", *selection.source == OfflineDecodeSource::Exported
                                      ? "exported"
                                      : "rebuilt from EDS");
        if (selection.hash_mismatch) {
            ImGui::TextColored(ImVec4(1.0F, 0.7F, 0.2F, 1.0F), "EDS hash mismatch");
        }
    }
    if (binding != nullptr) {
        const std::uint64_t now_ns = monotonic_ns();
        const std::uint64_t age = binding->heartbeat_seen && now_ns > binding->last_heartbeat_ns
                                      ? now_ns - binding->last_heartbeat_ns
                                      : 0U;
        ImGui::Text("NMT %s heartbeat %s age %llu ms", to_string(binding->nmt_state).data(),
                    binding->stale(now_ns) ? "stale" : "fresh", age / 1'000'000ULL);
    }
    ImGui::TextWrapped("%s", co_inspector_text(*selection.decoded).c_str());
    if (selection.decoded->mapping_stale) {
        ImGui::TextColored(ImVec4(1.0F, 0.7F, 0.2F, 1.0F), "mapping epoch stale");
    }
}

const NodeBinding* inspector_binding(UiState& state, const std::optional<CanRecordView>& record,
                                     const DecodedSelection& decoded) {
    if (record.has_value() && record->message != nullptr && state.registry != nullptr &&
        record->co_node_index < CanOpenRegistry::capacity) {
        if (const NodeBinding* binding = state.registry->at(record->co_node_index);
            binding != nullptr) {
            return binding;
        }
    }
    if (record.has_value() && record->message != nullptr && state.offline != nullptr &&
        decoded.decoded.has_value()) {
        return state.offline->registry().find(record->message->channel_id,
                                              decoded.decoded->node_id);
    }
    return nullptr;
}

void draw_inspector(UiState& state) {
    if (!ImGui::Begin("Inspector")) {
        ImGui::End();
        return;
    }
    CanView* view = state.model->focused();
    if (view == nullptr) {
        ImGui::TextDisabled("no focused view");
        ImGui::End();
        return;
    }
    const auto seq = view->last_selected();
    const auto record = seq.has_value() ? view->source().find(*seq) : std::nullopt;
    const DecodedSelection decoded = load_decoded(state, seq, record);
    const NodeBinding* binding = inspector_binding(state, record, decoded);
    if (ImGui::BeginTabBar("inspector-tabs")) {
        if (ImGui::BeginTabItem("Frame")) {
            draw_frame_tab(state, record, seq);
            ImGui::EndTabItem();
        }
        if (ImGui::BeginTabItem("CANopen")) {
            draw_canopen_tab(decoded, binding);
            ImGui::EndTabItem();
        }
        if (ImGui::BeginTabItem("Bit Activity")) {
            draw_bit_tab(state, *view, record);
            ImGui::EndTabItem();
        }
        ImGui::EndTabBar();
    }
    ImGui::End();
}

void draw_export_controls(UiState& state, CanView& view) {
    ImGui::SeparatorText("Export");
    ImGui::SetNextItemWidth(260.0F);
    ImGui::InputText("Path##export", state.export_path, sizeof(state.export_path));
    ImGui::SameLine();
    ImGui::SetNextItemWidth(90.0F);
    ImGui::Combo("Format", &state.export_format, "JSONL\0CSV\0TXT\0");
    if (ImGui::Button("Save selected")) {
        if (view.table_mode() == TableMode::Overwrite) {
            export_selected_keys(state, false);
        } else {
            export_trace_selection(state, view);
        }
    }
    if (view.table_mode() == TableMode::Overwrite) {
        ImGui::SameLine();
        if (ImGui::Button("All frames")) {
            export_selected_keys(state, true);
        }
    }
    if (state.export_writer != nullptr) {
        const auto progress = state.export_writer->progress();
        const std::uint64_t skipped = progress.skipped + state.export_skipped;
        ImGui::Text("export %llu/%llu skipped=%llu",
                    static_cast<unsigned long long>(progress.written),
                    static_cast<unsigned long long>(state.export_total),
                    static_cast<unsigned long long>(skipped));
        ImGui::SameLine();
        if (ImGui::Button("Cancel export")) {
            state.export_writer->cancel();
            state.export_writer.reset();
            state.export_decoder.reset();
            state.export_router.reset();
            state.export_registry.reset();
            state.status = "export cancelled";
        }
    }
}

void draw_status_channels(UiState& state) {
    for (const auto& channel : state.channels) {
        const UiState::LiveStatus& live = state.live_status[channel.channel_id % 32];
        const char* state_text = "waiting";
        if (state.capture == nullptr && !live.seen) {
            state_text = "offline";
        } else if (!channel.enabled) {
            state_text = "disabled";
        } else if (live.seen) {
            state_text = "live";
        }
        ImGui::Text("channel %s id=%u %s", channel.name.c_str(), channel.channel_id, state_text);
        if (live.seen) {
            ImGui::Text("  status %s rx=%llu tx=%llu drop=%llu",
                        nexigen::hw::can::to_string(live.status).c_str(),
                        static_cast<unsigned long long>(live.status.rx_count),
                        static_cast<unsigned long long>(live.status.tx_count),
                        static_cast<unsigned long long>(live.status.dropped_count));
        }
        if (channel.nominal_bitrate == 0U && channel.data_bitrate == 0U) {
            ImGui::TextColored(ImVec4(1.0F, 0.7F, 0.2F, 1.0F), "  bitrate: n/a");
        }
    }
}

void draw_status_io(UiState& state) {
    if (state.import_result.has_value()) {
        for (const auto& channel : state.import_result->header.channels) {
            if (const auto* status =
                    SessionImporter::status_for(*state.import_result, channel.channel_id);
                status != nullptr) {
                ImGui::Text("imported status %s: %s rx=%llu tx=%llu", channel.name.c_str(),
                            nexigen::hw::can::to_string(status->status).c_str(),
                            static_cast<unsigned long long>(status->status.rx_count),
                            static_cast<unsigned long long>(status->status.tx_count));
            }
        }
    }
    if (state.import_running) {
        ImGui::Text("import: running");
    } else if (state.import_result.has_value()) {
        ImGui::Text("import: %zu records, malformed=%zu", state.import_result->records.size(),
                    state.import_result->malformed);
    }
    if (CanView* focused = state.model->focused(); focused != nullptr) {
        const auto progress = focused->index().progress();
        if (progress.active) {
            ImGui::Text("filter rebuild %llu/%llu",
                        static_cast<unsigned long long>(progress.scanned),
                        static_cast<unsigned long long>(progress.total));
        }
        if (focused->find().rebuilding()) {
            ImGui::Text("find %zu/%zu", focused->find().scanned(), focused->find().total());
        }
        draw_export_controls(state, *focused);
    }
}

void draw_status(UiState& state, const CanViewConfig& /*config*/) {
    if (!ImGui::Begin("Status")) {
        ImGui::End();
        return;
    }
    CaptureStats capture_stats{};
    if (state.capture != nullptr) {
        capture_stats = state.capture->stats();
    }
    StatusSources sources{};
    sources.capture = state.capture != nullptr ? &capture_stats : nullptr;
    sources.ring = state.ring.get();
    sources.overwrite = &state.overwrite;
    sources.eds = state.eds_library.get();
    sources.reindex = state.reindexer.get();
    sources.export_writer = state.export_writer.get();
    sources.import = state.import_result.has_value() ? &*state.import_result : nullptr;
    sources.rejected = &state.rejected;
    const auto snapshot = collect_status(sources);
    ImGui::TextWrapped("%s", snapshot.to_string().c_str());
    ImGui::Text("time base: %s",
                nexigen::chrono::TimeBaseClock::attached() ? "attached" : "unavailable");
    ImGui::Text("ECO: %s trigger=%s cpu=%.1f%% rate=%.0f", to_string(state.guard.mode()).data(),
                to_string(state.guard.trigger()).data(), state.guard.cpu_percent(),
                state.guard.rate_ewma());
    if (state.plan.degraded) {
        ImGui::TextColored(ImVec4(1.0F, 0.7F, 0.2F, 1.0F), "degraded 16 MiB mode");
    }
    ImGui::Text("plan: ring=%zu paused=%zu overwrite=%zu heatmap=%zu eds=%d",
                state.plan.ring_capacity, state.plan.paused_snapshots, state.plan.overwrite_rows,
                state.plan.heatmap_columns, state.plan.eds_enabled ? 1 : 0);
    if (state.shared_time_warning) {
        ImGui::TextColored(ImVec4(1.0F, 0.7F, 0.2F, 1.0F), "shared timebase: clocks diverge");
    }
    if (!state.clone_summary.empty()) {
        ImGui::TextColored(ImVec4(1.0F, 0.7F, 0.2F, 1.0F), "clone: %s",
                           state.clone_summary.c_str());
    }
    draw_status_channels(state);
    draw_status_io(state);
    ImGui::End();
}

void rebuild_heatmap(UiState& state, const CanViewConfig& /*config*/) {
    const std::uint64_t now = monotonic_ns();
    if (now - state.last_heatmap_ns <= 250'000'000ULL || state.ring->tail() <= state.ring->head()) {
        return;
    }
    state.last_heatmap_ns = now;
    std::array<ChannelTiming, 32> timings{};
    for (const auto& channel : state.channels) {
        timings[channel.channel_id % 32] =
            ChannelTiming{channel.nominal_bitrate, channel.data_bitrate, channel.fd};
    }
    std::uint64_t newest = 0U;
    if (const auto record = state.ring->find(state.ring->tail() - 1U);
        record.has_value() && record->message != nullptr) {
        newest = record->message->hardware_timestamp_ns;
    }
    const std::uint64_t start = newest > 10'000'000'000ULL ? newest - 10'000'000'000ULL : 0U;
    state.heatmap.rebuild(*state.source, &state.time, HeatmapAxis::SharedHardware, start,
                          newest + 1U, state.heatmap_channel, timings);
}

void draw_heatmap_controls(UiState& state) {
    if (!state.channels.empty()) {
        int selected = -1;
        for (std::size_t index = 0; index < state.channels.size(); ++index) {
            if (state.channels[index].channel_id == state.heatmap_channel) {
                selected = static_cast<int>(index);
            }
        }
        ImGui::SetNextItemWidth(140.0F);
        if (ImGui::BeginCombo("Channel",
                              selected >= 0
                                  ? state.channels[static_cast<std::size_t>(selected)].name.c_str()
                                  : "?")) {
            for (std::size_t index = 0; index < state.channels.size(); ++index) {
                const bool active = state.channels[index].channel_id == state.heatmap_channel;
                if (ImGui::Selectable(state.channels[index].name.c_str(), active)) {
                    state.heatmap_channel = state.channels[index].channel_id;
                    state.last_heatmap_ns = 0U;
                }
            }
            ImGui::EndCombo();
        }
        ImGui::SameLine();
    }
    int axis = static_cast<int>(state.heatmap.axis());
    if (ImGui::Combo("Axis", &axis, "UTC\0HW lanes\0Shared HW\0")) {
        state.heatmap.set_axis(static_cast<HeatmapAxis>(axis));
        state.last_heatmap_ns = 0U;
    }
    ImGui::SameLine();
    if (ImGui::Button("Step -")) {
        state.heatmap.step_finer();
        state.last_heatmap_ns = 0U;
    }
    ImGui::SameLine();
    if (ImGui::Button("Step +")) {
        state.heatmap.step_coarser();
        state.last_heatmap_ns = 0U;
    }
    ImGui::SameLine();
    bool follow = state.heatmap.follow();
    if (ImGui::Checkbox("Follow", &follow)) {
        state.heatmap.set_follow(follow);
    }
    ImGui::Text("effective step: %.3f ms (requested %.3f ms)",
                static_cast<double>(state.heatmap.effective_step_ns()) / 1'000'000.0,
                static_cast<double>(state.heatmap.requested_step_ns()) / 1'000'000.0);
}

void draw_heatmap_plot(UiState& state) {
    std::vector<double> values(state.heatmap.column_count(),
                               std::numeric_limits<double>::quiet_NaN());
    std::size_t missing = 0U;
    for (std::size_t column = 0; column < values.size(); ++column) {
        const auto load = state.heatmap.load(column, state.heatmap_channel);
        if (load.missing_bitrate && !load.measured_available) {
            ++missing;
            continue;
        }
        values[column] = load.estimated_available ? load.estimated : load.measured;
    }
    if (missing > 0U) {
        ImGui::TextColored(ImVec4(1.0F, 0.7F, 0.2F, 1.0F),
                           "n/a: %zu column(s) have no configured bitrate", missing);
    }
    if (!ImPlot::BeginPlot("Bus load", ImVec2(-1, 180))) {
        return;
    }
    ImPlot::PlotHeatmap("load", values.data(), 1, static_cast<int>(values.size()), 0.0, 1.0,
                        nullptr, ImPlotPoint{0.0, 0.0},
                        ImPlotPoint{static_cast<double>(values.size()), 1.0});
    if (ImPlot::IsPlotHovered()) {
        const double mouse = ImPlot::GetPlotMousePos().x;
        if (mouse >= 0.0 && mouse < static_cast<double>(values.size())) {
            const auto column = static_cast<std::size_t>(mouse);
            const auto load = state.heatmap.load(column, state.heatmap_channel);
            if (load.missing_bitrate && !load.measured_available) {
                ImGui::SetTooltip("n/a: missing bitrate");
            } else if (ImGui::IsMouseClicked(0)) {
                if (const auto target = state.heatmap.click_record(column); target.has_value()) {
                    if (CanView* focused = state.model->focused(); focused != nullptr) {
                        static_cast<void>(focused->focus_record(*target));
                    }
                }
            }
        }
    }
    ImPlot::EndPlot();
}

void draw_heatmap(UiState& state, const CanViewConfig& config) {
    if (!ImGui::Begin("Heatmap")) {
        ImGui::End();
        return;
    }
    rebuild_heatmap(state, config);
    draw_heatmap_controls(state);
    if (state.heatmap.column_count() == 0U) {
        ImGui::TextDisabled("no bus-load data");
    } else {
        draw_heatmap_plot(state);
    }
    ImGui::End();
}

void draw_clone_modal(UiState& state) {
    if (!state.clone_modal) {
        return;
    }
    ImGui::OpenPopup("Clone view");
    if (ImGui::BeginPopupModal("Clone view", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
        ImGui::Checkbox("Selection", &state.clone_options.selection);
        ImGui::Checkbox("Bookmarks", &state.clone_options.bookmarks);
        ImGui::Checkbox("T0", &state.clone_options.t0);
        ImGui::Checkbox("Filter", &state.clone_options.filter);
        ImGui::Checkbox("Table/time", &state.clone_options.table_time);
        if (ImGui::Button("Create")) {
            if (CanView* focused = state.model->focused(); focused != nullptr) {
                CloneAdjustments adjustments{};
                const auto clone =
                    state.model->clone_view(focused->id(), state.clone_paused,
                                            state.clone_paused ? "PAUSED clone" : "LIVE clone",
                                            state.clone_options, ViewDefaults{}, &adjustments);
                state.status = clone.has_value() ? "clone created" : "clone limit reached";
                state.clone_summary = adjustments.any() ? adjustments.to_string() : "";
                if (!state.clone_summary.empty()) {
                    state.status += ": " + state.clone_summary;
                }
            }
            state.clone_modal = false;
            ImGui::CloseCurrentPopup();
        }
        ImGui::SameLine();
        if (ImGui::Button("Cancel")) {
            state.clone_modal = false;
            ImGui::CloseCurrentPopup();
        }
        ImGui::EndPopup();
    }
}

void handle_view_hotkeys(CanView& view, UiState& state) {
    if (ImGui::IsKeyPressed(ImGuiKey_Space)) {
        view.set_paused(!view.paused());
    }
    if (ImGui::IsKeyPressed(ImGuiKey_M)) {
        view.set_table_mode(view.table_mode() == TableMode::Trace ? TableMode::Overwrite
                                                                  : TableMode::Trace);
    }
    if (ImGui::IsKeyPressed(ImGuiKey_F2)) {
        if (const auto seq = view.last_selected(); seq.has_value()) {
            static_cast<void>(view.toggle_bookmark(*seq));
        }
    }
    if (ImGui::IsKeyPressed(ImGuiKey_T)) {
        if (const auto seq = view.last_selected(); seq.has_value()) {
            if (const auto record = view.source().find(*seq); record.has_value()) {
                view.set_t0(*record);
            }
        }
    }
    if (ImGui::IsKeyPressed(ImGuiKey_G)) {
        view.set_table_mode(TableMode::Trace);
        static_cast<void>(state.overwrite.consume_gap());
    }
}

bool nav_record_matches(const CanView& view, const CanRecordView& record, bool want_error) {
    if (!want_error) {
        return view.is_bookmarked(record.seq);
    }
    if (record.message == nullptr) {
        return false;
    }
    return record.message->kind == nexigen::hw::can::CanMessageKind::Error ||
           record.message->kind == nexigen::hw::can::CanMessageKind::Overload ||
           has_flag(record.meta, RecordFlags::InvalidStruct) ||
           (record.is_frame() &&
            TimePresenter::is_anomalous(record.delta_same_id_ns, view.anomaly_threshold_ns()));
}

void jump_to_nav_match(CanView& view, bool want_error, bool backward) {
    const std::size_t count = view.display_count();
    for (std::size_t step = 1U; step <= count; ++step) {
        const std::size_t order =
            backward ? (view.scroll() + count - step) % count : (view.scroll() + step) % count;
        const std::uint64_t seq = view.display_at(order);
        const auto record = view.source().find(seq);
        if (record.has_value() && nav_record_matches(view, *record, want_error)) {
            view.select_only(seq);
            view.set_scroll(order);
            return;
        }
    }
}

void handle_nav_hotkeys(CanView& view) {
    const std::size_t count = view.display_count();
    if (count == 0U) {
        return;
    }
    if (ImGui::IsKeyPressed(ImGuiKey_F3) || ImGui::IsKeyPressed(ImGuiKey_F4)) {
        const bool want_error = ImGui::IsKeyPressed(ImGuiKey_F4);
        if (view.table_mode() == TableMode::Overwrite) {
            view.set_table_mode(TableMode::Trace);
        }
        jump_to_nav_match(view, want_error, ImGui::GetIO().KeyShift);
    }
    const ImGuiIO& io = ImGui::GetIO();
    if (io.KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_Home)) {
        view.set_scroll(0U);
    }
    if (io.KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_End)) {
        view.set_scroll(count - 1U);
    }
}

void handle_clipboard_hotkeys(UiState& state, CanView& view) {
    const ImGuiIO& io = ImGui::GetIO();
    if (io.KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_C)) {
        if (const auto seq = view.last_selected(); seq.has_value()) {
            if (const auto record = view.source().find(*seq);
                record.has_value() && record->message != nullptr) {
                ImGui::SetClipboardText(payload_to_hex(*record->message).c_str());
                state.status = "record copied";
            }
        }
    }
    if (io.KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_F)) {
        state.focus_find = true;
    }
    if (!io.KeyCtrl || !ImGui::IsKeyPressed(ImGuiKey_A)) {
        return;
    }
    if (view.table_mode() == TableMode::Overwrite) {
        for (const auto& row : state.overwrite.rows()) {
            if (overwrite_row_visible(state, view, row)) {
                state.overwrite.select_key(row.key);
            }
        }
    } else {
        view.select_all_displayed();
    }
}

void handle_hotkeys(UiState& state) {
    CanView* view = state.model->focused();
    if (view == nullptr || ImGui::GetIO().WantTextInput) {
        return;
    }
    handle_view_hotkeys(*view, state);
    handle_nav_hotkeys(*view);
    handle_clipboard_hotkeys(state, *view);
}

void draw_splash(const SplashImage& image) {
    const ImGuiViewport* viewport = ImGui::GetMainViewport();
    ImDrawList* draw = ImGui::GetBackgroundDrawList();
    const float width = static_cast<float>(image.width());
    const float height = static_cast<float>(image.height());
    const float scale = std::min(viewport->Size.x / width, viewport->Size.y / height);
    const ImVec2 size{width * scale, height * scale};
    const ImVec2 position{(viewport->Size.x - size.x) * 0.5F, (viewport->Size.y - size.y) * 0.5F};
    draw->AddRectFilled(ImVec2(0, 0), viewport->Size, IM_COL32(8, 10, 12, 255));
    draw->AddImage(static_cast<ImTextureID>(image.texture()), position,
                   ImVec2(position.x + size.x, position.y + size.y));
}

bool init_window(GLFWwindow*& window, float scale, bool hidden, const std::string& ini_path) {
    if (glfwInit() == GLFW_FALSE) {
        std::fprintf(stderr, "canview: cannot initialise GLFW\n");
        return false;
    }
    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 3);
    glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);
    glfwWindowHint(GLFW_VISIBLE, hidden ? GLFW_FALSE : GLFW_TRUE);
    window = glfwCreateWindow(static_cast<int>(1600.0F * scale), static_cast<int>(900.0F * scale),
                              "nexigen-canview", nullptr, nullptr);
    if (window == nullptr) {
        glfwTerminate();
        return false;
    }
    glfwMakeContextCurrent(window);
    glfwSwapInterval(1);
    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImPlot::CreateContext();
    ImGuiIO& io = ImGui::GetIO();
    io.ConfigFlags |= ImGuiConfigFlags_DockingEnable;
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
    io.IniFilename = ini_path.c_str();
    apply_dark_theme();
    ImGui::GetStyle().ScaleAllSizes(scale);
    ImFontConfig font_config{};
    font_config.FontDataOwnedByAtlas = false;
    io.Fonts->AddFontFromMemoryTTF(const_cast<unsigned char*>(assets::kJetBrainsMono.data()),
                                   static_cast<int>(assets::kJetBrainsMono.size()), 16.0F * scale,
                                   &font_config);
    ImGui_ImplGlfw_InitForOpenGL(window, true);
    ImGui_ImplOpenGL3_Init("#version 330");
    return true;
}

void shutdown_window(GLFWwindow* window) {
    ImGui_ImplOpenGL3_Shutdown();
    ImGui_ImplGlfw_Shutdown();
    ImPlot::DestroyContext();
    ImGui::DestroyContext();
    if (window != nullptr) {
        glfwDestroyWindow(window);
    }
    glfwTerminate();
}

void begin_frame() {
    ImGui_ImplOpenGL3_NewFrame();
    ImGui_ImplGlfw_NewFrame();
    ImGui::NewFrame();
}

void end_frame(GLFWwindow* window) {
    ImGui::Render();
    int width = 0;
    int height = 0;
    glfwGetFramebufferSize(window, &width, &height);
    glViewport(0, 0, width, height);
    const ImVec4 clear{0.05F, 0.055F, 0.065F, 1.0F};
    glClearColor(clear.x, clear.y, clear.z, clear.w);
    glClear(GL_COLOR_BUFFER_BIT);
    ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
    glfwSwapBuffers(window);
}

} // namespace

float resolve_scale(const CliOptions& options) {
    if (options.ui_scale > 0.0F) {
        return options.ui_scale;
    }
    float scale_x = 1.0F;
    if (GLFWmonitor* monitor = glfwGetPrimaryMonitor(); monitor != nullptr) {
        float scale_y = 1.0F;
        glfwGetMonitorContentScale(monitor, &scale_x, &scale_y);
        scale_x = scale_x > scale_y ? scale_x : scale_y;
    }
    return std::clamp(scale_x, 1.0F, 4.0F);
}

std::string layout_ini_path() {
    std::string path = default_config_path();
    if (const std::size_t slash = path.find_last_of('/'); slash != std::string::npos) {
        path.resize(slash);
        std::error_code error;
        std::filesystem::create_directories(path, error);
        return path + "/canview-layout.ini";
    }
    return "canview-layout.ini";
}

bool show_splash(GLFWwindow* window, const CliOptions& options,
                 std::optional<SplashImage>& splash) {
    if (!splash.has_value() && !options.ui_splash_test) {
        return false;
    }
    const auto started = std::chrono::steady_clock::now();
    std::size_t frames = 0U;
    while (std::chrono::steady_clock::now() - started < std::chrono::seconds(1)) {
        glfwPollEvents();
        begin_frame();
        if (splash.has_value()) {
            draw_splash(*splash);
        }
        end_frame(window);
        ++frames;
    }
    if (!options.ui_splash_test) {
        return false;
    }
    std::printf("UI_SPLASH texture=%d width=%d height=%d frames=%zu elapsed_ms=1000 fallback=%d\n",
                splash.has_value() && splash->valid() ? 1 : 0,
                splash.has_value() ? splash->width() : 0, splash.has_value() ? splash->height() : 0,
                frames, splash.has_value() ? 0 : 1);
    return true;
}

void observe_resources(UiState& state, std::uint64_t now) {
    const std::uint64_t cpu = process_cpu_ns();
    std::uint64_t rate = 0U;
    const std::uint64_t received = state.ring->tail();
    if (state.last_rate_time != 0U && now > state.last_rate_time) {
        rate = (received - state.last_rate_count) * 1'000'000'000ULL / (now - state.last_rate_time);
    }
    state.last_rate_count = received;
    state.last_rate_time = now;
    state.last_cpu_ns = cpu;
    static_cast<void>(state.guard.observe(now, cpu, rate));
}

void step_background(UiState& state) {
    if (CanView* focused = state.model->focused(); focused != nullptr) {
        pump_find(*focused);
    }
    pump_import(state);
    pump_export(state);
    if (state.reindexer != nullptr && state.reindexer->active()) {
        static_cast<void>(state.reindexer->step(*state.ring));
        if (!state.reindexer->active()) {
            refresh_registry_views(state);
        }
    }
}

std::size_t workspace_loop(UiState& state, const CliOptions& options, const CanViewConfig& config,
                           GLFWwindow* window) {
    std::size_t frames = 0U;
    while (!glfwWindowShouldClose(window) && !g_stop.load(std::memory_order_relaxed)) {
        glfwPollEvents();
        const std::uint64_t now = monotonic_ns();
        observe_resources(state, now);
        ingest_ingress(state, now);
        step_background(state);
        begin_frame();
        ImGui::DockSpaceOverViewport(0, ImGui::GetMainViewport(),
                                     ImGuiDockNodeFlags_PassthruCentralNode);
        if (ImGui::IsAnyItemActive() || ImGui::GetIO().WantTextInput) {
            state.guard.note_input();
        }
        const bool recompute = state.guard.recompute_due(now);
        if (recompute) {
            state.model->step_rebuilds();
        }
        handle_hotkeys(state);
        draw_toolbar(state, config, options);
        draw_channels(state, config);
        draw_clone_modal(state);
        for (const auto& entry : state.model->views()) {
            if (entry->live()) {
                draw_view(state, *entry, config);
            }
        }
        draw_inspector(state);
        draw_status(state, config);
        if (recompute) {
            draw_heatmap(state, config);
        }
        end_frame(window);
        ++frames;
        if (state.guard.mode() == ResourceMode::Eco) {
            std::this_thread::sleep_for(std::chrono::milliseconds(50));
        }
        if (options.ui_smoke && frames >= 2U) {
            break;
        }
    }
    return frames;
}

void shutdown_state(UiState& state) {
    if (state.import_running) {
        state.import_cancel.store(true, std::memory_order_relaxed);
        if (state.import_future.valid()) {
            state.import_future.wait();
        }
        state.import_running = false;
    }
    if (state.export_writer != nullptr) {
        state.export_writer->cancel();
        state.export_writer.reset();
        state.export_decoder.reset();
        state.export_router.reset();
        state.export_registry.reset();
    }
    if (state.capture != nullptr) {
        state.capture->stop();
    }
}

int run_gui(const CliOptions& options, CanViewConfig& config) {
    UiState state{};
    if (!initialize(state, options, config)) {
        std::fprintf(stderr, "canview: %s\n", state.status.c_str());
        return 1;
    }
    if (options.ui_actions_test || options.ui_view_test || options.ui_focus_test ||
        options.ui_input_test) {
        return run_model_script(options, state);
    }
    const float scale = resolve_scale(options);
    const std::string layout_path = layout_ini_path();
    GLFWwindow* window = nullptr;
    if (!init_window(window, scale, options.ui_smoke || options.ui_splash_test, layout_path)) {
        return 1;
    }
    std::signal(SIGINT, on_signal);
    std::signal(SIGTERM, on_signal);
    std::signal(SIGHUP, on_signal);
    g_stop.store(false, std::memory_order_relaxed);
    if (options.ui_scale > 0.0F) {
        std::printf("UI_SCALE scale=%.2f\n", static_cast<double>(options.ui_scale));
    }
    std::optional<SplashImage> splash;
    if (auto loaded = SplashImage::load_embedded(); loaded) {
        splash.emplace(std::move(*loaded));
    }
    const bool splash_only = show_splash(window, options, splash);
    splash.reset();
    if (!splash_only) {
        const std::size_t frames = workspace_loop(state, options, config, window);
        if (options.ui_smoke) {
            std::printf("UI_SMOKE views=%zu frames=%zu ready=1\n", state.model->views().size(),
                        frames);
        }
    }
    shutdown_state(state);
    config.views = state.model->to_json();
    shutdown_window(window);
    return 0;
}

} // namespace nexigen::canview
