#include "logview_window.hpp"

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <filesystem>
#include <format>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <system_error>
#include <unordered_map>
#include <vector>

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
#include "splash_image.hpp"
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

struct ViewUiState : ui::ViewScrollState {
    char filter_buf[512]{};
    char find_buf[128]{};
    bool focus_find{false};
};

struct UiState {
    CaptureThread* capture{nullptr};
    CaptureStats capture_stats{};
    std::string status{};
    std::string ini_path{};
    char save_path[512]{};
    char open_path[512]{};
    bool open_save{false};
    bool open_open{false};
    bool open_colors{false};
    bool clone_open{false};
    ViewId clone_target{0};
    CloneMode clone_mode{CloneMode::Live};
    CloneOptions clone_options{};
    std::string clone_status{};
    ViewId save_view_id{0};
    ExportFormat save_format{ExportFormat::Jsonl};
    std::unordered_map<ViewId, ViewUiState> view_ui{};
    std::unordered_map<ViewId, ImGuiID> pending_dock{};
    ViewId float_view{0};
    std::uint64_t last_rejected{0};
    int smoke_frames{0};
    bool first_frame{true};
    bool live{true};
    bool input_test{false};
    bool log_view_focused{false};
    bool auto_open_sources_popup{false};
    bool force_eco{false};
    bool seen_pause_badge{false};
    bool seen_view_counters{false};
    bool seen_eco_badge{false};
    bool seen_floating_view{false};
    std::size_t docked_views{0};
    std::optional<std::uint64_t> inspected_seq{};
    bool seen_inspector_meta{false};
    bool seen_navigation_controls{false};
    bool seen_auto_scroll_active{false};
    std::size_t navigation_hit_areas{0};
    std::size_t splash_frames{0};
    double splash_elapsed_seconds{0.0};
    bool splash_aspect_fit{false};
    std::size_t splash_ingested{0};
    bool splash_input_ignored{true};
    bool splash_input_test{false};
    bool splash_ingest_test{false};
    std::size_t splash_expected_views{0};
    int splash_workspace_frames{0};
    std::optional<std::uint64_t> expected_inspected_seq{};
    ViewId view_test_focus_id{0};
    bool view_test{false};
    int view_test_phase{0};
    std::array<ImVec2, 3> nav_rect_min{};
    std::array<ImVec2, 3> nav_rect_max{};
    bool nav_rects_valid{false};
    bool view_test_toggle_off{false};
    bool view_test_toggle_on{false};
    bool view_test_wheel_off{false};
    bool inject_wheel{false};
    float inject_wheel_y{0.0F};
    bool focus_test_active{false};
    int focus_phase{0};
    ViewId focus_view_id{0};
    std::uint64_t focus_before{0};
    bool focus_ok{false};
    bool focus_unfocused_ok{false};
    bool focus_nav_unfocused_ok{false};
    bool focus_nav_last_ok{false};
    bool focus_nav_first_ok{false};
    ViewId focus_next_view{0};
    bool focus_sources_next{false};
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
    std::optional<std::uint64_t> hovered_seq{};
    bool inject_mouse_pos{false};
    bool inject_mouse_down{false};
    bool inject_mouse_up{false};
    bool inject_right_down{false};
    bool inject_right_up{false};
    ImVec2 inject_pos{};
    bool id_conflict_detected{false};
    bool capture_row_rects{false};
    std::size_t row_rect_count{0};
    std::array<ImVec2, 4> row_rect_min{};
    std::array<ImVec2, 4> row_rect_max{};
    std::array<std::uint64_t, 4> row_seqs{};
    bool menu_open_for_tests{false};
    std::optional<std::uint64_t> menu_target_seq{};
    bool capture_menu_rects{false};
    bool menu_message_rect_valid{false};
    bool menu_record_rect_valid{false};
    ImVec2 menu_message_rect_min{};
    ImVec2 menu_message_rect_max{};
    ImVec2 menu_record_rect_min{};
    ImVec2 menu_record_rect_max{};
    bool menu_message_copied{false};
    bool menu_record_copied{false};
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

std::string window_title(const LogView& view) {
    return std::format("Log: {}###logview-{}", view.title(), view.id());
}

ImGuiID focused_dock_id(LogViewModel& model) {
    const LogView* focused = model.focused_view();
    if (focused == nullptr) {
        return 0;
    }
    const ImGuiWindow* window = ImGui::FindWindowByName(window_title(*focused).c_str());
    return window != nullptr ? window->DockId : 0;
}

ViewUiState& ui_for(UiState& state, const LogView& view) {
    auto [it, inserted] = state.view_ui.try_emplace(view.id());
    if (inserted) {
        std::snprintf(it->second.filter_buf, sizeof(it->second.filter_buf), "%s",
                      view.filter().criteria().text.c_str());
        std::snprintf(it->second.find_buf, sizeof(it->second.find_buf), "%s",
                      view.find().query().c_str());
    }
    return it->second;
}

void begin_view_rebuild(LogViewModel& model, LogView& view) {
    model.begin_filter_rebuild(view);
}

/// @brief Draw the toolbar's action buttons and the restart badge.
///
/// @param model View model owning the reset operations.
/// @param state UI state receiving the dialog flags and status text.
void draw_toolbar_actions(LogViewModel& model, UiState& state) {
    if (ImGui::Button("Open")) {
        state.open_open = true;
    }
    ImGui::SameLine();
    if (ImGui::Button("Clear")) {
        model.clear_history();
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
        state.view_ui.clear();
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

/// @brief Draw the ECO/NORMAL mode badge with its hover tooltip.
///
/// @param model View model supplying the resource guard state.
/// @param state UI state receiving the seen-ECO flag.
void draw_toolbar_mode(LogViewModel& model, UiState& state) {
    ImGui::SameLine();
    const bool eco = state.force_eco || model.guard().mode() == ResourceMode::Eco;
    if (eco) {
        state.seen_eco_badge = true;
    }
    ImGui::TextColored(eco ? ImVec4(1.0F, 0.72F, 0.25F, 1.0F) : ImVec4(0.55F, 0.80F, 0.95F, 1.0F),
                       "%s", eco ? "ECO" : "NORMAL");
    if (ImGui::IsItemHovered()) {
        ImGui::SetTooltip("mode=%s trigger=%s cpu=%.1f%% rate=%.0f/s", eco ? "eco" : "normal",
                          to_string(model.guard().trigger()).data(), model.guard().cpu_percent(),
                          model.guard().rate_ewma());
    }
}

/// @brief Draw the view and record counters line.
///
/// @param model View model supplying the counters and rate.
void draw_toolbar_counters(const LogViewModel& model) {
    const auto counters = model.counters();
    ImGui::SameLine();
    ImGui::Text("views=%zu live=%zu paused=%zu total=%llu received=%llu synthetic=%llu "
                "evicted=%llu rate=%.0f/s",
                model.view_count(), model.live_view_count(), model.paused_view_count(),
                static_cast<unsigned long long>(counters.total),
                static_cast<unsigned long long>(counters.received),
                static_cast<unsigned long long>(counters.synthetic),
                static_cast<unsigned long long>(counters.evicted), model.rate());
}

/// @brief Draw the import and export progress bars with their cancel action.
///
/// @param model View model owning import/export progress.
void draw_toolbar_progress(LogViewModel& model) {
    if (model.importing()) {
        char overlay[96]{};
        std::snprintf(overlay, sizeof(overlay), "import %llu malformed",
                      static_cast<unsigned long long>(model.import_stats().malformed));
        ImGui::ProgressBar(static_cast<float>(model.import_progress()), ImVec2(-1.0F, 0.0F),
                           overlay);
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
}

void draw_global_toolbar(LogViewModel& model, UiState& state) {
    if (!ImGui::Begin("Toolbar")) {
        ImGui::End();
        return;
    }
    draw_toolbar_actions(model, state);
    draw_toolbar_mode(model, state);
    draw_toolbar_counters(model);
    draw_toolbar_progress(model);
    if (!state.status.empty()) {
        ImGui::TextUnformatted(state.status.c_str());
    }
    ImGui::End();
}

void draw_sources(LogViewModel& model, UiState& state) {
    if (state.focus_sources_next) {
        ImGui::SetNextWindowFocus();
        state.focus_sources_next = false;
    }
    if (!ImGui::Begin("Sources")) {
        ImGui::End();
        return;
    }
    const bool can_open_live = model.live_view_count() < kMaxLiveViews;
    if (ImGui::Button("All logs")) {
        const ImGuiID target_dock = focused_dock_id(model);
        const auto created = model.create_view(ViewKind::All, {});
        if (created) {
            state.pending_dock[*created] = target_dock;
        } else {
            state.status = "LIVE view limit reached (8 including All) or allocation failed";
        }
    }
    if (!can_open_live) {
        ImGui::SameLine();
        ImGui::TextDisabled("LIVE limit");
    }
    ImGui::Separator();
    ImGui::TextUnformatted("Applications");
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
        ImGui::Text("%.*s (%llu)", static_cast<int>(app.text().size()), app.text().data(),
                    static_cast<unsigned long long>(app.record_count));
        ImGui::SameLine();
        if (ImGui::SmallButton("Open")) {
            const ImGuiID target_dock = focused_dock_id(model);
            const auto created = model.create_view(ViewKind::Source, std::string(app.text()));
            if (created) {
                state.pending_dock[*created] = target_dock;
            } else {
                state.status = "LIVE view limit reached (8 including All) or allocation failed";
            }
        }
        ImGui::PopID();
    }
    ImGui::End();
}

enum class NavIcon : std::uint8_t { First, Last, AutoScroll };

bool draw_nav_icon_button(const char* id, NavIcon icon, bool active, const char* tooltip,
                          UiState& state, std::size_t index) {
    ImGui::PushID(id);
    const float side = ImGui::GetFrameHeight();
    const bool pressed = ImGui::InvisibleButton("##nav", ImVec2(side, side));
    const bool hovered = ImGui::IsItemHovered();
    ImDrawList* draw = ImGui::GetWindowDrawList();
    const ImVec2 min = ImGui::GetItemRectMin();
    const ImVec2 max = ImGui::GetItemRectMax();
    if (index < 3U) {
        state.navigation_hit_areas = std::max(state.navigation_hit_areas, index + 1U);
    }
    if (hovered) {
        draw->AddRectFilled(min, max, ImGui::GetColorU32(ImGuiCol_ButtonHovered), 2.0F);
    }
    const ImU32 color = ImGui::GetColorU32(active ? ImGuiCol_ButtonActive : ImGuiCol_Text);
    const float cx = (min.x + max.x) * 0.5F;
    const float cy = (min.y + max.y) * 0.5F;
    const float unit = side * 0.22F;
    if (icon == NavIcon::First) {
        draw->AddLine(ImVec2(cx - unit * 1.5F, cy - unit), ImVec2(cx - unit * 1.5F, cy + unit),
                      color, 2.0F);
        draw->AddTriangleFilled(ImVec2(cx - unit * 0.4F, cy - unit),
                                ImVec2(cx - unit * 0.4F, cy + unit), ImVec2(cx + unit, cy), color);
    } else if (icon == NavIcon::Last) {
        draw->AddTriangleFilled(ImVec2(cx - unit, cy - unit), ImVec2(cx - unit, cy + unit),
                                ImVec2(cx - unit * 0.4F, cy), color);
        draw->AddLine(ImVec2(cx + unit * 1.5F, cy - unit), ImVec2(cx + unit * 1.5F, cy + unit),
                      color, 2.0F);
    } else {
        draw->AddLine(ImVec2(cx, cy - unit * 1.4F), ImVec2(cx, cy + unit * 0.5F), color, 2.0F);
        draw->AddTriangleFilled(ImVec2(cx - unit * 0.7F, cy + unit * 0.2F),
                                ImVec2(cx + unit * 0.7F, cy + unit * 0.2F),
                                ImVec2(cx, cy + unit * 1.2F), color);
        draw->AddLine(ImVec2(cx - unit * 1.2F, cy + unit * 1.5F),
                      ImVec2(cx + unit * 1.2F, cy + unit * 1.5F), color, 2.0F);
    }
    if (hovered) {
        ImGui::SetTooltip("%s", tooltip);
    }
    ImGui::PopID();
    return pressed;
}

void draw_navigation_controls(LogView& view, UiState& state, ViewUiState& ui) {
    const bool capture_rects = state.view_test && view.id() == state.view_test_focus_id;
    if (draw_nav_icon_button("first", NavIcon::First, false, "First message (Ctrl+Home)", state,
                             0)) {
        ui::navigate_first(view, ui);
    }
    if (capture_rects) {
        state.nav_rect_min[0] = ImGui::GetItemRectMin();
        state.nav_rect_max[0] = ImGui::GetItemRectMax();
    }
    ImGui::SameLine();
    if (draw_nav_icon_button("last", NavIcon::Last, false, "Last message (Ctrl+End)", state, 1)) {
        ui::navigate_last(view, ui);
    }
    if (capture_rects) {
        state.nav_rect_min[1] = ImGui::GetItemRectMin();
        state.nav_rect_max[1] = ImGui::GetItemRectMax();
    }
    ImGui::SameLine();
    const char* tooltip = ui.auto_scroll ? "Auto-scroll: on (click to stop following)"
                                         : "Auto-scroll: off (click to follow latest)";
    if (draw_nav_icon_button("auto", NavIcon::AutoScroll, ui.auto_scroll, tooltip, state, 2)) {
        ui::toggle_auto_scroll(view, ui);
    }
    if (capture_rects) {
        state.nav_rect_min[2] = ImGui::GetItemRectMin();
        state.nav_rect_max[2] = ImGui::GetItemRectMax();
        state.nav_rects_valid = true;
    }
    state.seen_navigation_controls = true;
    if (ui.auto_scroll) {
        state.seen_auto_scroll_active = true;
    }
}

/// @brief Draw the live/paused state badge and the pause action.
///
/// @param view View whose state is shown.
/// @param state UI state receiving the pause-badge observation.
void draw_view_state_badge(LogView& view, UiState& state) {
    if (view.live()) {
        if (ImGui::Button(view.paused() ? "Resume" : "Pause")) {
            view.set_paused(!view.paused());
        }
        ImGui::SameLine();
        if (view.paused()) {
            state.seen_pause_badge = true;
            ImGui::TextColored(ImVec4(1.0F, 0.65F, 0.25F, 1.0F), "PAUSED (%llu new)",
                               static_cast<unsigned long long>(view.new_since_pause()));
            if (view.overwritten_while_paused() > 0U) {
                ImGui::SameLine();
                ImGui::TextColored(
                    ImVec4(1.0F, 0.40F, 0.40F, 1.0F), "(%llu overwritten)",
                    static_cast<unsigned long long>(view.overwritten_while_paused()));
            }
        } else {
            ImGui::TextColored(ImVec4(0.36F, 0.85F, 0.58F, 1.0F), "LIVE");
        }
    } else {
        ImGui::TextColored(ImVec4(1.0F, 0.65F, 0.25F, 1.0F), "PAUSED SNAPSHOT");
    }
}

/// @brief Draw the navigation controls, clone/save and bookmark actions.
///
/// @param view View acted on.
/// @param state UI state receiving the dialog flags.
/// @param ui Per-view UI state receiving the scroll request.
void draw_view_actions(LogView& view, UiState& state, ViewUiState& ui) {
    ImGui::SameLine();
    draw_navigation_controls(view, state, ui);
    ImGui::SameLine();
    if (ImGui::Button("Clone")) {
        state.clone_open = true;
        state.clone_target = view.id();
        state.clone_mode = CloneMode::Live;
        state.clone_options = CloneOptions{};
        state.clone_status.clear();
    }
    ImGui::SameLine();
    if (ImGui::Button("Save selected")) {
        state.open_save = true;
        state.save_view_id = view.id();
    }
    ImGui::SameLine();
    if (ImGui::Button("Prev B")) {
        if (const auto seq = view.previous_bookmark(view.last_selected()); seq.has_value()) {
            view.select_only(*seq);
            ui.scroll_to = seq;
        }
    }
    ImGui::SameLine();
    if (ImGui::Button("Next B")) {
        if (const auto seq = view.next_bookmark(view.last_selected()); seq.has_value()) {
            view.select_only(*seq);
            ui.scroll_to = seq;
        }
    }
    ImGui::SameLine();
    if (ImGui::Button("Prev W/E")) {
        if (const auto seq = view.previous_attention(view.last_selected()); seq.has_value()) {
            view.select_only(*seq);
            ui.scroll_to = seq;
        }
    }
    ImGui::SameLine();
    if (ImGui::Button("Next W/E")) {
        if (const auto seq = view.next_attention(view.last_selected()); seq.has_value()) {
            view.select_only(*seq);
            ui.scroll_to = seq;
        }
    }
    if (view.time().anchor_ns.has_value()) {
        ImGui::SameLine();
        if (ImGui::Button("Clear T0")) {
            view.time().clear_anchor();
        }
    }
}

/// @brief Draw the level visibility checkboxes.
///
/// @param model View model owning the rebuild scheduling.
/// @param view View whose filter is edited.
void draw_level_filters(LogViewModel& model, LogView& view) {
    ImGui::Separator();
    for (std::size_t i = 0; i < 7U; ++i) {
        if (i > 0U) {
            ImGui::SameLine();
        }
        bool visible = view.filter().criteria().levels[i];
        ImGui::PushID(static_cast<int>(i));
        if (ImGui::Checkbox(kLevelNames[i], &visible)) {
            view.filter().criteria().levels[i] = visible;
            begin_view_rebuild(model, view);
        }
        ImGui::PopID();
    }
}

/// @brief Draw the message text filter input.
///
/// @param model View model owning the rebuild scheduling.
/// @param view View whose filter is edited.
/// @param ui Per-view UI state holding the filter buffer.
void draw_text_filter(LogViewModel& model, LogView& view, ViewUiState& ui) {
    ImGui::SameLine();
    ImGui::SetNextItemWidth(220.0F);
    if (ImGui::InputTextWithHint("##filter", "message filter", ui.filter_buf,
                                 sizeof(ui.filter_buf))) {
        view.filter().criteria().text = ui.filter_buf;
        begin_view_rebuild(model, view);
    }
}

/// @brief Draw the source filter button and its popup.
///
/// @param model View model supplying the application registry.
/// @param view View whose source mask is edited.
/// @param state UI state handling the auto-open request.
void draw_source_filter(LogViewModel& model, LogView& view, UiState& state) {
    if (view.kind() == ViewKind::All) {
        ImGui::SameLine();
        if (ImGui::Button("Sources...")) {
            ImGui::OpenPopup("sources");
        }
        if (state.auto_open_sources_popup) {
            ImGui::OpenPopup("sources");
            state.auto_open_sources_popup = false;
        }
        if (ImGui::BeginPopup("sources")) {
            ImGui::TextUnformatted("Visible sources");
            bool changed = false;
            for (std::size_t slot = 0; slot < AppRegistry::capacity; ++slot) {
                const AppSlot& app = model.ring().apps().at(slot);
                if (!app.used) {
                    continue;
                }
                ImGui::PushID(static_cast<int>(slot));
                bool visible = view.filter().criteria().apps[slot];
                if (ImGui::Checkbox("##source", &visible)) {
                    view.filter().criteria().apps[slot] = visible;
                    bool any_hidden = false;
                    for (const bool shown : view.filter().criteria().apps) {
                        any_hidden = any_hidden || !shown;
                    }
                    view.filter().criteria().app_filter_active = any_hidden;
                    changed = true;
                }
                ImGui::SameLine();
                ImGui::Text("%.*s", static_cast<int>(app.text().size()), app.text().data());
                ImGui::PopID();
            }
            if (ImGui::Button("Show all")) {
                view.filter().criteria().apps.fill(true);
                view.filter().criteria().app_filter_active = false;
                changed = true;
            }
            ImGui::SameLine();
            if (ImGui::Button("Hide all")) {
                view.filter().criteria().apps.fill(false);
                view.filter().criteria().app_filter_active = true;
                changed = true;
            }
            if (changed) {
                begin_view_rebuild(model, view);
            }
            ImGui::EndPopup();
        }
    } else {
        ImGui::SameLine();
        ImGui::TextDisabled("source: %s", view.source().c_str());
    }
}

/// @brief Draw the find input and its match counter.
///
/// @param model View model owning the find operation.
/// @param view View whose find state is driven.
/// @param ui Per-view UI state holding the find buffer.
void draw_find_controls(LogViewModel& model, LogView& view, ViewUiState& ui) {
    ImGui::SetNextItemWidth(220.0F);
    if (ui.focus_find) {
        ImGui::SetKeyboardFocusHere();
        ui.focus_find = false;
    }
    if (ImGui::InputTextWithHint("##find", "find (Ctrl+F)", ui.find_buf, sizeof(ui.find_buf),
                                 ImGuiInputTextFlags_EnterReturnsTrue)) {
        model.begin_find(view, ui.find_buf);
        while (view.find().active()) {
            static_cast<void>(view.find().step(view.filter(), view.history(), 1'000'000U));
        }
        model.focus_view(view.id());
        if (const auto seq = view.find().next(); seq.has_value()) {
            view.select_only(*seq);
            ui.scroll_to = seq;
        }
    }
    if (view.find().count() > 0U) {
        ImGui::SameLine();
        ImGui::Text("%zu matches", view.find().count());
    } else if (!view.find().query().empty() && !view.find().active()) {
        ImGui::SameLine();
        ImGui::TextColored(ImVec4(0.9F, 0.5F, 0.4F, 1.0F), "0 matches");
    }
}

/// @brief Draw the timestamp, delta and anomaly threshold controls.
///
/// @param view View whose display modes are edited.
void draw_time_controls(LogView& view) {
    ImGui::SameLine();
    int timestamp_mode = static_cast<int>(view.timestamp_mode());
    ImGui::SetNextItemWidth(110.0F);
    if (ImGui::Combo("##time", &timestamp_mode, "Absolute\0Since boot\0Relative T0\0")) {
        view.timestamp_mode() = static_cast<TimestampMode>(timestamp_mode);
    }
    ImGui::SameLine();
    int delta_mode = static_cast<int>(view.delta_mode());
    ImGui::SetNextItemWidth(120.0F);
    if (ImGui::Combo("##delta", &delta_mode, "Previous\0Per app\0From T0\0")) {
        view.delta_mode() = static_cast<DeltaMode>(delta_mode);
    }
    ImGui::SameLine();
    int threshold_ms = static_cast<int>(view.time().anomaly_threshold_ns / 1'000'000LL);
    ImGui::SetNextItemWidth(80.0F);
    if (ImGui::InputInt("##anomaly", &threshold_ms) && threshold_ms >= 0) {
        view.time().anomaly_threshold_ns = static_cast<std::int64_t>(threshold_ms) * 1'000'000LL;
    }
}

/// @brief Draw the filter rebuild progress bar.
///
/// @param view View whose rebuild is reported.
void draw_rebuild_progress(LogView& view) {
    if (view.filter().rebuilding()) {
        const FilterIndex::Progress progress = view.filter().progress();
        char overlay[96]{};
        std::snprintf(overlay, sizeof(overlay), "filter rebuild %llu/%llu",
                      static_cast<unsigned long long>(progress.scanned),
                      static_cast<unsigned long long>(progress.total));
        ImGui::ProgressBar(static_cast<float>(progress.ratio()), ImVec2(-1.0F, 0.0F), overlay);
    }
}

/// @brief Draw the visible/total record counters line.
///
/// @param model View model supplying the rate.
/// @param view View whose counters are shown.
/// @param state UI state receiving the counters-seen observation.
void draw_view_counters(const LogViewModel& model, const LogView& view, UiState& state) {
    ImGui::Text("visible=%zu total=%zu %s rate=%.0f/s", view.display_count(), view.history().size(),
                view.live() ? "LIVE" : "PAUSED", model.rate());
    state.seen_view_counters = true;
}

void draw_local_controls(LogViewModel& model, LogView& view, UiState& state, ViewUiState& ui) {
    draw_view_state_badge(view, state);
    draw_view_actions(view, state, ui);
    draw_level_filters(model, view);
    draw_text_filter(model, view, ui);
    draw_source_filter(model, view, state);
    draw_find_controls(model, view, ui);
    draw_time_controls(view);
    draw_rebuild_progress(view);
    draw_view_counters(model, view, state);
}

std::optional<std::uint64_t> previous_frame(const LogView& view, std::size_t index) {
    while (index > 0U) {
        --index;
        const std::uint64_t seq = view.display_at(index);
        if (const auto record = view.history().find(seq);
            record.has_value() && record->is_frame()) {
            return record->frame->timestamp_ns;
        }
    }
    return std::nullopt;
}

void select_row(LogView& view, std::uint64_t seq) {
    const ImGuiIO& io = ImGui::GetIO();
    if (io.KeyShift) {
        view.select_to(seq);
    } else if (io.KeyCtrl) {
        view.toggle_selection(seq);
    } else {
        view.select_only(seq);
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

std::int64_t row_delta(const LogView& view, const RecordView& record,
                       const RowContext& context) noexcept {
    if (!record.is_frame()) {
        return 0;
    }
    if (view.delta_mode() == DeltaMode::PerApplication) {
        return record.prev_same_app_ns;
    }
    if (view.delta_mode() == DeltaMode::FromAnchor && view.time().anchor_ns.has_value()) {
        return static_cast<std::int64_t>(record.frame->timestamp_ns) -
               static_cast<std::int64_t>(*view.time().anchor_ns);
    }
    if (view.delta_mode() == DeltaMode::PreviousVisible && context.have_previous) {
        return static_cast<std::int64_t>(record.frame->timestamp_ns) -
               static_cast<std::int64_t>(context.previous_stamp);
    }
    return 0;
}

void draw_message_cell(const LogView& view, const RecordView& record) {
    const std::string& query = view.find().query();
    if (!record.is_frame()) {
        ImGui::Text("[gap] %llu lost (%s)", static_cast<unsigned long long>(record.gap_count),
                    to_string(record.kind).data());
        return;
    }
    const std::string_view message = record.frame->payload_text();
    if (query.empty() || !view.find().contains(record.seq) || message.empty()) {
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

void draw_row_tooltip(const RecordView& record, UiState& state) {
    state.hovered_seq = record.seq;
    ImGui::BeginTooltip();
    if (record.is_frame()) {
        const std::string_view message = record.frame->payload_text();
        ImGui::TextUnformatted(message.data(), message.data() + message.size());
    } else {
        ImGui::Text("[gap] %llu lost (%s)", static_cast<unsigned long long>(record.gap_count),
                    to_string(record.kind).data());
    }
    ImGui::Separator();
    ImGui::TextDisabled("Ctrl+C copies this record; right-click for more");
    ImGui::EndTooltip();
}

void draw_row_context_menu(LogViewModel& model, LogView& view, const RecordView& record,
                           UiState& state) {
    if (state.menu_open_for_tests && state.menu_target_seq.has_value() &&
        *state.menu_target_seq == record.seq) {
        ImGui::OpenPopup("row-menu");
        state.menu_open_for_tests = false;
        state.menu_target_seq.reset();
    }
    if (ImGui::IsItemClicked(ImGuiMouseButton_Right)) {
        model.focus_view(view.id());
        view.select_only(record.seq);
    }
    if (!ImGui::BeginPopupContextItem("row-menu")) {
        return;
    }
    const bool copy_message = ImGui::MenuItem("Copy message");
    if (state.capture_menu_rects && !state.menu_message_rect_valid) {
        state.menu_message_rect_min = ImGui::GetItemRectMin();
        state.menu_message_rect_max = ImGui::GetItemRectMax();
        state.menu_message_rect_valid = true;
    }
    if (copy_message) {
        if (record.is_frame()) {
            const std::string text(record.frame->payload_text());
            ImGui::SetClipboardText(text.c_str());
        } else {
            char text[96]{};
            std::snprintf(text, sizeof(text), "[gap] %llu lost (%s)",
                          static_cast<unsigned long long>(record.gap_count),
                          to_string(record.kind).data());
            ImGui::SetClipboardText(text);
        }
        state.status = "message copied";
        state.menu_message_copied = true;
    }
    const bool copy_record = ImGui::MenuItem("Copy full record");
    if (state.capture_menu_rects && !state.menu_record_rect_valid) {
        state.menu_record_rect_min = ImGui::GetItemRectMin();
        state.menu_record_rect_max = ImGui::GetItemRectMax();
        state.menu_record_rect_valid = true;
    }
    if (copy_record) {
        model.focus_view(view.id());
        view.select_only(record.seq);
        const std::string text = ui::copy_selected(model);
        if (!text.empty()) {
            ImGui::SetClipboardText(text.c_str());
            state.status = "record copied";
            state.menu_record_copied = true;
        }
    }
    ImGui::EndPopup();
}

void capture_row_rect(const LogView& view, const RecordView& record, UiState& state) {
    const bool active =
        state.input_test || (state.view_test && view.id() == state.view_test_focus_id);
    if (!active || !state.capture_row_rects || state.row_rect_count >= state.row_rect_min.size()) {
        return;
    }
    state.row_rect_min[state.row_rect_count] = ImGui::GetItemRectMin();
    state.row_rect_max[state.row_rect_count] = ImGui::GetItemRectMax();
    state.row_seqs[state.row_rect_count] = record.seq;
    ++state.row_rect_count;
}

void draw_timestamp_cell(const LogView& view, const RecordView& record) {
    const FormattedTime stamp = TimeFormatter::format_timestamp(record.frame->timestamp_ns,
                                                                view.timestamp_mode(), view.time());
    if (!stamp.unknown) {
        ImGui::TextUnformatted(stamp.text.data());
        return;
    }
    ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.0F, 0.55F, 0.35F, 1.0F));
    ImGui::TextUnformatted(stamp.text.data());
    ImGui::PopStyleColor();
}

void draw_app_cell(LogViewModel& model, const RecordView& record, bool critical) {
    if (!record.is_frame()) {
        ImGui::TextUnformatted("-");
        return;
    }
    const std::size_t slot = model.ring().apps().find(record.frame->app_id_text());
    if (slot >= AppRegistry::capacity) {
        ImGui::TextUnformatted("-");
        return;
    }
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
}

void draw_record_row(LogViewModel& model, LogView& view, UiState& state, const RecordView& record,
                     float row_height, RowContext& context) {
    char row_id[32]{};
    std::snprintf(row_id, sizeof(row_id), "##row%llu", static_cast<unsigned long long>(record.seq));
    ImGui::PushID(row_id);
    const bool selected = view.is_selected(record.seq);
    const bool bookmarked = view.is_bookmarked(record.seq);
    const bool is_frame = record.is_frame();
    const nexigen::core::LogLevel level = record.frame->log_level;
    const LevelStyle& style = model.colors().level(level);
    const bool critical =
        model.colors().mode() == ColorMode::Criticality && ColorManager::is_critical(level);
    ImVec4 text_color = to_imgui(style.text);
    const bool is_anchor =
        view.time().anchor_ns.has_value() && is_frame &&
        static_cast<std::int64_t>(record.frame->timestamp_ns) == *view.time().anchor_ns;
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
        model.focus_view(view.id());
        select_row(view, record.seq);
    }
    capture_row_rect(view, record, state);
    if (ImGui::IsItemHovered()) {
        draw_row_tooltip(record, state);
    }
    draw_row_context_menu(model, view, record, state);
    ImGui::SameLine();
    ImGui::TextUnformatted(bookmarked ? "B" : " ");
    ImGui::TableSetColumnIndex(1);
    ImGui::Text("%llu", static_cast<unsigned long long>(record.seq));
    ImGui::TableSetColumnIndex(2);
    draw_timestamp_cell(view, record);
    ImGui::TableSetColumnIndex(3);
    if (is_frame && view.time().mixed_boot) {
        ImGui::TextUnformatted("-");
    } else if (is_frame) {
        const std::int64_t delta = row_delta(view, record, context);
        if (TimeFormatter::is_anomalous(delta, view.time().anomaly_threshold_ns)) {
            ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.0F, 0.55F, 0.35F, 1.0F));
            ImGui::TextUnformatted(TimeFormatter::format_delta(delta).text.data());
            ImGui::PopStyleColor();
        } else {
            ImGui::TextUnformatted(TimeFormatter::format_delta(delta).text.data());
        }
        context.previous_stamp = record.frame->timestamp_ns;
        context.have_previous = true;
    } else {
        ImGui::TextUnformatted("-");
    }
    ImGui::TableSetColumnIndex(4);
    draw_app_cell(model, record, critical);
    ImGui::TableSetColumnIndex(5);
    ImGui::TextUnformatted("-");
    ImGui::TableSetColumnIndex(6);
    ImGui::TextUnformatted(nexigen::core::to_string(level).data());
    ImGui::TableSetColumnIndex(7);
    draw_message_cell(view, record);
    ImGui::PopStyleColor();
    ImGui::PopID();
}

void apply_table_scroll(LogView& view, ViewUiState& ui, int rows, float row_height) {
    const float scroll_max = ImGui::GetScrollMaxY();
    const float scroll = ImGui::GetScrollY();
    const bool at_bottom = scroll + 1.0F >= scroll_max;
    const bool drag_or_key_scroll =
        (!at_bottom && ImGui::IsWindowHovered() && ImGui::IsAnyMouseDown()) ||
        (!at_bottom && ImGui::IsWindowFocused() &&
         (ImGui::IsKeyPressed(ImGuiKey_PageUp) || ImGui::IsKeyPressed(ImGuiKey_UpArrow)));
    const bool wheel_away = ImGui::IsWindowHovered() && ImGui::GetIO().MouseWheel > 0.0F;
    if (ui::manual_scroll_disables_auto_scroll(ui.auto_scroll, drag_or_key_scroll || wheel_away)) {
        ui.auto_scroll = false;
    }
    if (ui.scroll_to.has_value() && rows > 0) {
        const std::size_t display_limit = static_cast<std::size_t>(rows);
        for (std::size_t i = 0; i < display_limit; ++i) {
            if (view.display_at(i) == *ui.scroll_to) {
                ImGui::SetScrollY(static_cast<float>(i) * row_height);
                break;
            }
        }
        ui.scroll_to.reset();
    } else if (ui::auto_scroll_active(ui.auto_scroll, view.live(), view.paused())) {
        ImGui::SetScrollY(scroll_max);
    }
}

/// @brief Draw one Log window.
/// @return `true` when the window close button was pressed.
/// @brief Apply pending dock, float and focus requests to the next window.
///
/// @param view View whose window is about to be created.
/// @param state UI state carrying the pending requests.
void apply_pending_window_state(const LogView& view, UiState& state) {
    if (const auto pending = state.pending_dock.find(view.id());
        pending != state.pending_dock.end()) {
        if (pending->second != 0) {
            ImGui::SetNextWindowDockID(pending->second, ImGuiCond_Always);
        }
        state.pending_dock.erase(pending);
    }
    if (state.float_view == view.id()) {
        ImGui::SetNextWindowDockID(0, ImGuiCond_Always);
        state.float_view = 0;
        state.seen_floating_view = true;
    }
    if (state.focus_next_view == view.id()) {
        ImGui::SetNextWindowFocus();
        state.focus_next_view = 0;
    }
}

/// @brief Update docked/focused bookkeeping for the current log window.
///
/// @param model View model receiving the focus request.
/// @param view View owning the window.
/// @param state UI state receiving the dock/focus observations.
void update_log_window_focus(LogViewModel& model, LogView& view, UiState& state) {
    if (ImGui::IsWindowDocked()) {
        ++state.docked_views;
    }
    if (ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows)) {
        model.focus_view(view.id());
        state.log_view_focused = true;
    }
}

/// @brief Draw the clipped records table of one view.
///
/// @param model View model supplying the row renderer.
/// @param view View whose history is displayed.
/// @param state UI state receiving hover and row-rect observations.
/// @param ui Per-view UI state carrying the scroll request.
void draw_records_table(LogViewModel& model, LogView& view, UiState& state, ViewUiState& ui) {
    constexpr ImGuiTableFlags flags = ImGuiTableFlags_Resizable | ImGuiTableFlags_RowBg |
                                      ImGuiTableFlags_BordersOuter | ImGuiTableFlags_BordersInnerV |
                                      ImGuiTableFlags_ScrollY | ImGuiTableFlags_SizingStretchProp;
    if (!ImGui::BeginTable("records", 8, flags)) {
        return;
    }
    draw_table_header();
    state.hovered_seq.reset();
    const std::size_t display = view.display_count();
    const int rows = static_cast<int>(std::min<std::size_t>(display, 2'000'000'000U));
    ImGuiListClipper clipper;
    clipper.Begin(rows, ImGui::GetTextLineHeightWithSpacing());
    const float row_height = ImGui::GetTextLineHeightWithSpacing();
    RowContext context{};
    if (rows > 0 && view.delta_mode() == DeltaMode::PreviousVisible && clipper.DisplayStart > 0) {
        if (const auto stamp = previous_frame(view, static_cast<std::size_t>(clipper.DisplayStart));
            stamp.has_value()) {
            context.previous_stamp = *stamp;
            context.have_previous = true;
        }
    }
    while (clipper.Step()) {
        for (int row = clipper.DisplayStart; row < clipper.DisplayEnd; ++row) {
            const std::uint64_t seq = view.display_at(static_cast<std::size_t>(row));
            if (const auto record = view.history().find(seq); record.has_value()) {
                draw_record_row(model, view, state, *record, row_height, context);
            }
        }
    }
    apply_table_scroll(view, ui, rows, row_height);
    ImGui::EndTable();
}

bool draw_log_view(LogViewModel& model, LogView& view, UiState& state) {
    ViewUiState& ui = ui_for(state, view);
    const std::string title = window_title(view);
    apply_pending_window_state(view, state);
    bool open = true;
    if (!ImGui::Begin(title.c_str(), &open)) {
        ImGui::End();
        return !open;
    }
    update_log_window_focus(model, view, state);
    draw_local_controls(model, view, state, ui);
    draw_records_table(model, view, state, ui);
    ImGui::End();
    return !open;
}

std::int64_t selected_delta(const LogView& view, const RecordView& record) {
    if (!record.is_frame()) {
        return 0;
    }
    if (view.delta_mode() == DeltaMode::PerApplication) {
        return record.prev_same_app_ns;
    }
    if (view.delta_mode() == DeltaMode::FromAnchor && view.time().anchor_ns.has_value()) {
        return static_cast<std::int64_t>(record.frame->timestamp_ns) -
               static_cast<std::int64_t>(*view.time().anchor_ns);
    }
    if (view.delta_mode() == DeltaMode::PreviousVisible) {
        for (std::size_t i = 0; i < view.display_count(); ++i) {
            if (view.display_at(i) == record.seq) {
                if (const auto stamp = previous_frame(view, i); stamp.has_value()) {
                    return static_cast<std::int64_t>(record.frame->timestamp_ns) -
                           static_cast<std::int64_t>(*stamp);
                }
                break;
            }
        }
    }
    return 0;
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
    LogView* view = model.focused_view();
    const auto selected = view != nullptr ? view->last_selected() : std::nullopt;
    if (!selected.has_value()) {
        ImGui::TextUnformatted("Select a record to inspect.");
        ImGui::End();
        return;
    }
    const auto record = view->history().find(*selected);
    if (!record.has_value()) {
        ImGui::TextUnformatted("The selected record has been evicted.");
        ImGui::End();
        return;
    }
    state.inspected_seq = record->seq;
    const FormattedTime stamp = TimeFormatter::format_timestamp(
        record->frame->timestamp_ns, view->timestamp_mode(), view->time());
    const FormattedTime delta = TimeFormatter::format_delta(selected_delta(*view, *record));
    ImGui::Text("index: %llu", static_cast<unsigned long long>(record->seq));
    ImGui::Text("kind: %s", to_string(record->kind).data());
    ImGui::Text("timestamp: %s", stamp.text.data());
    ImGui::Text("delta: %s", delta.text.data());
    ImGui::Text("application: %.*s", static_cast<int>(record->frame->app_id_text().size()),
                record->frame->app_id_text().data());
    ImGui::Text("context: %u", record->frame->context_id);
    state.seen_inspector_meta = true;
    ImGui::Text("level: %s", nexigen::core::to_string(record->frame->log_level).data());
    ImGui::Text("thread: %u  pid: %u", record->frame->thread_id, record->frame->pid);
    ImGui::Text("publisher drop counter: %u", record->frame->dropped_counter);
    ImGui::Text("wire version: %u", record->frame->version);
    if (payload_may_be_truncated(*record->frame)) {
        ImGui::TextColored(ImVec4(1.0F, 0.68F, 0.30F, 1.0F),
                           "Payload is at capacity; the producer may have truncated it.");
    }
    ImGui::Separator();
    ImGui::TextWrapped("%.*s", static_cast<int>(record->frame->payload_text().size()),
                       record->frame->payload_text().data());
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
    if (const LogView* view = model.focused_view(); view != nullptr) {
        const Minimap& minimap = view->minimap();
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
                draw->AddRectFilled(ImVec2(origin.x + size.x - 6.0F, y0),
                                    ImVec2(origin.x + size.x, y1), IM_COL32(240, 210, 70, 230));
            }
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
    ImGui::Text("time base: %s",
                model.base_time().time_base_attached ? "attached" : "sampled offset");
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
    ImGui::Text("selected: %llu",
                static_cast<unsigned long long>(
                    model.focused_view() != nullptr ? model.focused_view()->selected_count() : 0U));
    ImGui::End();
}

void draw_workspace(LogViewModel& model, UiState& state) {
    if (model.view_count() != 0U) {
        return;
    }
    if (!ImGui::Begin("Workspace")) {
        ImGui::End();
        return;
    }
    ImGui::TextUnformatted("No log views are open.");
    ImGui::TextUnformatted("Open `All logs` or an application from the Sources panel.");
    if (ImGui::Button("Open All logs")) {
        const auto created = model.create_view(ViewKind::All, {});
        if (!created) {
            state.status = "LIVE view limit reached (8 including All)";
        }
    }
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
        const ViewId target = state.save_view_id != 0U ? state.save_view_id : model.focused_id();
        const auto started = model.start_export(target, state.save_path, state.save_format);
        if (started) {
            state.status = "export started";
        } else if (model.exporting()) {
            state.status = "export failed: another export is already running";
        } else {
            state.status = "export failed to start (select records first)";
        }
        ImGui::CloseCurrentPopup();
    }
    ImGui::SameLine();
    if (ImGui::Button("Cancel")) {
        ImGui::CloseCurrentPopup();
    }
    ImGui::EndPopup();
}

/// @brief Draw the clone dialog header, mode radios and the copy-state label.
///
/// @param source View being cloned.
/// @param state UI state holding the selected clone mode.
void draw_clone_header(const LogView& source, UiState& state) {
    ImGui::Text("Clone: Log: %s", source.title().c_str());
    int mode = static_cast<int>(state.clone_mode);
    if (ImGui::RadioButton("LIVE", &mode, static_cast<int>(CloneMode::Live))) {
        state.clone_mode = CloneMode::Live;
    }
    ImGui::SameLine();
    if (ImGui::RadioButton("PAUSED", &mode, static_cast<int>(CloneMode::Paused))) {
        state.clone_mode = CloneMode::Paused;
    }
    ImGui::Separator();
    ImGui::TextUnformatted("Copy state");
}

/// @brief Draw the clone dialog's copy-state checkboxes.
///
/// @param state UI state holding the clone options.
void draw_clone_options(UiState& state) {
    ImGui::Checkbox("Levels", &state.clone_options.levels);
    ImGui::SameLine();
    ImGui::Checkbox("Text filter", &state.clone_options.text);
    ImGui::Checkbox("Timestamp mode", &state.clone_options.timestamp_mode);
    ImGui::SameLine();
    ImGui::Checkbox("Delta mode", &state.clone_options.delta_mode);
    ImGui::Checkbox("Anomaly threshold", &state.clone_options.anomaly_threshold);
    ImGui::SameLine();
    ImGui::Checkbox("Selection", &state.clone_options.selection);
    ImGui::Checkbox("Bookmarks", &state.clone_options.bookmarks);
    ImGui::SameLine();
    ImGui::Checkbox("T0", &state.clone_options.t0);
    ImGui::Checkbox("Find query", &state.clone_options.find);
}

/// @brief Draw the mode hint and the last clone status line.
///
/// @param state UI state holding the mode and status text.
void draw_clone_hint(const UiState& state) {
    if (state.clone_mode == CloneMode::Live) {
        ImGui::TextDisabled("LIVE clone follows the shared tail and starts at the newest record.");
    } else {
        ImGui::TextDisabled("PAUSED clone copies at most 1 MiB of the newest records.");
    }
    if (!state.clone_status.empty()) {
        ImGui::TextColored(ImVec4(1.0F, 0.55F, 0.35F, 1.0F), "%s", state.clone_status.c_str());
    }
}

/// @brief Draw the clone/cancel buttons and apply the clone request.
///
/// @param model View model owning the clone operation.
/// @param state UI state carrying the request and receiving the outcome.
void draw_clone_actions(LogViewModel& model, UiState& state) {
    if (ImGui::Button("Clone")) {
        const ImGuiID target_dock = focused_dock_id(model);
        const auto created =
            model.clone_view(state.clone_target, state.clone_mode, state.clone_options);
        if (created) {
            state.pending_dock[*created] = target_dock;
            state.status = model.last_clone_status();
            if (state.status.empty()) {
                state.status = "clone created";
            }
            state.clone_status.clear();
            ImGui::CloseCurrentPopup();
        } else if (state.clone_mode == CloneMode::Live) {
            state.clone_status = model.live_view_count() >= kMaxLiveViews
                                     ? "LIVE view limit reached (8 including All)"
                                     : "view allocation failed";
        } else {
            state.clone_status = model.paused_view_count() >= kMaxPausedViews
                                     ? "PAUSED view limit reached (16)"
                                     : "snapshot allocation failed";
        }
    }
    ImGui::SameLine();
    if (ImGui::Button("Cancel")) {
        ImGui::CloseCurrentPopup();
    }
}

void draw_clone_modal(LogViewModel& model, UiState& state) {
    if (state.clone_open) {
        ImGui::OpenPopup("Clone view");
        state.clone_open = false;
    }
    if (!ImGui::BeginPopupModal("Clone view", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
        return;
    }
    const LogView* source = model.view(state.clone_target);
    if (source == nullptr) {
        ImGui::TextUnformatted("The source view is no longer open.");
        if (ImGui::Button("Close")) {
            ImGui::CloseCurrentPopup();
        }
        ImGui::EndPopup();
        return;
    }
    draw_clone_header(*source, state);
    draw_clone_options(state);
    draw_clone_hint(state);
    draw_clone_actions(model, state);
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
    if (LogView* view = model.focused_view(); view != nullptr) {
        int threshold_ms = static_cast<int>(view->time().anomaly_threshold_ns / 1'000'000LL);
        if (ImGui::InputInt("anomaly threshold (ms)", &threshold_ms) && threshold_ms >= 0) {
            view->time().anomaly_threshold_ns =
                static_cast<std::int64_t>(threshold_ms) * 1'000'000LL;
        }
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
    draw_clone_modal(model, state);
}

} // namespace

int run_ui_actions_test(LogViewModel& model, const UiOptions& options) {
    const std::size_t history = model.ring().size();
    const std::size_t visible = model.display_count();
    LogView* focused = model.focused_view();
    if (visible == 0U || focused == nullptr) {
        std::fprintf(stderr, "UI_ACTIONS failed: no visible record\n");
        return 1;
    }
    model.select_only(focused->display_at(0));
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

void draw_default_layout(ImGuiID dockspace, const LogViewModel& model) {
    // Rebuild from a clean node so a stale layout from an older viewer version
    // cannot accumulate dock depth across smoke runs or upgrades.
    ImGui::DockBuilderRemoveNode(dockspace);
    ImGui::DockBuilderAddNode(dockspace, ImGuiDockNodeFlags_DockSpace);
    ImGui::DockBuilderSetNodeSize(dockspace, ImGui::GetMainViewport()->Size);
    ImGuiID main = dockspace;
    ImGuiID top = ImGui::DockBuilderSplitNode(main, ImGuiDir_Up, 0.08F, nullptr, &main);
    ImGuiID bottom = ImGui::DockBuilderSplitNode(main, ImGuiDir_Down, 0.28F, nullptr, &main);
    ImGuiID left = ImGui::DockBuilderSplitNode(main, ImGuiDir_Left, 0.22F, nullptr, &main);
    ImGuiID right = ImGui::DockBuilderSplitNode(main, ImGuiDir_Right, 0.12F, nullptr, &main);
    ImGui::DockBuilderDockWindow("Toolbar", top);
    for (std::size_t i = 0; i < model.view_count(); ++i) {
        if (const LogView* view = model.view_at(i); view != nullptr) {
            ImGui::DockBuilderDockWindow(window_title(*view).c_str(), main);
        }
    }
    ImGui::DockBuilderDockWindow("Sources", left);
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
    model.step_filter_rebuilds(20'000U);
    model.step_finds(20'000U);
    if (model.importing()) {
        model.step_import(2'000U);
    }
    if (model.exporting()) {
        model.step_export(1'024U);
    }
}

void handle_copy_shortcut(LogViewModel& model, UiState& state) {
    if (!ImGui::GetIO().KeyCtrl || !ImGui::IsKeyPressed(ImGuiKey_C)) {
        return;
    }
    LogView* view = model.focused_view();
    if (view == nullptr) {
        return;
    }
    const auto seq = state.hovered_seq.has_value() ? state.hovered_seq : view->last_selected();
    if (!seq.has_value() || !view->history().find(*seq).has_value()) {
        return;
    }
    view->select_only(*seq);
    const std::string text = ui::copy_selected(model);
    if (!text.empty()) {
        ImGui::SetClipboardText(text.c_str());
        state.status = "record copied";
    }
}

void handle_bookmark_shortcut(LogViewModel& model, UiState& state) {
    LogView* view = model.focused_view();
    if (view == nullptr) {
        return;
    }
    if (ImGui::IsKeyPressed(ImGuiKey_F2)) {
        if (const auto seq = view->last_selected(); seq.has_value()) {
            static_cast<void>(view->toggle_bookmark(*seq));
        }
    }
    if (ImGui::IsKeyPressed(ImGuiKey_F3)) {
        const auto seq = ImGui::GetIO().KeyShift ? view->previous_bookmark(view->last_selected())
                                                 : view->next_bookmark(view->last_selected());
        if (seq.has_value()) {
            view->select_only(*seq);
            ui_for(state, *view).scroll_to = seq;
        }
    }
}

void handle_attention_shortcut(LogViewModel& model, UiState& state) {
    LogView* view = model.focused_view();
    if (view == nullptr || !ImGui::IsKeyPressed(ImGuiKey_F4)) {
        return;
    }
    const auto seq = ImGui::GetIO().KeyShift ? view->previous_attention(view->last_selected())
                                             : view->next_attention(view->last_selected());
    if (seq.has_value()) {
        view->select_only(*seq);
        ui_for(state, *view).scroll_to = seq;
    }
}

void handle_view_shortcuts(LogViewModel& model, UiState& state) {
    LogView* view = model.focused_view();
    if (view == nullptr) {
        return;
    }
    if (ImGui::IsKeyPressed(ImGuiKey_Space)) {
        view->set_paused(!view->paused());
    }
    if (ImGui::IsKeyPressed(ImGuiKey_T)) {
        if (const auto seq = view->last_selected(); seq.has_value()) {
            if (const auto record = view->history().find(*seq);
                record.has_value() && record->is_frame()) {
                view->time().set_anchor(static_cast<std::int64_t>(record->frame->timestamp_ns));
            }
        }
    }
    const ImGuiIO& io = ImGui::GetIO();
    if (io.KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_Home)) {
        ui::navigate_first(*view, ui_for(state, *view));
    }
    if (io.KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_End)) {
        ui::navigate_last(*view, ui_for(state, *view));
    }
    if (io.KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_A)) {
        view->select_all_visible();
    }
    if (io.KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_F)) {
        ui_for(state, *view).focus_find = true;
    }
}

void handle_shortcuts(LogViewModel& model, UiState& state) {
    if (!ui::shortcuts_allowed(state.log_view_focused, ImGui::GetIO().WantTextInput)) {
        return;
    }
    handle_view_shortcuts(model, state);
    handle_bookmark_shortcut(model, state);
    handle_attention_shortcut(model, state);
    handle_copy_shortcut(model, state);
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

void draw_splash(const SplashImage& image, UiState& state) {
    ImGuiViewport* viewport = ImGui::GetMainViewport();
    ImGui::SetNextWindowPos(viewport->Pos, ImGuiCond_Always);
    ImGui::SetNextWindowSize(viewport->Size, ImGuiCond_Always);
    constexpr ImGuiWindowFlags flags =
        ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoResize |
        ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoBringToFrontOnFocus |
        ImGuiWindowFlags_NoNav | ImGuiWindowFlags_NoInputs;
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.0F, 0.0F));
    ImGui::PushStyleColor(ImGuiCol_WindowBg, ImVec4(0.06F, 0.07F, 0.08F, 1.0F));
    if (ImGui::Begin("##splash", nullptr, flags)) {
        const ImVec2 available = ImGui::GetContentRegionAvail();
        const float scale = std::min(available.x / static_cast<float>(image.width()),
                                     available.y / static_cast<float>(image.height()));
        if (scale > 0.0F) {
            const ImVec2 size(static_cast<float>(image.width()) * scale,
                              static_cast<float>(image.height()) * scale);
            ImGui::SetCursorPos(
                ImVec2((available.x - size.x) * 0.5F, (available.y - size.y) * 0.5F));
            ImGui::Image(static_cast<ImTextureID>(image.texture()), size);
            state.splash_aspect_fit = true;
        }
    }
    ImGui::End();
    ImGui::PopStyleColor();
    ImGui::PopStyleVar();
}

nexigen::logging::LogFrame splash_test_frame(std::size_t index) {
    nexigen::logging::LogFrame frame{};
    frame.timestamp_ns = 2'000'000'000ULL + index * 1'000'000ULL;
    constexpr std::string_view app = "Splash";
    for (std::size_t i = 0; i < app.size() && i < sizeof(frame.app_id); ++i) {
        frame.app_id[i] = app[i];
    }
    frame.log_level = nexigen::core::LogLevel::Info;
    frame.thread_id = 7U;
    frame.pid = 7000U;
    const int written = std::snprintf(reinterpret_cast<char*>(frame.payload), sizeof(frame.payload),
                                      "splash record %zu", index);
    frame.payload_size = written > 0 ? static_cast<std::uint16_t>(written) : 0U;
    return frame;
}

bool run_splash(GLFWwindow* window, LogViewModel& model, UiState& state, CaptureThread* capture,
                const SplashImage& image, double seconds) {
    const auto start = std::chrono::steady_clock::now();
    const auto deadline = start + std::chrono::duration<double>(seconds);
    while (!glfwWindowShouldClose(window) && std::chrono::steady_clock::now() < deadline) {
        glfwPollEvents();
        if (glfwWindowShouldClose(window)) {
            break;
        }
        process_model_frame(model, state, capture);
        if (state.splash_ingest_test && state.splash_ingested < 50U) {
            model.ingest_frame(splash_test_frame(state.splash_ingested), 0U);
            ++state.splash_ingested;
        }
        ImGui_ImplOpenGL3_NewFrame();
        ImGui_ImplGlfw_NewFrame();
        if (state.splash_input_test) {
            ImGuiIO& io = ImGui::GetIO();
            if (state.splash_frames == 0U) {
                io.AddMousePosEvent(io.DisplaySize.x * 0.5F, io.DisplaySize.y * 0.5F);
                io.AddMouseButtonEvent(0, true);
                io.AddKeyEvent(ImGuiKey_Space, true);
            } else if (state.splash_frames == 1U) {
                io.AddMouseButtonEvent(0, false);
                io.AddKeyEvent(ImGuiKey_Space, false);
            }
        }
        ImGui::NewFrame();
        draw_splash(image, state);
        if (state.splash_input_test && (ImGui::IsAnyItemHovered() || ImGui::IsAnyItemActive())) {
            state.splash_input_ignored = false;
        }
        present_frame(window);
        ++state.splash_frames;
        state.splash_elapsed_seconds =
            std::chrono::duration<double>(std::chrono::steady_clock::now() - start).count();
    }
    return !glfwWindowShouldClose(window);
}

ImVec2 rect_center(ImVec2 min, ImVec2 max) {
    return ImVec2((min.x + max.x) * 0.5F, (min.y + max.y) * 0.5F);
}

void inject_ui_input(UiState& state) {
    if (!state.input_test && !state.view_test) {
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
    if (state.inject_right_down) {
        io.AddMouseButtonEvent(1, true);
        state.inject_right_down = false;
    }
    if (state.inject_right_up) {
        io.AddMouseButtonEvent(1, false);
        state.inject_right_up = false;
    }
    if (state.inject_wheel) {
        io.AddMouseWheelEvent(0.0F, state.inject_wheel_y);
        state.inject_wheel = false;
    }
}

bool drive_hover_phases(LogViewModel& model, UiState& state) {
    switch (state.input_phase) {
        case 0:
            if (model.display_count() > 0U && model.focused_view() != nullptr) {
                model.select_only(model.focused_view()->display_at(0));
                state.truncated = ui::selected_payload_truncated(model);
                state.capture_row_rects = true;
                state.input_phase = 1;
            }
            break;
        case 1:
            if (state.row_rect_count >= 3U) {
                state.inject_pos = rect_center(state.row_rect_min[0], state.row_rect_max[0]);
                state.inject_mouse_pos = true;
                state.input_phase = 2;
            }
            break;
        case 2:
            state.inject_pos = rect_center(state.row_rect_min[1], state.row_rect_max[1]);
            state.inject_mouse_pos = true;
            state.input_phase = 3;
            break;
        case 3:
            state.inject_pos = rect_center(state.row_rect_min[2], state.row_rect_max[2]);
            state.inject_mouse_pos = true;
            state.input_phase = 4;
            break;
        default: break;
    }
    return false;
}

bool drive_message_menu(UiState& state) {
    switch (state.input_phase) {
        case 4:
            if (state.id_conflict_detected) {
                std::printf("UI_INPUT conflict=1\n");
                return true;
            }
            state.menu_target_seq = state.row_seqs[0];
            state.menu_open_for_tests = true;
            state.input_phase = 5;
            break;
        case 5:
            state.capture_menu_rects = true;
            state.input_phase = 6;
            break;
        case 6:
            if (state.menu_message_rect_valid) {
                state.inject_pos =
                    rect_center(state.menu_message_rect_min, state.menu_message_rect_max);
                state.inject_mouse_pos = true;
                state.input_phase = 7;
            }
            break;
        case 7:
            state.inject_mouse_down = true;
            state.input_phase = 8;
            break;
        case 8:
            state.inject_mouse_up = true;
            state.input_phase = 9;
            break;
        case 9:
            if (state.menu_message_copied) {
                state.menu_message_rect_valid = false;
                state.menu_record_rect_valid = false;
                state.capture_menu_rects = false;
                state.menu_target_seq = state.row_seqs[0];
                state.menu_open_for_tests = true;
                state.input_phase = 10;
            }
            break;
        default: break;
    }
    return false;
}

bool drive_record_menu(UiState& state) {
    switch (state.input_phase) {
        case 10:
            state.capture_menu_rects = true;
            state.input_phase = 11;
            break;
        case 11:
            if (state.menu_record_rect_valid) {
                state.inject_pos =
                    rect_center(state.menu_record_rect_min, state.menu_record_rect_max);
                state.inject_mouse_pos = true;
                state.input_phase = 12;
            }
            break;
        case 12:
            state.inject_mouse_down = true;
            state.input_phase = 13;
            break;
        case 13:
            state.inject_mouse_up = true;
            state.input_phase = 14;
            break;
        case 14:
            std::printf(
                "UI_INPUT truncated=%d conflict=%d menu_message=%d menu_record=%d layout=%d\n",
                state.truncated, state.id_conflict_detected, state.menu_message_copied,
                state.menu_record_copied, state.layout_loaded);
            return true;
        default: return true;
    }
    return false;
}

bool drive_input_script(LogViewModel& model, UiState& state) {
    if (!state.input_test) {
        return false;
    }
    if (state.input_phase > 0 && ++state.input_phase_frames > 900) {
        std::printf("UI_INPUT stalled phase=%d conflict=%d rows=%zu menu_msg=%d menu_rec=%d\n",
                    state.input_phase, state.id_conflict_detected, state.row_rect_count,
                    state.menu_message_copied, state.menu_record_copied);
        return true;
    }
    if (state.input_phase <= 3) {
        return drive_hover_phases(model, state);
    }
    if (state.input_phase <= 9) {
        return drive_message_menu(state);
    }
    return drive_record_menu(state);
}

void drain_view_rebuilds(LogViewModel& model) {
    for (;;) {
        model.step_filter_rebuilds(1'000'000U);
        bool rebuilding = false;
        for (std::size_t i = 0; i < model.view_count(); ++i) {
            const LogView* view = model.view_at(i);
            rebuilding = rebuilding || (view != nullptr && view->rebuilding());
        }
        if (!rebuilding) {
            return;
        }
    }
}

/// @brief Aggregate of the scripted view-test observations.
struct ViewTestChecks {
    bool live_clone{false};
    bool paused_clone{false};
    bool live_tail{false};
    bool nav_default_on{false};
    bool nav_pause_preserved{false};
    bool levels_isolated{true};
    bool focus_ok{false};
    bool limit_rejected{false};
};

/// @brief Ensure the model has an All view and a free live slot.
///
/// @param model View model to prepare.
/// @return The view count observed before preparing.
std::size_t restore_view_test_layout(LogViewModel& model) {
    const std::size_t restored = model.view_count();
    if (model.view_count() == 0U) {
        static_cast<void>(model.create_view(ViewKind::All, {}));
    }
    if (model.live_view_count() >= kMaxLiveViews) {
        for (std::size_t i = model.view_count(); i > 0U; --i) {
            const LogView* candidate = model.view_at(i - 1U);
            if (candidate != nullptr && candidate->live()) {
                static_cast<void>(model.close_view(candidate->id()));
                break;
            }
        }
    }
    return restored;
}

/// @brief Pick the source name used by the scripted view test.
///
/// @param model View model supplying the application registry.
/// @return The first used application, or `Engine` when none exists.
std::string view_test_source_name(LogViewModel& model) {
    std::string source_name;
    for (std::size_t slot = 0; slot < AppRegistry::capacity; ++slot) {
        const AppSlot& app = model.ring().apps().at(slot);
        if (app.used) {
            source_name = std::string(app.text());
            break;
        }
    }
    if (source_name.empty()) {
        source_name = "Engine";
    }
    return source_name;
}

/// @brief Create the scripted test's source view.
///
/// @param model View model receiving the view.
/// @param source_name Source application name.
/// @return The created view id, or the focused id when creation failed.
ViewId create_view_test_source(LogViewModel& model, const std::string& source_name) {
    ViewId source_id = model.focused_id();
    if (const auto created = model.create_view(ViewKind::Source, source_name); created) {
        source_id = *created;
    }
    return source_id;
}

/// @brief Find the model's All view.
///
/// @param model View model to scan.
/// @return The All view, or `nullptr` when none is open.
LogView* find_all_view(LogViewModel& model) {
    for (std::size_t i = 0; i < model.view_count(); ++i) {
        LogView* candidate = model.view_at(i);
        if (candidate != nullptr && candidate->kind() == ViewKind::All) {
            return candidate;
        }
    }
    return nullptr;
}

/// @brief Free one live slot so the clone checks can create a clone.
///
/// @param model View model to adjust.
/// @param source_id Source view that must stay open.
void free_live_slot_for_clone(LogViewModel& model, ViewId source_id) {
    if (model.live_view_count() < kMaxLiveViews) {
        return;
    }
    for (std::size_t i = model.view_count(); i > 0U; --i) {
        const LogView* candidate = model.view_at(i - 1U);
        if (candidate != nullptr && candidate->live() && candidate->id() != source_id) {
            static_cast<void>(model.close_view(candidate->id()));
            break;
        }
    }
}

/// @brief Run the clone, isolation, focus and live-limit observations.
///
/// @param model View model under test.
/// @param state UI state observed through the per-view UI.
/// @param source_id Source view id.
/// @param source Source view pointer.
/// @param all_view All view used for the level-isolation check.
/// @param checks Receives the observations.
void run_view_test_clones(LogViewModel& model, UiState& state, ViewId source_id, LogView* source,
                          LogView* all_view, ViewTestChecks& checks) {
    free_live_slot_for_clone(model, source_id);
    checks.live_clone = model.clone_view(source_id, CloneMode::Live, CloneOptions{}).has_value();
    if (checks.live_clone) {
        LogView* clone = model.view(model.focused_id());
        checks.live_tail = clone != nullptr && clone->live() && !clone->paused() &&
                           ui_for(state, *clone).auto_scroll;
    }
    checks.paused_clone =
        model.clone_view(source_id, CloneMode::Paused, CloneOptions{}).has_value();
    if (all_view != nullptr) {
        const auto before = all_view->filter().criteria().levels;
        const auto error_level = static_cast<std::size_t>(nexigen::core::LogLevel::Error);
        source->filter().criteria().levels[error_level] =
            !source->filter().criteria().levels[error_level];
        checks.levels_isolated = all_view->filter().criteria().levels == before;
    }
    model.focus_view(source_id);
    checks.focus_ok = model.focused_id() == source_id && model.focused_view() == source;
    int extra = 0;
    while (model.live_view_count() < kMaxLiveViews) {
        const auto created = model.create_view(ViewKind::Source, std::format("UiLimit{}", extra++));
        if (!created) {
            break;
        }
    }
    checks.limit_rejected = !model.create_view(ViewKind::Source, "UiOverflow").has_value();
    model.focus_view(source_id);
}

/// @brief Run the auto-scroll default and pause-preservation observations.
///
/// @param state UI state holding the per-view UI.
/// @param source Source view under test.
/// @param checks Receives the observations.
void run_view_test_navigation(UiState& state, LogView& source, ViewTestChecks& checks) {
    ViewUiState& nav_ui = ui_for(state, source);
    checks.nav_default_on = nav_ui.auto_scroll;
    nav_ui.auto_scroll = false;
    source.set_paused(true);
    source.set_paused(false);
    checks.nav_pause_preserved = !nav_ui.auto_scroll;
    nav_ui.auto_scroll = true;
}

/// @brief Prepare the selection, pause and focus state used by the scripted run.
///
/// @param state UI state receiving the expected selection and focus id.
/// @param source Source view to pause and select in.
/// @param source_id Source view id.
void prepare_view_test_state(UiState& state, LogView& source, ViewId source_id) {
    source.select_only(source.display_at(0));
    state.expected_inspected_seq = source.last_selected();
    source.set_paused(true);
    state.view_test_focus_id = source_id;
}

/// @brief Print the view-test observation line and compute the verdict.
///
/// @param model View model supplying the view counts.
/// @param restored View count observed before preparation.
/// @param source_ok Whether the source view exists.
/// @param checks Collected observations.
/// @return `true` when every observation matched.
bool report_view_test(const LogViewModel& model, std::size_t restored, bool source_ok,
                      const ViewTestChecks& checks) {
    std::printf("UI_VIEWS restored=%zu live=%zu paused=%zu source=%d live_clone=%d "
                "paused_clone=%d live_tail=%d nav_default=%d nav_pause=%d levels_isolated=%d "
                "focus=%d limit_rejected=%d\n",
                restored, model.live_view_count(), model.paused_view_count(), source_ok,
                checks.live_clone, checks.paused_clone, checks.live_tail, checks.nav_default_on,
                checks.nav_pause_preserved, checks.levels_isolated, checks.focus_ok,
                checks.limit_rejected);
    return source_ok && checks.live_clone && checks.paused_clone && checks.live_tail &&
           checks.nav_default_on && checks.nav_pause_preserved && checks.levels_isolated &&
           checks.focus_ok && checks.limit_rejected;
}

bool run_ui_view_test(LogViewModel& model, UiState& state) {
    const std::size_t restored = restore_view_test_layout(model);
    const std::string source_name = view_test_source_name(model);
    const ViewId source_id = create_view_test_source(model, source_name);
    LogView* source = model.view(source_id);
    LogView* all_view = find_all_view(model);
    ViewTestChecks checks{};
    if (source != nullptr) {
        run_view_test_clones(model, state, source_id, source, all_view, checks);
    }
    drain_view_rebuilds(model);
    if (source != nullptr) {
        run_view_test_navigation(state, *source, checks);
    }
    if (source != nullptr && source->display_count() > 0U) {
        prepare_view_test_state(state, *source, source_id);
    }
    state.clone_open = true;
    state.clone_target = source_id;
    state.float_view = source_id;
    state.clone_mode = CloneMode::Paused;
    state.auto_open_sources_popup = true;
    state.force_eco = true;
    return report_view_test(model, restored, source != nullptr, checks);
}

// Drive the `--ui-view-test` navigation phases: click the auto-scroll toggle off
// and on, then wheel the table up and confirm auto-scroll turns off again.
/// @brief Close any popup that could cover the navigation controls.
void close_covering_popup() {
    if (ImGui::GetCurrentContext()->OpenPopupStack.Size > 0) {
        ImGui::ClosePopupToLevel(0, false);
    }
}

/// @brief Inject a mouse press onto the third navigation control.
///
/// @param state UI state receiving the injected press.
void press_navigation_control(UiState& state) {
    state.inject_pos = rect_center(state.nav_rect_min[2], state.nav_rect_max[2]);
    state.inject_mouse_pos = true;
    state.inject_mouse_down = true;
}

/// @brief Advance predicate that never waits for an observation.
///
/// @return Always `true`.
[[nodiscard]] bool always_advance(const UiState&) noexcept {
    return true;
}

/// @brief One phase of the scripted view test.
struct ViewTestStep {
    /// @brief Inject the phase's input.
    void (*drive)(UiState&, ViewUiState*) noexcept;
    /// @brief Whether the phase may advance to the next step.
    bool (*advance)(const UiState&) noexcept;
};

/// @brief The scripted `--ui-view-test` phases, indexed by `view_test_phase`.
constexpr ViewTestStep kViewTestSteps[] = {
    {[](UiState&, ViewUiState*) noexcept { close_covering_popup(); },
     [](const UiState& state) noexcept { return state.nav_rects_valid; }},
    {[](UiState&, ViewUiState*) noexcept { close_covering_popup(); }, always_advance},
    {[](UiState& state, ViewUiState*) noexcept { press_navigation_control(state); },
     always_advance},
    {[](UiState& state, ViewUiState*) noexcept { state.inject_mouse_up = true; }, always_advance},
    {[](UiState& state, ViewUiState* nav) noexcept {
         state.view_test_toggle_off = nav != nullptr && !nav->auto_scroll;
         press_navigation_control(state);
     },
     always_advance},
    {[](UiState& state, ViewUiState*) noexcept { state.inject_mouse_up = true; }, always_advance},
    {[](UiState& state, ViewUiState* nav) noexcept {
         state.view_test_toggle_on = nav != nullptr && nav->auto_scroll;
         state.row_rect_count = 0;
         state.capture_row_rects = true;
     },
     always_advance},
    {[](UiState& state, ViewUiState*) noexcept {
         if (state.row_rect_count > 0) {
             state.inject_pos = rect_center(state.row_rect_min[0], state.row_rect_max[0]);
             state.inject_mouse_pos = true;
         }
     },
     [](const UiState& state) noexcept { return state.row_rect_count > 0; }},
    {[](UiState& state, ViewUiState*) noexcept {
         // Inject the wheel on its own frame: ImGui trickles a wheel event queued
         // right after a mouse-move event to the following frame.
         state.inject_wheel = true;
         state.inject_wheel_y = 1.0F;
     },
     always_advance},
    {[](UiState& state, ViewUiState* nav) noexcept {
         state.view_test_wheel_off = nav != nullptr && !nav->auto_scroll;
         state.capture_row_rects = false;
     },
     always_advance},
};

void drive_view_test(LogViewModel& model, UiState& state) {
    if (!state.view_test) {
        return;
    }
    LogView* view = model.view(state.view_test_focus_id);
    ViewUiState* nav = view != nullptr ? &ui_for(state, *view) : nullptr;
    constexpr std::size_t phases = sizeof(kViewTestSteps) / sizeof(kViewTestSteps[0]);
    if (static_cast<std::size_t>(state.view_test_phase) >= phases) {
        return;
    }
    const ViewTestStep& step = kViewTestSteps[state.view_test_phase];
    step.drive(state, nav);
    if (step.advance(state)) {
        ++state.view_test_phase;
    }
}

bool setup_focus_test(LogViewModel& model, UiState& state) {
    if (model.view_count() == 0U) {
        static_cast<void>(model.create_view(ViewKind::All, {}));
    }
    LogView* view = model.view_at(0);
    if (view == nullptr || view->display_count() == 0U) {
        return false;
    }
    view->select_only(view->display_at(0));
    state.focus_test_active = true;
    state.focus_phase = 0;
    state.focus_view_id = view->id();
    state.focus_before = view->bookmarked_count();
    return true;
}

/// @brief Prepare the UI state for one run.
///
/// @param state UI state to initialize.
/// @param model View model supplying the open views.
/// @param options Parsed command line.
void initialize_ui_state(UiState& state, LogViewModel& model, const UiOptions& options) {
    state.live = options.live;
    state.input_test = options.input_test;
    state.view_test = options.view_test;
    state.splash_input_test =
        options.splash_test && std::getenv("NEXIGEN_LOGVIEW_SPLASH_TEST_INPUT") != nullptr;
    state.splash_ingest_test =
        options.splash_test && std::getenv("NEXIGEN_LOGVIEW_SPLASH_TEST_INGEST") != nullptr;
    state.ini_path = parent_directory(default_config_path()) + "/logview-layout.ini";
    {
        std::error_code error;
        state.layout_loaded = std::filesystem::exists(state.ini_path, error);
        std::filesystem::create_directories(parent_directory(state.ini_path), error);
    }
    for (std::size_t i = 0; i < model.view_count(); ++i) {
        if (const LogView* view = model.view_at(i); view != nullptr) {
            static_cast<void>(ui_for(state, *view));
        }
    }
    std::snprintf(state.save_path, sizeof(state.save_path), "/tmp/nexigen-logview-export.jsonl");
    std::snprintf(state.open_path, sizeof(state.open_path), "%s", options.open_path.c_str());
}

/// @brief Run the view and focus scripted tests before opening the window.
///
/// @param model View model under test.
/// @param state UI state used by the scripts.
/// @param options Parsed command line.
/// @return `false` when a scripted test failed.
bool run_pre_gui_tests(LogViewModel& model, UiState& state, const UiOptions& options) {
    if (options.view_test && !run_ui_view_test(model, state)) {
        std::fprintf(stderr, "UI_VIEWS failed\n");
        return false;
    }
    if (options.focus_test && !setup_focus_test(model, state)) {
        std::fprintf(stderr, "UI_FOCUS failed: no visible record\n");
        return false;
    }
    return true;
}

/// @brief Start the capture thread when the run is live.
///
/// @param model View model supplying the service configuration.
/// @param state UI state receiving a capture failure status.
/// @param options Parsed command line.
/// @return The capture thread, or `nullptr` when not live or unavailable.
std::unique_ptr<CaptureThread> start_capture(LogViewModel& model, UiState& state,
                                             const UiOptions& options) {
    if (!options.live) {
        return nullptr;
    }
    auto capture =
        std::make_unique<CaptureThread>(model.config().service_name, model.config().capture_thread);
    const auto started = capture->start();
    if (!started) {
        state.status =
            "capture unavailable: " + std::string(nexigen::core::to_string(started.error()));
        return nullptr;
    }
    return capture;
}

/// @brief Whether the run starts without showing the splash screen.
///
/// @param options Parsed command line.
/// @return `true` for smoke and scripted runs.
bool splash_skipped(const UiOptions& options) {
    return options.smoke || options.actions_test || options.input_test || options.view_test ||
           options.focus_test;
}

/// @brief Result of loading the splash asset.
struct SplashLoad {
    std::optional<SplashImage> image{}; ///< Loaded image, when available.
    bool exit{false};                   ///< Whether the process must exit now.
    int exit_code{0};                   ///< Exit code for the early exit.
};

/// @brief Load the splash image when the run asks for one.
///
/// @param options Parsed command line.
/// @param window GUI window to shut down on an early exit.
/// @param capture Capture thread to stop on an early exit; may be `nullptr`.
/// @param skip_splash Whether this run skips the splash.
/// @return The loaded image or an early-exit request.
SplashLoad load_splash(const UiOptions& options, GLFWwindow* window, CaptureThread* capture,
                       bool skip_splash) {
    SplashLoad result{};
    if (!options.splash_test && skip_splash) {
        return result;
    }
    auto loaded = SplashImage::load_embedded();
    if (options.splash_test && std::getenv("NEXIGEN_LOGVIEW_SPLASH_TEST_FAIL") != nullptr) {
        loaded = std::unexpected(nexigen::core::ErrorCode::CorruptData);
    }
    if (loaded) {
        result.image.emplace(std::move(*loaded));
        return result;
    }
    std::fprintf(stderr, "nexigen-logview: splash unavailable: %s\n",
                 std::string(nexigen::core::to_string(loaded.error())).c_str());
    if (!options.splash_test) {
        return result;
    }
    std::printf("UI_SPLASH fallback=1\n");
    if (capture != nullptr) {
        capture->stop();
    }
    shutdown_gui(window);
    result.exit = true;
    return result;
}

/// @brief Show the splash screen (or run its test) before the workspace.
///
/// @param model View model supplying the records shown during the splash.
/// @param state UI state carrying the splash counters.
/// @param options Parsed command line.
/// @param window GUI window.
/// @param capture Capture thread; may be `nullptr`.
/// @param skip_splash Whether this run skips the splash.
/// @param splash Loaded splash image; released after the phase.
/// @param exit_code Receives the early exit code.
/// @return `false` when the process must exit, with `exit_code` set.
bool run_splash_phase(LogViewModel& model, UiState& state, const UiOptions& options,
                      GLFWwindow* window, CaptureThread* capture, bool skip_splash,
                      std::optional<SplashImage>& splash, int& exit_code) {
    if (options.splash_test) {
        if (!splash.has_value()) {
            if (capture != nullptr) {
                capture->stop();
            }
            shutdown_gui(window);
            exit_code = 1;
            return false;
        }
        state.splash_expected_views = model.view_count();
        const bool completed = run_splash(window, model, state, capture, *splash, 1.0);
        std::printf("UI_SPLASH width=%d height=%d texture=%d frames=%zu elapsed_ms=%.1f "
                    "aspect_fit=%d records=%zu ingested=%zu input_ignored=%d\n",
                    splash->width(), splash->height(), splash->valid() ? 1 : 0, state.splash_frames,
                    state.splash_elapsed_seconds * 1000.0, state.splash_aspect_fit,
                    model.display_count(), state.splash_ingested,
                    state.splash_input_ignored ? 1 : 0);
        splash.reset();
        if (completed) {
            // Fall through to a short workspace session so the test can confirm the
            // workspace appears and the records ingested during the splash are present.
            return true;
        }
        if (capture != nullptr) {
            capture->stop();
        }
        shutdown_gui(window);
        exit_code = 0;
        return false;
    }
    if (skip_splash || !splash.has_value()) {
        return true;
    }
    const bool completed = run_splash(window, model, state, capture, *splash, 1.0);
    splash.reset();
    if (completed) {
        return true;
    }
    if (capture != nullptr) {
        capture->stop();
    }
    shutdown_gui(window);
    exit_code = 0;
    return false;
}

/// @brief Run the scripted `--actions-test` round and shut the GUI down.
///
/// @param model View model under test.
/// @param options Parsed command line.
/// @param window GUI window.
/// @param capture Capture thread; may be `nullptr`.
/// @return The actions-test verdict.
int run_actions_test(LogViewModel& model, const UiOptions& options, GLFWwindow* window,
                     CaptureThread* capture) {
    const int actions_result = run_ui_actions_test(model, options);
    if (capture != nullptr) {
        capture->stop();
    }
    shutdown_gui(window);
    return actions_result;
}

/// @brief Inject the focus script's key events for the current phase.
///
/// @param model View model supplying the focused view.
/// @param state UI state carrying the focus-script phase.
/// @brief Focus-script input for phase 0: press F2.
void focus_input_press_f2(LogViewModel&, UiState&) {
    ImGui::GetIO().AddKeyEvent(ImGuiKey_F2, true);
}

/// @brief Focus-script input for phase 2: release F2.
void focus_input_release_f2(LogViewModel&, UiState&) {
    ImGui::GetIO().AddKeyEvent(ImGuiKey_F2, false);
}

/// @brief Focus-script input for phase 4: bookmark with Ctrl+F2 and scroll to the end.
void focus_input_bookmark_end(LogViewModel&, UiState&) {
    ImGuiIO& io = ImGui::GetIO();
    io.AddKeyEvent(ImGuiKey_F2, true);
    io.AddKeyEvent(ImGuiMod_Ctrl, true);
    io.AddKeyEvent(ImGuiKey_End, true);
}

/// @brief Focus-script input for phase 5: release the bookmark chord.
void focus_input_release_bookmark(LogViewModel&, UiState&) {
    ImGuiIO& io = ImGui::GetIO();
    io.AddKeyEvent(ImGuiKey_F2, false);
    io.AddKeyEvent(ImGuiKey_End, false);
    io.AddKeyEvent(ImGuiMod_Ctrl, false);
}

/// @brief Focus-script input for phase 7: press Ctrl+End.
void focus_input_press_ctrl_end(LogViewModel&, UiState&) {
    ImGuiIO& io = ImGui::GetIO();
    io.AddKeyEvent(ImGuiMod_Ctrl, true);
    io.AddKeyEvent(ImGuiKey_End, true);
}

/// @brief Focus-script input for phase 8: release Ctrl+End and record navigation.
///
/// @param model View model supplying the focused view.
/// @param state UI state receiving the observation.
void focus_input_check_end(LogViewModel& model, UiState& state) {
    LogView* view = model.view(state.focus_view_id);
    ViewUiState* nav = view != nullptr ? &ui_for(state, *view) : nullptr;
    state.focus_nav_last_ok = view != nullptr && nav != nullptr && state.log_view_focused &&
                              nav->scroll_to == ui::last_visible_sequence(*view) &&
                              !nav->auto_scroll;
    ImGuiIO& io = ImGui::GetIO();
    io.AddKeyEvent(ImGuiKey_End, false);
    io.AddKeyEvent(ImGuiMod_Ctrl, false);
}

/// @brief Focus-script input for phase 9: press Ctrl+Home.
void focus_input_press_ctrl_home(LogViewModel&, UiState&) {
    ImGuiIO& io = ImGui::GetIO();
    io.AddKeyEvent(ImGuiMod_Ctrl, true);
    io.AddKeyEvent(ImGuiKey_Home, true);
}

/// @brief Focus-script input for phase 10: release Ctrl+Home and record navigation.
///
/// @param model View model supplying the focused view.
/// @param state UI state receiving the observation.
void focus_input_check_home(LogViewModel& model, UiState& state) {
    LogView* view = model.view(state.focus_view_id);
    ViewUiState* nav = view != nullptr ? &ui_for(state, *view) : nullptr;
    state.focus_nav_first_ok = view != nullptr && nav != nullptr && state.log_view_focused &&
                               nav->scroll_to == ui::first_visible_sequence(*view) &&
                               !nav->auto_scroll;
    ImGuiIO& io = ImGui::GetIO();
    io.AddKeyEvent(ImGuiKey_Home, false);
    io.AddKeyEvent(ImGuiMod_Ctrl, false);
}

/// @brief Focus-script input per phase; `nullptr` means "no input this frame".
using FocusInputFn = void (*)(LogViewModel&, UiState&);

/// @brief Indexed by `focus_phase`.
constexpr FocusInputFn kFocusInput[] = {
    focus_input_press_f2,
    focus_input_release_f2,
    nullptr,
    nullptr,
    focus_input_bookmark_end,
    focus_input_release_bookmark,
    nullptr,
    focus_input_press_ctrl_end,
    focus_input_check_end,
    focus_input_press_ctrl_home,
    focus_input_check_home,
};

/// @brief Inject the focus script's key events for the current phase.
///
/// @param model View model supplying the focused view.
/// @param state UI state carrying the focus-script phase.
void update_focus_script_input(LogViewModel& model, UiState& state) {
    constexpr std::size_t phases = sizeof(kFocusInput) / sizeof(kFocusInput[0]);
    if (static_cast<std::size_t>(state.focus_phase) >= phases) {
        return;
    }
    const FocusInputFn input = kFocusInput[state.focus_phase];
    if (input != nullptr) {
        input(model, state);
    }
}

/// @brief Focus-script state for phase 0: request focus on the view.
void focus_phase_request(LogViewModel&, UiState& state, LogView*, ViewUiState*) {
    state.focus_next_view = state.focus_view_id;
    state.focus_phase = 1;
}

/// @brief Focus-script state for phase 1: verify the bookmark and request focus.
void focus_phase_bookmark(LogViewModel&, UiState& state, LogView* view, ViewUiState*) {
    state.focus_ok =
        view != nullptr && state.log_view_focused && view->bookmarked_count() > state.focus_before;
    state.focus_sources_next = true;
    state.focus_phase = 2;
}

/// @brief Focus-script state for phase 2: advance.
void focus_phase_advance(LogViewModel&, UiState& state, LogView*, ViewUiState*) {
    state.focus_phase = 3;
}

/// @brief Focus-script state for phase 3: verify the unfocused bookmark.
void focus_phase_unfocused(LogViewModel&, UiState& state, LogView* view, ViewUiState* nav_ui) {
    state.focus_unfocused_ok = view != nullptr && !state.log_view_focused &&
                               view->bookmarked_count() == state.focus_before + 1U;
    if (nav_ui != nullptr) {
        nav_ui->scroll_to.reset();
    }
    state.focus_phase = 4;
}

/// @brief Focus-script state for phase 4: verify scroll state while unfocused.
void focus_phase_nav_unfocused(LogViewModel&, UiState& state, LogView* view, ViewUiState* nav_ui) {
    state.focus_nav_unfocused_ok = view != nullptr && nav_ui != nullptr &&
                                   !state.log_view_focused && !nav_ui->scroll_to.has_value() &&
                                   nav_ui->auto_scroll;
    state.focus_phase = 5;
}

/// @brief Focus-script state for phase 5: switch auto-scroll off and refocus.
void focus_phase_auto_scroll_off(LogViewModel&, UiState& state, LogView*, ViewUiState* nav_ui) {
    if (nav_ui != nullptr) {
        nav_ui->auto_scroll = false;
        nav_ui->scroll_to.reset();
    }
    state.focus_next_view = state.focus_view_id;
    state.focus_phase = 6;
}

/// @brief Focus-script state for a pass-through phase.
void focus_phase_passthrough(LogViewModel&, UiState& state, LogView*, ViewUiState*) {
    ++state.focus_phase;
}

/// @brief Focus-script state handler.
using FocusStepFn = void (*)(LogViewModel&, UiState&, LogView*, ViewUiState*);

/// @brief Indexed by `focus_phase`; phases 6 to 10 advance one step each.
constexpr FocusStepFn kFocusSteps[] = {
    focus_phase_request,     focus_phase_bookmark,      focus_phase_advance,
    focus_phase_unfocused,   focus_phase_nav_unfocused, focus_phase_auto_scroll_off,
    focus_phase_passthrough, focus_phase_passthrough,   focus_phase_passthrough,
    focus_phase_passthrough, focus_phase_passthrough,
};

/// @brief Advance the focus script's state machine for the current frame.
///
/// @param model View model supplying the focused view.
/// @param state UI state carrying the focus-script phase and observations.
void update_focus_test_state(LogViewModel& model, UiState& state) {
    LogView* view = model.view(state.focus_view_id);
    ViewUiState* nav_ui = view != nullptr ? &ui_for(state, *view) : nullptr;
    constexpr std::size_t phases = sizeof(kFocusSteps) / sizeof(kFocusSteps[0]);
    if (static_cast<std::size_t>(state.focus_phase) >= phases) {
        return;
    }
    kFocusSteps[state.focus_phase](model, state, view, nav_ui);
}

/// @brief Print the view-test observation line.
///
/// @param state UI state holding the observations.
void print_view_test_checks(const UiState& state) {
    const bool inspector_ok = state.expected_inspected_seq.has_value() &&
                              state.inspected_seq == state.expected_inspected_seq;
    std::printf("UI_VIEWS_CHECKS pause=%d counters=%d eco=%d docked=%zu floating=%d "
                "inspector=%d inspector_meta=%d nav=%d nav_hit_areas=%zu auto_active=%d "
                "toggle_off=%d toggle_on=%d wheel_off=%d\n",
                state.seen_pause_badge, state.seen_view_counters, state.seen_eco_badge,
                state.docked_views, state.seen_floating_view, inspector_ok,
                state.seen_inspector_meta, state.seen_navigation_controls,
                state.navigation_hit_areas, state.seen_auto_scroll_active,
                state.view_test_toggle_off, state.view_test_toggle_on, state.view_test_wheel_off);
}

/// @brief Print the splash-workspace observation line.
///
/// @param model View model supplying the final counts.
/// @param state UI state holding the splash counters.
void print_splash_workspace(const LogViewModel& model, const UiState& state) {
    const bool stable = state.splash_expected_views == model.view_count();
    std::printf("UI_SPLASH_WORKSPACE frames=%d records=%zu views=%zu stable=%d\n",
                state.splash_workspace_frames, model.display_count(), model.view_count(),
                stable ? 1 : 0);
}

/// @brief Whether the focus script passed.
///
/// @param options Parsed command line.
/// @param state UI state holding the focus observations.
/// @return `true` when the focus script is not requested or passed.
bool focus_result_ok(const UiOptions& options, const UiState& state) {
    if (!options.focus_test) {
        return true;
    }
    return state.focus_ok && state.focus_unfocused_ok && state.focus_nav_unfocused_ok &&
           state.focus_nav_last_ok && state.focus_nav_first_ok;
}

/// @brief Whether the view script passed.
///
/// @param options Parsed command line.
/// @param state UI state holding the view observations.
/// @return `true` when the view script is not requested or passed.
bool view_result_ok(const UiOptions& options, const UiState& state) {
    if (!options.view_test) {
        return true;
    }
    return state.view_test_toggle_off && state.view_test_toggle_on && state.view_test_wheel_off;
}

/// @brief Print the focus observation line when the focus script ran.
///
/// @param options Parsed command line.
/// @param state UI state holding the focus observations.
void print_focus_result(const UiOptions& options, const UiState& state) {
    if (!options.focus_test) {
        return;
    }
    std::printf("UI_FOCUS focused=%d unfocused=%d nav_unfocused=%d nav_last=%d nav_first=%d\n",
                state.focus_ok, state.focus_unfocused_ok, state.focus_nav_unfocused_ok,
                state.focus_nav_last_ok, state.focus_nav_first_ok);
}

/// @brief Print the scripted-run observations, shut down and compute the exit code.
///
/// @param model View model supplying the final counts.
/// @param state UI state holding the observations.
/// @param options Parsed command line.
/// @param window GUI window to shut down.
/// @param capture Capture thread to stop; may be `nullptr`.
/// @return Process exit code.
int report_ui_result(LogViewModel& model, UiState& state, const UiOptions& options,
                     GLFWwindow* window, CaptureThread* capture) {
    if (options.view_test) {
        print_view_test_checks(state);
    }
    if (options.splash_test) {
        print_splash_workspace(model, state);
    }
    const bool focus_ok = focus_result_ok(options, state);
    const bool view_ok = view_result_ok(options, state);
    print_focus_result(options, state);
    if (capture != nullptr) {
        capture->stop();
    }
    shutdown_gui(window);
    return (focus_ok && view_ok) ? 0 : 1;
}

/// @brief Draw one workspace frame: dockspace, windows and per-view tables.
///
/// @param model View model supplying the views.
/// @param state UI state carrying the layout and script flags.
/// @param options Parsed command line.
/// @param layout_done Whether the default layout was already applied.
void draw_workspace_frame(LogViewModel& model, UiState& state, const UiOptions& options,
                          bool& layout_done) {
    const ImGuiID dockspace = ImGui::DockSpaceOverViewport();
    if (!layout_done) {
        layout_done = true;
        std::error_code layout_error;
        if (options.smoke || options.view_test ||
            !std::filesystem::exists(state.ini_path, layout_error)) {
            draw_default_layout(dockspace, model);
        }
    }
    draw_global_toolbar(model, state);
    draw_sources(model, state);
    draw_workspace(model, state);
    std::vector<ViewId> close_views;
    for (std::size_t i = 0; i < model.view_count(); ++i) {
        LogView* view = model.view_at(i);
        if (view == nullptr) {
            break;
        }
        if (draw_log_view(model, *view, state)) {
            close_views.push_back(view->id());
        }
    }
    for (const ViewId id : close_views) {
        static_cast<void>(model.close_view(id));
        state.view_ui.erase(id);
        state.pending_dock.erase(id);
        state.log_view_focused = false;
    }
    if (options.view_test && state.view_test_focus_id != 0) {
        model.focus_view(state.view_test_focus_id);
    }
    if (options.view_test) {
        drive_view_test(model, state);
    }
}

/// @brief Advance the focus script's state machine for the current frame.
///
/// @param model View model supplying the focused view.
/// @param state UI state carrying the focus-script phase and observations.
// update_focus_test_state and its tables are defined above.

/// @brief Whether the current frame finishes a scripted run.
///
/// @param options Parsed command line.
/// @param state UI state carrying the scripted-run counters.
/// @return `true` when the smoke/view/focus/splash run is complete.
bool frame_test_done(const UiOptions& options, UiState& state) {
    if (options.smoke) {
        return ++state.smoke_frames >= 2;
    }
    if (options.view_test) {
        return state.view_test_phase >= 10;
    }
    if (options.focus_test) {
        return ++state.smoke_frames >= 12;
    }
    if (options.splash_test) {
        return state.splash_workspace_frames >= 6;
    }
    return false;
}

/// @brief Pump window events at the rate the resource guard allows.
///
/// @param model View model supplying the guard mode.
void pump_window_events(LogViewModel& model) {
    if (model.guard().mode() == ResourceMode::Eco) {
        glfwWaitEventsTimeout(1.0 / 15.0);
    } else {
        glfwPollEvents();
    }
}

/// @brief Run the interactive frame loop until the window closes or a test ends.
///
/// @param model View model under test.
/// @param state UI state mutated across frames.
/// @param options Parsed command line.
/// @param window GUI window.
/// @param capture Capture thread; may be `nullptr`.
void run_frame_loop(LogViewModel& model, UiState& state, const UiOptions& options,
                    GLFWwindow* window, CaptureThread* capture) {
    bool layout_done = false;
    while (glfwWindowShouldClose(window) == GLFW_FALSE) {
        pump_window_events(model);
        if (glfwWindowShouldClose(window) != GLFW_FALSE) {
            break;
        }
        process_model_frame(model, state, capture);
        ImGui_ImplOpenGL3_NewFrame();
        ImGui_ImplGlfw_NewFrame();
        inject_ui_input(state);
        if (options.focus_test) {
            update_focus_script_input(model, state);
        }
        ImGui::NewFrame();
        state.log_view_focused = false;
        draw_workspace_frame(model, state, options, layout_done);
        if (options.focus_test && state.focus_test_active) {
            update_focus_test_state(model, state);
        }
        draw_inspector(model, state);
        draw_minimap(model);
        draw_status(model, state);
        draw_modals(model, state);
        if (state.input_test && ImGui::GetCurrentContext()->DebugDrawIdConflictsId != 0) {
            state.id_conflict_detected = true;
        }
        handle_shortcuts(model, state);
        const bool input_done = drive_input_script(model, state);
        present_frame(window);
        if (options.splash_test) {
            ++state.splash_workspace_frames;
        }
        const bool test_done = frame_test_done(options, state);
        if (input_done || test_done) {
            break;
        }
    }
}

/// @brief Print the scripted-run observations, shut down and compute the exit code.
///
/// @param model View model supplying the final counts.
/// @param state UI state holding the observations.
/// @param options Parsed command line.
/// @param window GUI window to shut down.
/// @param capture Capture thread to stop; may be `nullptr`.
/// @return Process exit code.
// report_ui_result and its helpers are defined above.

int run_ui(LogViewModel& model, const UiOptions& options) {
    UiState state{};
    initialize_ui_state(state, model, options);
    if (!run_pre_gui_tests(model, state, options)) {
        return 1;
    }
    GLFWwindow* window = nullptr;
    if (!init_gui(state, options, window)) {
        return 1;
    }
    std::unique_ptr<CaptureThread> capture = start_capture(model, state, options);
    state.capture = capture.get();
    const bool skip_splash = splash_skipped(options);
    SplashLoad splash = load_splash(options, window, capture.get(), skip_splash);
    if (splash.exit) {
        return splash.exit_code;
    }
    int exit_code = 0;
    if (!run_splash_phase(model, state, options, window, capture.get(), skip_splash, splash.image,
                          exit_code)) {
        return exit_code;
    }
    if (options.actions_test) {
        return run_actions_test(model, options, window, capture.get());
    }
    run_frame_loop(model, state, options, window, capture.get());
    return report_ui_result(model, state, options, window, capture.get());
}

} // namespace nexigen::logview
