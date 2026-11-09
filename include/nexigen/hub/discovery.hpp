// nexigen::hub — run-root discovery and inotify reconciliation
// (change nexigen-hub; spec hub/discovery).
#pragma once

#include <cstdint>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include <nexigen/core/result.hpp>
#include <nexigen/hub/hub_tree.hpp>

namespace nexigen::hub {

/// One explicit observation root.
struct RunRoot {
    std::string key{};
    std::string path{};

    [[nodiscard]] std::string to_string() const;
};

[[nodiscard]] inline std::string to_string(const RunRoot& root) {
    return root.to_string();
}

/// Read-only filesystem observer. All operations run on the hub control loop.
class Discovery {
public:
    Discovery() = default;
    ~Discovery();

    Discovery(const Discovery&) = delete;
    Discovery& operator=(const Discovery&) = delete;

    /// Validate/canonicalize roots, create inotify, and perform the initial scan.
    [[nodiscard]] core::Status initialize(std::span<const std::string> run_dirs) noexcept;

    /// Rescan every root and reconcile the tree. Increments revision only when
    /// the serialized tree changed.
    [[nodiscard]] core::Status rescan() noexcept;

    /// Consume pending inotify events; sets the dirty flag on any relevant event.
    void handle_inotify() noexcept;

    /// True when an event or the reconciliation tick requires a rescan.
    [[nodiscard]] bool dirty() const noexcept { return dirty_; }
    void clear_dirty() noexcept { dirty_ = false; }

    [[nodiscard]] int inotify_fd() const noexcept { return inotify_fd_; }
    [[nodiscard]] const HubTree& tree() const noexcept { return tree_; }
    [[nodiscard]] const std::vector<RunRoot>& roots() const noexcept { return roots_; }
    [[nodiscard]] std::uint64_t revision() const noexcept { return revision_; }

private:
    void add_watch(std::string_view path) noexcept;
    void rebuild_watches() noexcept;
    [[nodiscard]] bool scan_run(const RunRoot& root, HubRun& out) noexcept;
    void scan_suite(const RunRoot& root, const std::string& suite_dir, HubSuite& out) noexcept;
    void scan_attempts(const RunRoot& root, std::string_view artifacts_path,
                       std::string_view suite_id, std::string_view app, std::string_view instance,
                       std::string_view run_key, HubInstance& out) noexcept;
    void scan_standalone(const RunRoot& root, HubRun& out) noexcept;

    std::vector<RunRoot> roots_{};
    HubTree tree_{};
    std::string tree_signature_{};
    std::uint64_t revision_{0};
    int inotify_fd_{-1};
    bool dirty_{false};
};

} // namespace nexigen::hub
