#include "logview_window.hpp"

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <ctime>
#include <filesystem>
#include <format>
#include <memory>
#include <string>
#include <string_view>
#include <system_error>

#include <GL/gl.h>
#include <GLFW/glfw3.h>

#include <imgui.h>
#include <imgui_internal.h>
#include <backends/imgui_impl_glfw.h>
#include <backends/imgui_impl_opengl3.h>

#include "backend/capture_thread.hpp"
#include "importer.hpp"
#include "logview_config.hpp"
#include "ui_actions.hpp"
#include "resource_guard.hpp"
#include "time_formatter.hpp"
#include "theme.hpp"
#include "ui_scale.hpp"

#if __has_include("jetbrains_mono_asset.hpp")
#include "jetbrains_mono_asset.hpp"
#define NEXIGEN_LOGVIEW_HAS_FONT 1
#else
#define NEXIGEN_LOGVIEW_HAS_FONT 0
#endif

namespace nexigen::logview {

namespace {

constexpr const char* kLevelNames[] = {"Verbose", "Debug", "Info", "Warning",
                                       "Error",   "Fatal", "Off"};

struct UiState {
    CaptureThread* capture{nullptr};
    CaptureStats capture_stats{};
    std::string status{};
    std::string ini_path{};
    char filter_buf[512]{};
    char find_buf[128]{};
    char open_path[512]{};
    char save_path[512]{};
    bool auto_scroll{true};
    bool open_save{false};
    bool open_open{false};
    bool open_colors{false};
    ExportFormat save_format{ExportFormat::Jsonl};
    std::optional<std::uint64_t> scroll_to{};
    std::uint64_t last_rejected{0};
    int smoke_frames{0};
    bool first_frame{true};
    bool live{true};
    bool focus_find{false};
    bool input_test{false};
    int input_phase{0};
    int input_phase_frames{0};
    bool capture_copy_rect{false};
    bool copy_rect_valid{false};
    ImVec2 copy_rect_min{};
    ImVec2 copy_rect_max{};
    bool capture_open_modal_rect{false};
    bool open_modal_rect_valid{false};
    ImVec2 open_modal_rect_min{};
    ImVec2 open_modal_rect_max{};
    bool copied{false};
    bool truncated{false};
    bool open_clicked{false};
    bool layout_loaded{false};
    bool inject_mouse_pos{false};
    bool inject_mouse_down{false};
    bool inject_mouse_up{false};
    ImVec2 inject_pos{};
};

ImVec4 to_imgui(const Rgba& color) {
    return ImVec4(color.r, color.g, color.b, color.a);
}

std::uint64_t monotonic_now_ns() noexcept {
    timespec ts{};
    if (::clock_gettime(CLOCK_MONOTONIC_RAW, &ts) != 0) {
        return 0;
    }
    return static_cast<std::uint64_t>(ts.tv_sec) * 1'000'000'000ULL +
           static_cast<std::uint64_t>(ts.tv_nsec);
}

std::uint64_t process_cpu_ns() noexcept {
    timespec ts{};
    if (::clock_gettime(CLOCK_PROCESS_CPUTIME_ID, &ts) != 0) {
        return 0;
    }
    return static_cast<std::uint64_t>(ts.tv_sec) * 1'000'000'000ULL +
           static_cast<std::uint64_t>(ts.tv_nsec);
}

std::string parent_directory(const std::string& path) {
    const std::size_t slash = path.find_last_of('/');
    return slash == std::string::npos ? std::string(".") : path.substr(0, slash);
}

void draw_toolbar_pause(LogViewModel& model, UiState& state) {
    if (ImGui::Button(model.paused() ? "Resume" : "Pause")) {
        model.set_paused(!model.paused());
        state.auto_scroll = !model.paused();
    }
    ImGui::SameLine();
    if (model.paused()) {
        ImGui::TextColored(ImVec4(1.0F, 0.65F, 0.25F, 1.0F), "PAUSED (%llu new)",
                           static_cast<unsigned long long>(model.new_since_pause()));
        if (model.overwritten_while_paused() > 0U) {
            ImGui::SameLine();
            ImGui::TextColored(ImVec4(1.0F, 0.40F, 0.40F, 1.0F), "(%llu overwritten)",
                               static_cast<unsigned long long>(model.overwritten_while_paused()));
        }
    } else {
        ImGui::TextColored(ImVec4(0.36F, 0.85F, 0.58F, 1.0F), "LIVE");
    }
}

void draw_toolbar_actions(LogViewModel& model, UiState& state) {
    ImGui::SameLine();
    if (ImGui::Button("Clear")) {
        model.clear_history();
        state.auto_scroll = true;
    }
    ImGui::SameLine();
    if (ImGui::Button("Save selected")) {
        state.open_save = true;
    }
    ImGui::SameLine();
    if (ImGui::Button("Open")) {
        state.open_open = true;
    }
    ImGui::SameLine();
    if (ImGui::Button("Colors")) {
        state.open_colors = true;
    }
    ImGui::SameLine();
    if (ImGui::Button("Reset colors")) {
        model.reset_colors();
    }
    ImGui::SameLine();
    if (ImGui::Button("Reset config")) {
        model.reset_configuration();
        state.filter_buf[0] = '\0';
        state.find_buf[0] = '\0';
        state.auto_scroll = true;
        state.status = model.restart_required()
                           ? "configuration reset; restart applies service/memory/thread defaults"
                           : "configuration reset";
    }
    if (model.restart_required()) {
        ImGui::SameLine();
        ImGui::TextColored(ImVec4(1.0F, 0.55F, 0.30F, 1.0F), "RESTART");
        if (ImGui::IsItemHovered()) {
            ImGui::SetTooltip("Service, memory budget, or thread placement changed; "
                              "restart the viewer to apply them.");
        }
    }
}

void draw_toolbar_modes(LogViewModel& model, UiState& state) {
    ImGui::SameLine();
    int timestamp_mode = static_cast<int>(model.config().timestamp_mode);
    ImGui::SetNextItemWidth(110.0F);
    if (ImGui::Combo("##time", &timestamp_mode, "Absolute\0Since boot\0Relative T0\0")) {
        model.config().timestamp_mode = static_cast<TimestampMode>(timestamp_mode);
    }
    ImGui::SameLine();
    int delta_mode = static_cast<int>(model.config().delta_mode);
    ImGui::SetNextItemWidth(120.0F);
    if (ImGui::Combo("##delta", &delta_mode, "Previous\0Per app\0From T0\0")) {
        model.config().delta_mode = static_cast<DeltaMode>(delta_mode);
    }
    ImGui::SameLine();
    if (!model.time().time_base_attached) {
        ImGui::TextColored(ImVec4(1.0F, 0.72F, 0.25F, 1.0F), "NO TB");
        if (ImGui::IsItemHovered()) {
            ImGui::SetTooltip("No published time base; Absolute uses the sampled UTC offset.");
        }
        ImGui::SameLine();
    }
    if (model.time().anchor_ns.has_value()) {
        if (ImGui::Button("Clear T0")) {
            model.time().clear_anchor();
        }
        ImGui::SameLine();
    }
    if (ImGui::Button("Prev B")) {
        if (const auto seq = model.previous_bookmark(model.last_selected()); seq.has_value()) {
            model.select_only(*seq);
            state.scroll_to = seq;
        }
    }
    ImGui::SameLine();
    if (ImGui::Button("Next B")) {
        if (const auto seq = model.next_bookmark(model.last_selected()); seq.has_value()) {
            model.select_only(*seq);
            state.scroll_to = seq;
        }
    }
    ImGui::SameLine();
    if (ImGui::Button("Prev W/E")) {
        if (const auto seq = model.previous_attention(model.last_selected()); seq.has_value()) {
            model.select_only(*seq);
            state.scroll_to = seq;
        }
    }
    ImGui::SameLine();
    if (ImGui::Button("Next W/E")) {
        if (const auto seq = model.next_attention(model.last_selected()); seq.has_value()) {
            model.select_only(*seq);
            state.scroll_to = seq;
        }
    }
}

void draw_toolbar_eco(LogViewModel& model, UiState& state) {
    ImGui::SameLine();
    const bool eco = model.guard().mode() == ResourceMode::Eco;
    ImGui::TextColored(eco ? ImVec4(1.0F, 0.72F, 0.25F, 1.0F) : ImVec4(0.55F, 0.80F, 0.95F, 1.0F),
                       "%s", eco ? "ECO" : "NORMAL");
    if (ImGui::IsItemHovered()) {
        ImGui::SetTooltip("mode=%s trigger=%s cpu=%.1f%% rate=%.0f/s", eco ? "eco" : "normal",
                          to_string(model.guard().trigger()).data(), model.guard().cpu_percent(),
                          model.guard().rate_ewma());
    }
    ImGui::SameLine();
    if (!state.auto_scroll && !model.paused()) {
        if (ImGui::Button("Return to live")) {
            state.auto_scroll = true;
        }
        ImGui::SameLine();
    }
    const auto counters = model.counters();
    ImGui::Text("visible=%llu total=%llu received=%llu synthetic=%llu evicted=%llu rate=%.0f/s",
                static_cast<unsigned long long>(counters.visible),
                static_cast<unsigned long long>(counters.total),
                static_cast<unsigned long long>(counters.received),
                static_cast<unsigned long long>(counters.synthetic),
                static_cast<unsigned long long>(counters.evicted), model.rate());
}

void draw_toolbar_find(LogViewModel& model, UiState& state) {
    ImGui::SameLine();
    ImGui::SetNextItemWidth(220.0F);
    if (state.focus_find) {
        ImGui::SetKeyboardFocusHere();
        state.focus_find = false;
    }
    if (ImGui::InputTextWithHint("##find", "find (Ctrl+F)", state.find_buf, sizeof(state.find_buf),
                                 ImGuiInputTextFlags_EnterReturnsTrue)) {
        model.begin_find(state.find_buf);
        if (ImGui::GetIO().KeyShift) {
            if (const auto seq = model.find_previous(); seq.has_value()) {
                state.scroll_to = seq;
            }
        } else {
            model.step_find(1'000'000U);
            if (const auto seq = model.find_next(); seq.has_value()) {
                state.scroll_to = seq;
            }
        }
    }
    if (model.find().count() > 0U) {
        ImGui::SameLine();
        ImGui::Text("%zu matches", model.find().count());
    } else if (!model.find().query().empty() && !model.find().active()) {
        ImGui::SameLine();
        ImGui::TextColored(ImVec4(0.9F, 0.5F, 0.4F, 1.0F), "0 matches");
    }
}

void draw_toolbar_progress(LogViewModel& model, UiState& state) {
    if (model.importing()) {
        char overlay[96]{};
        std::snprintf(overlay, sizeof(overlay), "import %llu malformed",
                      static_cast<unsigned long long>(model.import_stats().malformed));
        ImGui::ProgressBar(static_cast<float>(model.import_progress()), ImVec2(-1.0F, 0.0F),
                           overlay);
    }
    if (model.time().mixed_boot) {
        ImGui::TextColored(ImVec4(1.0F, 0.68F, 0.30F, 1.0F),
                           "Mixed boot sessions: cross-session deltas are not comparable.");
    }
    if (model.exporting() || model.export_stats().active) {
        const ExportStats stats = model.export_stats();
        char overlay[96]{};
        std::snprintf(overlay, sizeof(overlay), "export %llu/%llu",
                      static_cast<unsigned long long>(stats.written),
                      static_cast<unsigned long long>(stats.total));
        ImGui::ProgressBar(static_cast<float>(stats.ratio()), ImVec2(-1.0F, 0.0F), overlay);
        if (stats.missing > 0U) {
            ImGui::TextColored(ImVec4(1.0F, 0.6F, 0.3F, 1.0F), "%llu records evicted before export",
                               static_cast<unsigned long long>(stats.missing));
        }
        if (model.exporting() && ImGui::Button("Cancel export")) {
            model.cancel_export();
        }
    }
    if (!state.status.empty()) {
        ImGui::TextUnformatted(state.status.c_str());
    }
}

void draw_toolbar(LogViewModel& model, UiState& state) {
    if (!ImGui::Begin("Toolbar")) {
        ImGui::End();
        return;
    }
    draw_toolbar_pause(model, state);
    draw_toolbar_actions(model, state);
    draw_toolbar_modes(model, state);
    draw_toolbar_eco(model, state);
    draw_toolbar_find(model, state);
    draw_toolbar_progress(model, state);
    ImGui::End();
}

void draw_sources(LogViewModel& model, UiState& state) {
    if (!ImGui::Begin("Sources & levels")) {
        ImGui::End();
        return;
    }
    FilterCriteria& criteria = model.filter().criteria();
    ImGui::TextUnformatted("Levels");
    for (std::size_t i = 0; i < 7U; ++i) {
        bool visible = criteria.levels[i];
        ImGui::PushID(static_cast<int>(i));
        if (ImGui::Checkbox(kLevelNames[i], &visible)) {
            criteria.levels[i] = visible;
            model.begin_filter_rebuild();
        }
        ImGui::PopID();
    }
    ImGui::Separator();
    ImGui::SetNextItemWidth(-1.0F);
    if (ImGui::InputTextWithHint("##filter", "filter substring", state.filter_buf,
                                 sizeof(state.filter_buf))) {
        criteria.text = state.filter_buf;
        model.begin_filter_rebuild();
    }
    if (ImGui::Button("Show all sources")) {
        criteria.apps.fill(true);
        criteria.app_filter_active = false;
        model.begin_filter_rebuild();
    }
    ImGui::SameLine();
    if (ImGui::Button("Hide all")) {
        criteria.apps.fill(false);
        criteria.app_filter_active = true;
        model.begin_filter_rebuild();
    }
    ImGui::Separator();
    ImGui::TextUnformatted("Sources");
    const AppRegistry& apps = model.ring().apps();
    for (std::size_t slot = 0; slot < AppRegistry::capacity; ++slot) {
        const AppSlot& app = apps.at(slot);
        if (!app.used) {
            continue;
        }
        ImGui::PushID(static_cast<int>(slot));
        const ImVec4 color = to_imgui(model.colors().app_color(app.color_index));
        ImGui::ColorButton("##app_color", color,
                           ImGuiColorEditFlags_NoTooltip | ImGuiColorEditFlags_NoPicker,
                           ImVec2(12.0F, 12.0F));
        ImGui::SameLine();
        bool visible = criteria.apps[slot];
        if (ImGui::Checkbox("##visible", &visible)) {
            criteria.apps[slot] = visible;
            bool any_hidden = false;
            for (const bool shown : criteria.apps) {
                any_hidden = any_hidden || !shown;
            }
            criteria.app_filter_active = any_hidden;
            model.begin_filter_rebuild();
        }
        ImGui::SameLine();
        ImGui::Text("%.*s (%llu)", static_cast<int>(app.text().size()), app.text().data(),
                    static_cast<unsigned long long>(app.record_count));
        ImGui::PopID();
    }
    ImGui::End();
}

std::optional<std::uint64_t> previous_frame(const LogViewModel& model, std::size_t index) {
    const RecordRing& ring = model.ring();
    while (index > 0U) {
        --index;
        const std::uint64_t seq = ring.visible_at(index);
        if (const auto view = ring.find(seq); view.has_value() && view->is_frame()) {
            return view->frame->timestamp_ns;
        }
    }
    return std::nullopt;
}

void select_row(LogViewModel& model, std::uint64_t seq) {
    const ImGuiIO& io = ImGui::GetIO();
    if (io.KeyShift) {
        model.select_to(seq);
    } else if (io.KeyCtrl) {
        model.toggle_selection(seq);
    } else {
        model.select_only(seq);
    }
}

struct RowContext {
    std::uint64_t previous_stamp{0};
    bool have_previous{false};
};

void draw_table_header() {
    ImGui::TableSetupScrollFreeze(0, 1);
    ImGui::TableSetupColumn("B", ImGuiTableColumnFlags_WidthFixed, 22.0F);
    ImGui::TableSetupColumn("#", ImGuiTableColumnFlags_WidthFixed, 80.0F);
    ImGui::TableSetupColumn("Timestamp", ImGuiTableColumnFlags_WidthFixed, 190.0F);
    ImGui::TableSetupColumn("Delta", ImGuiTableColumnFlags_WidthFixed, 110.0F);
    ImGui::TableSetupColumn("Application", ImGuiTableColumnFlags_WidthFixed, 120.0F);
    ImGui::TableSetupColumn("Context", ImGuiTableColumnFlags_WidthFixed, 70.0F);
    ImGui::TableSetupColumn("Level", ImGuiTableColumnFlags_WidthFixed, 75.0F);
    ImGui::TableSetupColumn("Message", ImGuiTableColumnFlags_WidthStretch);
    ImGui::TableHeadersRow();
}

std::int64_t row_delta(const LogViewModel& model, const RecordView& view,
                       const RowContext& context) noexcept {
    if (!view.is_frame()) {
        return 0;
    }
    if (model.config().delta_mode == DeltaMode::PerApplication) {
        return view.prev_same_app_ns;
    }
    if (model.config().delta_mode == DeltaMode::FromAnchor && model.time().anchor_ns.has_value()) {
        return static_cast<std::int64_t>(view.frame->timestamp_ns) -
               static_cast<std::int64_t>(*model.time().anchor_ns);
    }
    if (model.config().delta_mode == DeltaMode::PreviousVisible && context.have_previous) {
        return static_cast<std::int64_t>(view.frame->timestamp_ns) -
               static_cast<std::int64_t>(context.previous_stamp);
    }
    return 0;
}

void draw_message_cell(const LogViewModel& model, const RecordView& view) {
    const std::string& query = model.find().query();
    if (!view.is_frame()) {
        ImGui::Text("[gap] %llu lost (%s)", static_cast<unsigned long long>(view.gap_count),
                    to_string(view.kind).data());
        return;
    }
    const std::string_view message = view.frame->payload_text();
    if (query.empty() || !model.find().contains(view.seq) || message.empty()) {
        if (message.empty()) {
            ImGui::TextUnformatted("");
        } else {
            ImGui::TextUnformatted(message.data(), message.data() + message.size());
        }
        return;
    }
    std::size_t cursor = 0;
    for (;;) {
        const std::size_t hit = message.find(query, cursor);
        if (hit == std::string_view::npos) {
            if (cursor < message.size()) {
                ImGui::TextUnformatted(message.data() + cursor, message.data() + message.size());
            }
            break;
        }
        if (hit > cursor) {
            ImGui::TextUnformatted(message.data() + cursor, message.data() + hit);
            ImGui::SameLine(0.0F, 0.0F);
        }
        ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.0F, 0.92F, 0.35F, 1.0F));
        ImGui::TextUnformatted(message.data() + hit, message.data() + hit + query.size());
        ImGui::PopStyleColor();
        cursor = hit + query.size();
        if (cursor < message.size()) {
            ImGui::SameLine(0.0F, 0.0F);
        }
    }
}

void draw_record_row(LogViewModel& model, const RecordView& view, float row_height,
                     RowContext& context) {
    const bool selected = model.ring().is_selected(view.seq);
    const bool bookmarked = model.ring().is_bookmarked(view.seq);
    const bool is_frame = view.is_frame();
    const nexigen::core::LogLevel level = view.frame->log_level;
    const LevelStyle& style = model.colors().level(level);
    const bool critical =
        model.colors().mode() == ColorMode::Criticality && ColorManager::is_critical(level);
    ImVec4 text_color = to_imgui(style.text);
    const bool is_anchor =
        model.time().anchor_ns.has_value() && is_frame &&
        static_cast<std::int64_t>(view.frame->timestamp_ns) == *model.time().anchor_ns;
    if (is_anchor) {
        text_color = ImVec4(0.45F, 0.90F, 1.0F, 1.0F);
    }
    ImGui::PushStyleColor(ImGuiCol_Text, critical ? to_imgui(style.text) : text_color);
    ImGui::TableNextRow();
    if (style.background.a > 0.01F) {
        ImGui::TableSetBgColor(ImGuiTableBgTarget_RowBg0,
                               ImGui::GetColorU32(to_imgui(style.background)));
    }
    ImGui::TableSetColumnIndex(0);
    const bool clicked = ImGui::Selectable(
        "##row", selected, ImGuiSelectableFlags_SpanAllColumns | ImGuiSelectableFlags_AllowOverlap,
        ImVec2(0.0F, row_height));
    if (clicked) {
        select_row(model, view.seq);
    }
    ImGui::SameLine();
    ImGui::TextUnformatted(bookmarked ? "B" : " ");
    ImGui::TableSetColumnIndex(1);
    ImGui::Text("%llu", static_cast<unsigned long long>(view.seq));
    ImGui::TableSetColumnIndex(2);
    const FormattedTime stamp = TimeFormatter::format_timestamp(
        view.frame->timestamp_ns, model.config().timestamp_mode, model.time());
    if (stamp.unknown) {
        ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.0F, 0.55F, 0.35F, 1.0F));
        ImGui::TextUnformatted(stamp.text.data());
        ImGui::PopStyleColor();
    } else {
        ImGui::TextUnformatted(stamp.text.data());
    }
    ImGui::TableSetColumnIndex(3);
    if (is_frame && model.time().mixed_boot) {
        ImGui::TextUnformatted("-");
    } else if (is_frame) {
        const std::int64_t delta = row_delta(model, view, context);
        if (TimeFormatter::is_anomalous(delta, model.time().anomaly_threshold_ns)) {
            ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.0F, 0.55F, 0.35F, 1.0F));
            ImGui::TextUnformatted(TimeFormatter::format_delta(delta).text.data());
            ImGui::PopStyleColor();
        } else {
            ImGui::TextUnformatted(TimeFormatter::format_delta(delta).text.data());
        }
        context.previous_stamp = view.frame->timestamp_ns;
        context.have_previous = true;
    } else {
        ImGui::TextUnformatted("-");
    }
    ImGui::TableSetColumnIndex(4);
    if (is_frame) {
        const std::size_t slot = model.ring().apps().find(view.frame->app_id_text());
        if (slot < AppRegistry::capacity) {
            const ImVec4 app_color =
                to_imgui(model.colors().app_color(model.ring().apps().color_index(slot)));
            const std::string_view app_text = model.ring().apps().at(slot).text();
            if (model.colors().mode() == ColorMode::Combined && !critical) {
                ImGui::PushStyleColor(ImGuiCol_Text, app_color);
                ImGui::TextUnformatted(app_text.data(), app_text.data() + app_text.size());
                ImGui::PopStyleColor();
            } else {
                ImGui::TextUnformatted(app_text.data(), app_text.data() + app_text.size());
            }
        } else {
            ImGui::TextUnformatted("-");
        }
    } else {
        ImGui::TextUnformatted("-");
    }
    ImGui::TableSetColumnIndex(5);
    ImGui::TextUnformatted("-");
    ImGui::TableSetColumnIndex(6);
    ImGui::TextUnformatted(nexigen::core::to_string(level).data());
    ImGui::TableSetColumnIndex(7);
    draw_message_cell(model, view);
    ImGui::PopStyleColor();
}

void apply_table_scroll(LogViewModel& model, UiState& state, int rows, float row_height) {
    const float scroll_max = ImGui::GetScrollMaxY();
    const float scroll = ImGui::GetScrollY();
    const bool at_bottom = scroll + 1.0F >= scroll_max;
    const bool drag_or_key_scroll =
        (!at_bottom && ImGui::IsWindowHovered() && ImGui::IsAnyMouseDown()) ||
        (!at_bottom && ImGui::IsWindowFocused() &&
         (ImGui::IsKeyPressed(ImGuiKey_PageUp) || ImGui::IsKeyPressed(ImGuiKey_UpArrow)));
    if (drag_or_key_scroll) {
        state.auto_scroll = false;
    }
    if (state.scroll_to.has_value() && rows > 0) {
        const std::size_t display_limit = static_cast<std::size_t>(rows);
        for (std::size_t i = 0; i < display_limit; ++i) {
            if (model.ring().visible_at(i) == *state.scroll_to) {
                ImGui::SetScrollY(static_cast<float>(i) * row_height);
                break;
            }
        }
        state.scroll_to.reset();
    } else if (state.auto_scroll && !model.paused()) {
        ImGui::SetScrollY(scroll_max);
    }
    if (ImGui::IsWindowHovered() && ImGui::GetIO().MouseWheel > 0.0F) {
        state.auto_scroll = false;
    }
}

void draw_table(LogViewModel& model, UiState& state) {
    if (!ImGui::Begin("Log")) {
        ImGui::End();
        return;
    }
    const FilterIndex::Progress progress = model.filter_progress();
    if (progress.active) {
        char overlay[96]{};
        std::snprintf(overlay, sizeof(overlay), "filter rebuild %llu/%llu",
                      static_cast<unsigned long long>(progress.scanned),
                      static_cast<unsigned long long>(progress.total));
        ImGui::ProgressBar(static_cast<float>(progress.ratio()), ImVec2(-1.0F, 0.0F), overlay);
    }
    constexpr ImGuiTableFlags flags = ImGuiTableFlags_Resizable | ImGuiTableFlags_RowBg |
                                      ImGuiTableFlags_BordersOuter | ImGuiTableFlags_BordersInnerV |
                                      ImGuiTableFlags_ScrollY | ImGuiTableFlags_SizingStretchProp;
    if (!ImGui::BeginTable("records", 8, flags)) {
        ImGui::End();
        return;
    }
    draw_table_header();
    const std::size_t display = model.display_count();
    const int rows = static_cast<int>(std::min<std::size_t>(display, 2'000'000'000U));
    ImGuiListClipper clipper;
    clipper.Begin(rows, ImGui::GetTextLineHeightWithSpacing());
    const float row_height = ImGui::GetTextLineHeightWithSpacing();
    RowContext context{};
    if (rows > 0 && model.config().delta_mode == DeltaMode::PreviousVisible) {
        if (const auto stamp =
                previous_frame(model, static_cast<std::size_t>(clipper.DisplayStart));
            stamp.has_value()) {
            context.previous_stamp = *stamp;
            context.have_previous = true;
        }
    }
    while (clipper.Step()) {
        for (int row = clipper.DisplayStart; row < clipper.DisplayEnd; ++row) {
            const std::uint64_t seq = model.ring().visible_at(static_cast<std::size_t>(row));
            if (const auto view = model.ring().find(seq); view.has_value()) {
                draw_record_row(model, *view, row_height, context);
            }
        }
    }
    apply_table_scroll(model, state, rows, row_height);
    ImGui::EndTable();
    ImGui::End();
}

void draw_inspector(LogViewModel& model, UiState& state) {
    if (state.input_test && state.input_phase < 5) {
        ImGui::SetNextWindowDockID(0, ImGuiCond_Always);
        ImGui::SetNextWindowPos(ImVec2(800.0F, 80.0F), ImGuiCond_Always);
        ImGui::SetNextWindowSize(ImVec2(700.0F, 400.0F), ImGuiCond_Always);
        ImGui::SetNextWindowCollapsed(false);
        ImGui::SetNextWindowFocus();
    }
    if (!ImGui::Begin("Inspector")) {
        ImGui::End();
        return;
    }
    const auto selected = model.last_selected();
    if (!selected.has_value()) {
        ImGui::TextUnformatted("Select a record to inspect.");
        ImGui::End();
        return;
    }
    const auto view = model.ring().find(*selected);
    if (!view.has_value()) {
        ImGui::TextUnformatted("The selected record has been evicted.");
        ImGui::End();
        return;
    }
    const FormattedTime stamp = TimeFormatter::format_timestamp(
        view->frame->timestamp_ns, model.config().timestamp_mode, model.time());
    ImGui::Text("index: %llu", static_cast<unsigned long long>(view->seq));
    ImGui::Text("kind: %s", to_string(view->kind).data());
    ImGui::Text("timestamp: %s", stamp.text.data());
    ImGui::Text("application: %.*s", static_cast<int>(view->frame->app_id_text().size()),
                view->frame->app_id_text().data());
    ImGui::Text("context: -");
    ImGui::Text("level: %s", nexigen::core::to_string(view->frame->log_level).data());
    ImGui::Text("thread: %u  pid: %u", view->frame->thread_id, view->frame->pid);
    ImGui::Text("publisher drop counter: %u", view->frame->dropped_counter);
    ImGui::Text("wire version: %u", view->frame->version);
    if (payload_may_be_truncated(*view->frame)) {
        ImGui::TextColored(ImVec4(1.0F, 0.68F, 0.30F, 1.0F),
                           "Payload is at capacity; the producer may have truncated it.");
    }
    ImGui::Separator();
    ImGui::TextWrapped("%.*s", static_cast<int>(view->frame->payload_text().size()),
                       view->frame->payload_text().data());
    if (ImGui::Button("Copy record")) {
        const std::string text = ui::copy_selected(model);
        if (!text.empty()) {
            ImGui::SetClipboardText(text.c_str());
            state.copied = true;
        }
    }
    if (state.capture_copy_rect) {
        state.copy_rect_min = ImGui::GetItemRectMin();
        state.copy_rect_max = ImGui::GetItemRectMax();
        state.copy_rect_valid = true;
        state.capture_copy_rect = false;
    }
    ImGui::End();
}

void draw_minimap(LogViewModel& model) {
    if (!ImGui::Begin("Minimap")) {
        ImGui::End();
        return;
    }
    ImDrawList* draw = ImGui::GetWindowDrawList();
    const ImVec2 origin = ImGui::GetCursorScreenPos();
    const ImVec2 size = ImGui::GetContentRegionAvail();
    draw->AddRectFilled(origin, ImVec2(origin.x + size.x, origin.y + size.y),
                        IM_COL32(18, 20, 24, 255));
    const Minimap& minimap = model.ring().minimap();
    for (std::size_t i = 0; i < Minimap::bucket_count; ++i) {
        const MinimapBucket& bucket = minimap.at(i);
        if (bucket.errors == 0U && bucket.fatals == 0U && bucket.bookmarks == 0U) {
            continue;
        }
        const float y0 = origin.y + size.y * static_cast<float>(i) / 1024.0F;
        const float y1 = origin.y + size.y * static_cast<float>(i + 1U) / 1024.0F;
        if (bucket.fatals > 0U) {
            draw->AddRectFilled(ImVec2(origin.x, y0), ImVec2(origin.x + size.x, y1),
                                IM_COL32(220, 45, 55, 190));
        } else if (bucket.errors > 0U) {
            draw->AddRectFilled(ImVec2(origin.x, y0), ImVec2(origin.x + size.x, y1),
                                IM_COL32(230, 120, 50, 170));
        }
        if (bucket.bookmarks > 0U) {
            draw->AddRectFilled(ImVec2(origin.x + size.x - 6.0F, y0), ImVec2(origin.x + size.x, y1),
                                IM_COL32(240, 210, 70, 230));
        }
    }
    ImGui::Dummy(size);
    ImGui::End();
}

void draw_status(LogViewModel& model, UiState& state) {
    if (!ImGui::Begin("Status")) {
        ImGui::End();
        return;
    }
    ImGui::Text("service: %s", model.runtime_service_name().c_str());
    ImGui::Text("memory budget: %u MiB", model.runtime_memory_mb());
    ImGui::Text("live: %s", state.live ? "attached" : "unavailable (offline)");
    ImGui::Text("time base: %s", model.time().time_base_attached ? "attached" : "sampled offset");
    ImGui::Text("publishers: %u", state.capture_stats.publishers);
    ImGui::Text("received: %llu  lost: %llu  rejected: %llu",
                static_cast<unsigned long long>(state.capture_stats.received),
                static_cast<unsigned long long>(state.capture_stats.overflow),
                static_cast<unsigned long long>(state.capture_stats.rejected));
    ImGui::Text("hand-off: %zu / 65536", state.capture_stats.ingress_size);
    ImGui::Text("last transport error: %d", state.capture_stats.last_error);
    ImGui::Text("imported: %llu malformed: %llu mixed boot: %s",
                static_cast<unsigned long long>(model.import_stats().imported),
                static_cast<unsigned long long>(model.import_stats().malformed),
                model.import_stats().mixed_boot ? "yes" : "no");
    ImGui::Text("history: %llu / %zu  evictions: %llu",
                static_cast<unsigned long long>(model.ring().size()), model.ring().capacity(),
                static_cast<unsigned long long>(model.ring().evictions()));
    ImGui::Text("selected: %llu", static_cast<unsigned long long>(model.ring().selected_count()));
    ImGui::End();
}

void draw_save_modal(LogViewModel& model, UiState& state) {
    if (state.open_save) {
        ImGui::OpenPopup("Save selected records");
        state.open_save = false;
    }
    if (!ImGui::BeginPopupModal("Save selected records", nullptr,
                                ImGuiWindowFlags_AlwaysAutoResize)) {
        return;
    }
    ImGui::InputText("path", state.save_path, sizeof(state.save_path));
    const char* formats[] = {"JSONL", "CSV", "TXT"};
    int current = static_cast<int>(state.save_format);
    if (ImGui::Combo("format", &current, formats, 3)) {
        state.save_format = static_cast<ExportFormat>(current);
    }
    if (ImGui::Button("Save")) {
        const auto started = model.start_export(state.save_path, state.save_format);
        state.status = started ? "export started" : "export failed to start (select records first)";
        ImGui::CloseCurrentPopup();
    }
    ImGui::SameLine();
    if (ImGui::Button("Cancel")) {
        ImGui::CloseCurrentPopup();
    }
    ImGui::EndPopup();
}

void draw_open_modal(LogViewModel& model, UiState& state) {
    if (state.open_open) {
        ImGui::OpenPopup("Open log file");
        state.open_open = false;
    }
    if (state.input_test) {
        ImGui::SetNextWindowPos(ImVec2(500.0F, 550.0F), ImGuiCond_Always);
    }
    if (!ImGui::BeginPopupModal("Open log file", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
        return;
    }
    ImGui::InputText("path", state.open_path, sizeof(state.open_path));
    if (ui::open_requires_confirmation(state.live, model.ring().size())) {
        ImGui::TextColored(ImVec4(1.0F, 0.72F, 0.25F, 1.0F),
                           "Live capture is running; opening replaces the current history.");
    }
    const bool open_clicked = ImGui::Button("Open");
    if (state.capture_open_modal_rect) {
        state.open_modal_rect_min = ImGui::GetItemRectMin();
        state.open_modal_rect_max = ImGui::GetItemRectMax();
        state.open_modal_rect_valid = true;
        state.capture_open_modal_rect = false;
    }
    if (open_clicked) {
        const auto imported = ui::open_file(model, state.open_path);
        state.status = imported ? "import started" : "import failed";
        state.open_clicked = true;
        ImGui::CloseCurrentPopup();
    }
    ImGui::SameLine();
    if (ImGui::Button("Cancel")) {
        ImGui::CloseCurrentPopup();
    }
    ImGui::EndPopup();
}

void draw_colors_modal(LogViewModel& model, UiState& state) {
    if (state.open_colors) {
        ImGui::OpenPopup("Colors");
        state.open_colors = false;
    }
    if (!ImGui::BeginPopupModal("Colors", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
        return;
    }
    int mode = static_cast<int>(model.colors().mode());
    const char* modes[] = {"Combined", "Criticality"};
    if (ImGui::Combo("mode", &mode, modes, 2)) {
        model.colors().set_mode(static_cast<ColorMode>(mode));
    }
    for (std::size_t i = 0; i < 7U; ++i) {
        ImGui::PushID(static_cast<int>(i));
        LevelStyle& style = model.colors().level(static_cast<nexigen::core::LogLevel>(i));
        ImGui::ColorEdit4("text", &style.text.r, ImGuiColorEditFlags_AlphaBar);
        ImGui::ColorEdit4("background", &style.background.r, ImGuiColorEditFlags_AlphaBar);
        ImGui::PopID();
    }
    int threshold_ms = static_cast<int>(model.time().anomaly_threshold_ns / 1'000'000LL);
    if (ImGui::InputInt("anomaly threshold (ms)", &threshold_ms) && threshold_ms >= 0) {
        model.time().anomaly_threshold_ns = static_cast<std::int64_t>(threshold_ms) * 1'000'000LL;
        model.config().anomaly_threshold_ns =
            static_cast<std::uint64_t>(model.time().anomaly_threshold_ns);
    }
    if (ImGui::Button("Reset")) {
        model.reset_colors();
    }
    ImGui::SameLine();
    if (ImGui::Button("Close")) {
        ImGui::CloseCurrentPopup();
    }
    ImGui::EndPopup();
}

void draw_modals(LogViewModel& model, UiState& state) {
    draw_save_modal(model, state);
    draw_open_modal(model, state);
    draw_colors_modal(model, state);
}

} // namespace

int run_ui_actions_test(LogViewModel& model, const UiOptions& options) {
    const std::size_t history = model.ring().size();
    const std::size_t visible = model.display_count();
    if (visible == 0U) {
        std::fprintf(stderr, "UI_ACTIONS failed: no visible record\n");
        return 1;
    }
    model.select_only(model.ring().visible_at(0));
    const bool truncated = ui::selected_payload_truncated(model);
    const std::string clipboard = ui::copy_selected(model);
    const bool confirm = ui::open_requires_confirmation(true, history);
    const bool opened =
        !options.open_path.empty() && static_cast<bool>(ui::open_file(model, options.open_path));
    if (opened) {
        while (model.importing()) {
            model.step_import(100000U);
        }
    }
    const bool imported = opened && model.import_stats().imported > 0U;
    std::printf("UI_ACTIONS truncated=%d copy=%zu confirm=%d open=%d imported=%d\n", truncated,
                clipboard.size(), confirm, opened, imported);
    return (truncated && !clipboard.empty() && confirm && imported) ? 0 : 1;
}

bool init_gui(UiState& state, const UiOptions& options, GLFWwindow*& window) {
    glfwSetErrorCallback([](int code, const char* description) {
        std::fprintf(stderr, "nexigen-logview: glfw error %d: %s\n", code, description);
    });
    if (glfwInit() == GLFW_FALSE) {
        std::fprintf(stderr, "nexigen-logview: cannot initialise GLFW\n");
        return false;
    }
    float scale = options.ui_scale;
    if (scale <= 0.0F) {
        float scale_x = 1.0F;
        float scale_y = 1.0F;
        if (GLFWmonitor* monitor = glfwGetPrimaryMonitor(); monitor != nullptr) {
            glfwGetMonitorContentScale(monitor, &scale_x, &scale_y);
        }
        scale = clamp_auto_ui_scale(scale_x > scale_y ? scale_x : scale_y);
    }
    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 3);
    glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);
    glfwWindowHint(GLFW_VISIBLE, options.smoke ? GLFW_FALSE : GLFW_TRUE);
    window = glfwCreateWindow(static_cast<int>(std::lround(1600.0F * scale)),
                              static_cast<int>(std::lround(900.0F * scale)), "nexigen-logview",
                              nullptr, nullptr);
    if (window == nullptr) {
        glfwTerminate();
        std::fprintf(stderr, "nexigen-logview: cannot create a window\n");
        return false;
    }
    glfwMakeContextCurrent(window);
    glfwSwapInterval(1);

    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO();
    io.ConfigFlags |= ImGuiConfigFlags_DockingEnable;
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
    io.IniFilename = state.ini_path.c_str();
    apply_dark_theme();
    ImGui::GetStyle().ScaleAllSizes(scale);
#if NEXIGEN_LOGVIEW_HAS_FONT
    ImFontConfig font_config{};
    font_config.FontDataOwnedByAtlas = false;
    io.Fonts->AddFontFromMemoryTTF(const_cast<unsigned char*>(assets::kJetBrainsMono.data()),
                                   static_cast<int>(assets::kJetBrainsMono.size()), 16.0F * scale,
                                   &font_config);
#endif
    ImGui_ImplGlfw_InitForOpenGL(window, true);
    ImGui_ImplOpenGL3_Init("#version 330");
    return true;
}

void shutdown_gui(GLFWwindow* window) {
    ImGui_ImplOpenGL3_Shutdown();
    ImGui_ImplGlfw_Shutdown();
    ImGui::DestroyContext();
    if (window != nullptr) {
        glfwDestroyWindow(window);
    }
    glfwTerminate();
}

void draw_default_layout(ImGuiID dockspace) {
    ImGuiID main = dockspace;
    ImGuiID top = ImGui::DockBuilderSplitNode(main, ImGuiDir_Up, 0.08F, nullptr, &main);
    ImGuiID bottom = ImGui::DockBuilderSplitNode(main, ImGuiDir_Down, 0.28F, nullptr, &main);
    ImGuiID left = ImGui::DockBuilderSplitNode(main, ImGuiDir_Left, 0.22F, nullptr, &main);
    ImGuiID right = ImGui::DockBuilderSplitNode(main, ImGuiDir_Right, 0.12F, nullptr, &main);
    ImGui::DockBuilderDockWindow("Toolbar", top);
    ImGui::DockBuilderDockWindow("Log", main);
    ImGui::DockBuilderDockWindow("Sources & levels", left);
    ImGui::DockBuilderDockWindow("Minimap", right);
    ImGui::DockBuilderDockWindow("Inspector", bottom);
    ImGui::DockBuilderDockWindow("Status", bottom);
    ImGui::DockBuilderFinish(dockspace);
}

void process_model_frame(LogViewModel& model, UiState& state, CaptureThread* capture) {
    if (capture != nullptr) {
        state.capture_stats = capture->stats();
        static_cast<void>(model.drain(capture->ingress(), state.capture_stats.overflow));
        if (state.capture_stats.rejected > state.last_rejected) {
            model.note_rejected(state.capture_stats.rejected - state.last_rejected);
            state.last_rejected = state.capture_stats.rejected;
        }
    }
    const std::uint64_t now_ns = monotonic_now_ns();
    static_cast<void>(model.tick(now_ns, process_cpu_ns()));
    if (!model.guard().recompute_due(now_ns)) {
        return;
    }
    if (model.filter_progress().active) {
        model.step_filter_rebuild(20'000U);
    }
    if (model.find().active()) {
        model.step_find(20'000U);
    }
    if (model.importing()) {
        model.step_import(2'000U);
    }
    if (model.exporting()) {
        model.step_export(1'024U);
    }
}

void handle_shortcuts(LogViewModel& model, UiState& state) {
    const ImGuiIO& io = ImGui::GetIO();
    if (io.WantTextInput) {
        return;
    }
    if (ImGui::IsKeyPressed(ImGuiKey_Space)) {
        model.set_paused(!model.paused());
        state.auto_scroll = !model.paused();
    }
    if (ImGui::IsKeyPressed(ImGuiKey_F2)) {
        if (const auto seq = model.last_selected(); seq.has_value()) {
            static_cast<void>(model.toggle_bookmark(*seq));
        }
    }
    if (ImGui::IsKeyPressed(ImGuiKey_T)) {
        if (const auto seq = model.last_selected(); seq.has_value()) {
            if (const auto view = model.ring().find(*seq); view.has_value()) {
                model.time().set_anchor(static_cast<std::int64_t>(view->frame->timestamp_ns));
            }
        }
    }
    if (io.KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_A)) {
        model.select_all_visible();
    }
    if (io.KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_F)) {
        state.focus_find = true;
    }
    if (ImGui::IsKeyPressed(ImGuiKey_F3)) {
        const auto seq = io.KeyShift ? model.previous_bookmark(model.last_selected())
                                     : model.next_bookmark(model.last_selected());
        if (seq.has_value()) {
            model.select_only(*seq);
            state.scroll_to = seq;
        }
    }
    if (ImGui::IsKeyPressed(ImGuiKey_F4)) {
        const auto seq = io.KeyShift ? model.previous_attention(model.last_selected())
                                     : model.next_attention(model.last_selected());
        if (seq.has_value()) {
            model.select_only(*seq);
            state.scroll_to = seq;
        }
    }
}

void present_frame(GLFWwindow* window) {
    ImGui::Render();
    int width = 0;
    int height = 0;
    glfwGetFramebufferSize(window, &width, &height);
    glViewport(0, 0, width, height);
    glClearColor(0.06F, 0.07F, 0.08F, 1.0F);
    glClear(GL_COLOR_BUFFER_BIT);
    ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
    glfwSwapBuffers(window);
}

ImVec2 rect_center(ImVec2 min, ImVec2 max) {
    return ImVec2((min.x + max.x) * 0.5F, (min.y + max.y) * 0.5F);
}

void inject_ui_input(UiState& state) {
    if (!state.input_test) {
        return;
    }
    ImGuiIO& io = ImGui::GetIO();
    if (state.inject_mouse_pos) {
        io.AddMousePosEvent(state.inject_pos.x, state.inject_pos.y);
        state.inject_mouse_pos = false;
    }
    if (state.inject_mouse_down) {
        io.AddMouseButtonEvent(0, true);
        state.inject_mouse_down = false;
    }
    if (state.inject_mouse_up) {
        io.AddMouseButtonEvent(0, false);
        state.inject_mouse_up = false;
    }
}

bool drive_input_script(LogViewModel& model, UiState& state) {
    if (!state.input_test) {
        return false;
    }
    if (state.input_phase > 0 && ++state.input_phase_frames > 600) {
        std::printf("UI_INPUT stalled phase=%d copied=%d copy_rect=%d open_rect=%d popup=%d\n",
                    state.input_phase, state.copied, state.copy_rect_valid,
                    state.open_modal_rect_valid, ImGui::IsPopupOpen("Open log file"));
        return true;
    }
    switch (state.input_phase) {
        case 0:
            if (model.display_count() > 0U) {
                model.select_only(model.ring().visible_at(0));
                state.truncated = ui::selected_payload_truncated(model);
                state.capture_copy_rect = true;
                state.input_phase = 1;
            }
            break;
        case 1:
            if (state.copy_rect_valid) {
                state.inject_pos = rect_center(state.copy_rect_min, state.copy_rect_max);
                state.inject_mouse_pos = true;
                state.input_phase = 2;
            }
            break;
        case 2:
            state.inject_mouse_down = true;
            state.input_phase = 3;
            break;
        case 3:
            state.inject_mouse_up = true;
            state.input_phase = 4;
            break;
        case 4:
            if (state.copied) {
                state.open_open = true;
                state.capture_open_modal_rect = true;
                state.input_phase = 5;
            }
            break;
        case 5:
            if (state.open_modal_rect_valid) {
                state.inject_pos =
                    rect_center(state.open_modal_rect_min, state.open_modal_rect_max);
                state.inject_mouse_pos = true;
                state.input_phase = 6;
            }
            break;
        case 6:
            state.inject_mouse_down = true;
            state.input_phase = 7;
            break;
        case 7:
            state.inject_mouse_up = true;
            state.input_phase = 8;
            break;
        case 8:
            while (model.importing()) {
                model.step_import(100000U);
            }
            std::printf("UI_INPUT copied=%d truncated=%d opened=%d layout=%d\n", state.copied,
                        state.truncated, state.open_clicked, state.layout_loaded);
            return true;
        default: return true;
    }
    return false;
}

int run_ui(LogViewModel& model, const UiOptions& options) {
    UiState state{};
    state.live = options.live;
    state.input_test = options.input_test;
    state.ini_path = parent_directory(default_config_path()) + "/logview-layout.ini";
    {
        std::error_code error;
        state.layout_loaded = std::filesystem::exists(state.ini_path, error);
        std::filesystem::create_directories(parent_directory(state.ini_path), error);
    }
    std::snprintf(state.filter_buf, sizeof(state.filter_buf), "%s",
                  model.filter().criteria().text.c_str());
    std::snprintf(state.save_path, sizeof(state.save_path), "/tmp/nexigen-logview-export.jsonl");
    std::snprintf(state.open_path, sizeof(state.open_path), "%s", options.open_path.c_str());

    GLFWwindow* window = nullptr;
    if (!init_gui(state, options, window)) {
        return 1;
    }
    std::unique_ptr<CaptureThread> capture;
    if (options.live) {
        capture = std::make_unique<CaptureThread>(model.config().service_name,
                                                  model.config().capture_thread);
        const auto started = capture->start();
        if (!started) {
            state.status =
                "capture unavailable: " + std::string(nexigen::core::to_string(started.error()));
            capture.reset();
        }
    }
    state.capture = capture.get();

    if (options.actions_test) {
        const int actions_result = run_ui_actions_test(model, options);
        if (capture != nullptr) {
            capture->stop();
        }
        shutdown_gui(window);
        return actions_result;
    }

    bool layout_done = false;
    while (glfwWindowShouldClose(window) == GLFW_FALSE) {
        const bool eco = model.guard().mode() == ResourceMode::Eco;
        if (eco) {
            glfwWaitEventsTimeout(1.0 / 15.0);
        } else {
            glfwPollEvents();
        }
        if (glfwWindowShouldClose(window) != GLFW_FALSE) {
            break;
        }
        process_model_frame(model, state, capture.get());
        ImGui_ImplOpenGL3_NewFrame();
        ImGui_ImplGlfw_NewFrame();
        inject_ui_input(state);
        ImGui::NewFrame();
        const ImGuiID dockspace = ImGui::DockSpaceOverViewport();
        if (!layout_done) {
            layout_done = true;
            std::error_code layout_error;
            if (options.smoke || !std::filesystem::exists(state.ini_path, layout_error)) {
                draw_default_layout(dockspace);
            }
        }
        draw_toolbar(model, state);
        draw_sources(model, state);
        draw_table(model, state);
        draw_inspector(model, state);
        draw_minimap(model);
        draw_status(model, state);
        draw_modals(model, state);
        handle_shortcuts(model, state);
        const bool input_done = drive_input_script(model, state);
        present_frame(window);
        if (input_done || (options.smoke && ++state.smoke_frames >= 2)) {
            break;
        }
    }

    if (capture != nullptr) {
        capture->stop();
    }
    shutdown_gui(window);
    return 0;
}

} // namespace nexigen::logview
