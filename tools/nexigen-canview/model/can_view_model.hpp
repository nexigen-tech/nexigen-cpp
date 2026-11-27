// nexigen-canview — view registry, focus and persistence.
#pragma once

#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include <nlohmann/json.hpp>

#include <nexigen/core/result.hpp>

#include "can_record_ring.hpp"
#include "can_view.hpp"
#include "record_source.hpp"

namespace nexigen::canview {

/// @brief Resolve a CANopen binding to a registry slot.
using NodeResolver = std::optional<std::uint16_t> (*)(std::uint16_t channel_id,
                                                      std::uint8_t node_id, void* context);

/// @brief Owns the history, views, focus and LIVE view persistence.
class CanViewModel {
public:
    /// @brief Maximum LIVE views including `All`.
    static constexpr std::size_t max_live_views{8U};
    /// @brief Maximum PAUSED clones.
    static constexpr std::size_t max_paused_views{16U};
    /// @brief Record-data budget of one PAUSED snapshot (1 MiB).
    static constexpr std::uint64_t snapshot_bytes{1ULL * 1024ULL * 1024ULL};

    /// @brief Bind the model to one history ring.
    /// @param ring Ring that must outlive the model.
    explicit CanViewModel(CanRecordRing& ring) noexcept : ring_(ring), source_(ring) {}

    /// @brief Shared history.
    /// @return Ring reference.
    [[nodiscard]] CanRecordRing& ring() noexcept { return ring_; }
    /// @brief Read-only history source.
    /// @return Source reference.
    [[nodiscard]] const RecordSource& source() const noexcept { return source_; }

    /// @brief Create a LIVE view.
    /// @param kind Scope kind.
    /// @param channel_id Fixed channel for `Channel`/`Node`.
    /// @param node_id Fixed node for `Node` (1..127).
    /// @param title Display title.
    /// @param defaults Initial table/time settings.
    /// @param node_slot Explicit binding slot (tests); `std::nullopt` resolves it.
    /// @return New view id, or a limit/argument error.
    [[nodiscard]] nexigen::core::Result<std::uint32_t>
    create_view(ViewKind kind, std::uint16_t channel_id, std::uint8_t node_id, std::string title,
                const ViewDefaults& defaults,
                std::optional<std::uint16_t> node_slot = std::nullopt);

    /// @brief Clone one view.
    /// @param source_id View to clone.
    /// @param paused `true` for an immutable snapshot, `false` for a LIVE clone.
    /// @param title Display title.
    /// @param options State groups to copy.
    /// @param defaults Fallback table/time settings.
    /// @param adjustments Optional clipping summary written on success.
    /// @return New view id, or a limit/argument error.
    [[nodiscard]] nexigen::core::Result<std::uint32_t>
    clone_view(std::uint32_t source_id, bool paused, std::string title, const CloneOptions& options,
               const ViewDefaults& defaults, CloneAdjustments* adjustments = nullptr);

    /// @brief Remove one view.
    /// @param id View identity.
    /// @return `true` when removed.
    bool remove_view(std::uint32_t id);

    /// @brief Rename one view.
    /// @param id View identity.
    /// @param title New display title; surrounding whitespace is trimmed.
    /// @return Empty status, or `NotFound`/`InvalidArgument`.
    [[nodiscard]] nexigen::core::Status rename_view(std::uint32_t id, std::string title);

    /// @brief Drop every view.
    void clear_views();

    /// @brief Look up one view.
    /// @param id View identity.
    /// @return Borrowed view, or `nullptr`.
    [[nodiscard]] CanView* view(std::uint32_t id) noexcept;
    /// @brief Look up one view.
    /// @param id View identity.
    /// @return Borrowed view, or `nullptr`.
    [[nodiscard]] const CanView* view(std::uint32_t id) const noexcept;
    /// @brief All views in creation order.
    /// @return Borrowed vector.
    [[nodiscard]] const std::vector<std::unique_ptr<CanView>>& views() const noexcept {
        return views_;
    }

    /// @brief Number of LIVE views.
    /// @return Count.
    [[nodiscard]] std::size_t live_count() const noexcept;
    /// @brief Number of PAUSED views.
    /// @return Count.
    [[nodiscard]] std::size_t paused_count() const noexcept;

    /// @brief Append a new record to every LIVE view.
    /// @param seq Absolute record number.
    void ingest(std::uint64_t seq);
    /// @brief Advance every in-progress rebuild.
    /// @param max_records Chunk size per view.
    void step_rebuilds(std::size_t max_records = 20'000U);
    /// @brief Rebuild every view completely.
    void rebuild_all();

    /// @brief Install the CANopen binding resolver.
    /// @param resolver Resolver, or `nullptr` to leave node views unresolved.
    /// @param context Opaque context passed to the resolver.
    void set_node_resolver(NodeResolver resolver, void* context = nullptr) noexcept;
    /// @brief Re-resolve every node view, orphaning and recovering as needed.
    void refresh_node_views();

    /// @brief Focus one view.
    /// @param id View identity, or `std::nullopt` to clear focus.
    void focus(std::optional<std::uint32_t> id) noexcept;
    /// @brief Focused view id.
    /// @return Identity or `std::nullopt`.
    [[nodiscard]] std::optional<std::uint32_t> focused_id() const noexcept { return focus_id_; }
    /// @brief Focused view.
    /// @return Borrowed view, or `nullptr`.
    [[nodiscard]] CanView* focused() noexcept;
    /// @brief Focus the first view when no view is focused.
    /// @return Focused view, or `nullptr` when the model has no views.
    [[nodiscard]] CanView* ensure_focused() noexcept;

    /// @brief Left-panel shortcut: show/hide one channel in the focused view.
    /// @param channel_id Channel identity.
    /// @param visible New state.
    /// @return `true` when a focused view was edited.
    bool set_focused_channel_visible(std::uint16_t channel_id, bool visible) noexcept;
    /// @brief Left-panel shortcut: show/hide one CANopen slot in the focused view.
    /// @param slot Registry slot.
    /// @param visible New state.
    /// @return `true` when a focused view was edited.
    bool set_focused_node_visible(std::uint16_t slot, bool visible) noexcept;

    /// @brief Persist LIVE views as JSON.
    /// @return `{"views":[...]}` in creation order.
    [[nodiscard]] nlohmann::json to_json() const;

    /// @brief Result of loading persisted views.
    struct LoadReport {
        std::size_t loaded{0};  ///< Views created.
        std::size_t skipped{0}; ///< Entries rejected by the LIVE limit.
        std::size_t failed{0};  ///< Malformed or invalid entries.
    };

    /// @brief Replace every view with the persisted LIVE views.
    /// @param document `{"views":[...]}` produced by `to_json()`.
    /// @param defaults Fallback table/time settings.
    /// @return Load report; PAUSED views are never persisted.
    LoadReport load_json(const nlohmann::json& document, const ViewDefaults& defaults);

    /// @brief Override the PAUSED clone limit (degraded mode).
    /// @param limit New limit; clamped to `max_paused_views`.
    void set_paused_limit(std::size_t limit) noexcept {
        max_paused_ = std::min(limit, max_paused_views);
    }

    /// @brief Inject a view-index allocation failure (tests only).
    /// @param fail New state.
    void fail_view_allocation_for_tests(bool fail) noexcept { fail_view_ = fail; }
    /// @brief Inject a PAUSED snapshot allocation failure (tests only).
    /// @param fail New state.
    void fail_snapshot_allocation_for_tests(bool fail) noexcept { fail_snapshot_ = fail; }

    /// @brief Cold-path diagnostic rendering.
    /// @return Text with the view counts and focused id.
    [[nodiscard]] std::string to_string() const;

private:
    /// @brief Create a LIVE view without limit/argument checks.
    std::uint32_t add_live(ViewKind kind, std::uint16_t channel_id, std::uint8_t node_id,
                           std::optional<std::uint16_t> node_slot, std::string title,
                           const ViewDefaults& defaults);
    /// @brief Build a PAUSED clone.
    nexigen::core::Result<std::uint32_t> add_paused(const CanView& source_view, std::string title,
                                                    const CloneOptions& options,
                                                    const ViewDefaults& defaults,
                                                    CloneAdjustments* adjustments);

    CanRecordRing& ring_;                           ///< Shared history.
    RingRecordSource source_;                       ///< Source over the ring.
    std::vector<std::unique_ptr<CanView>> views_{}; ///< Views in creation order.
    std::uint32_t next_id_{1};                      ///< Next view identity.
    std::optional<std::uint32_t> focus_id_{};       ///< Focused view.
    NodeResolver resolver_{nullptr};                ///< Binding resolver.
    std::size_t max_paused_{max_paused_views};      ///< Effective PAUSED limit.
    bool fail_view_{false};                         ///< Test-only view allocation failure.
    bool fail_snapshot_{false};                     ///< Test-only snapshot allocation failure.
    void* resolver_context_{nullptr};               ///< Resolver context.
};

} // namespace nexigen::canview
