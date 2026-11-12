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

/// @brief One explicit observation root.
///
/// `path` is the canonical (realpath-resolved) directory and `key` is the
/// stable identifier the hub assigns to it (`run-1`, `run-2`, ...). Duplicate
/// roots collapse to one entry. The structure is a cold-path DTO owned by
/// `Discovery`.
struct RunRoot {
    std::string key{};  ///< Stable tree key assigned by `Discovery::initialize`.
    std::string path{}; ///< Canonical absolute run-root directory.

    /// @brief Render the root as a diagnostic string.
    ///
    /// @return Text naming the key and canonical path; allocates and is only
    ///         meant for logs, HTTP snapshots, and tests.
    /// @note Spec `hub/discovery`, requirement "Discovery types follow the
    ///       serialization contract".
    /// @note Cold-path diagnostic only (project mandate 9).
    [[nodiscard]] std::string to_string() const;
};

/// @brief Free diagnostic helper mirroring `RunRoot::to_string`.
///
/// @param root Root to render.
/// @return The member `to_string()` output.
/// @note Spec `hub/discovery`, requirement "Discovery types follow the
///       serialization contract".
/// @note Cold-path diagnostic only (project mandate 9).
[[nodiscard]] inline std::string to_string(const RunRoot& root) {
    return root.to_string();
}

/// @brief Read-only filesystem observer that builds and maintains the run tree.
///
/// `Discovery` canonicalizes the explicit run roots, scans them into a
/// `HubTree` (run → suite → app → instance → attempt), and watches the scanned
/// directories with inotify so creates, removals, and moves reconcile without a
/// restart. Every read target originating from `suite.json` is accepted only
/// when its real path stays inside the owning run root; outside or missing
/// targets degrade gracefully instead of failing startup. Nothing under a run
/// or artifact root is ever created, modified, or deleted.
///
/// All operations run on the hub control loop and are cold path: the observer
/// never touches an application tick, producer, snapshot, or sink callback. It
/// owns one inotify descriptor and is non-copyable.
///
/// @note Spec `hub/discovery`, requirement "Discovery is read-only".
/// @note Spec `hub/discovery`, requirement "Discovery is cold path only".
class Discovery {
public:
    /// @brief Construct an uninitialized observer.
    ///
    /// No inotify descriptor exists and the tree is empty until `initialize()`
    /// succeeds.
    Discovery() = default;

    /// @brief Stop watching and release the inotify descriptor.
    ///
    /// The discovered tree is destroyed with the observer.
    ~Discovery();

    /// @brief Deleted: the observer owns an inotify descriptor.
    Discovery(const Discovery&) = delete;

    /// @brief Deleted: the observer owns an inotify descriptor.
    Discovery& operator=(const Discovery&) = delete;

    /// @brief Validate and canonicalize roots, create inotify, and scan once.
    ///
    /// Each supplied path must be absolute, exist, and name a directory; it is
    /// resolved with `realpath` and duplicates are collapsed. The roots are
    /// sorted by canonical path and keyed `run-1`, `run-2`, ... The initial
    /// scan builds the whole tree, installs the watches, and seeds the revision
    /// counter.
    ///
    /// @param run_dirs Explicit run-root paths from `--run-dir`; an empty span
    ///        is rejected rather than scanning implicitly.
    /// @return Empty `core::Status` on success, `InvalidArgument` for an empty
    ///         list or a relative, missing, or non-directory root, or
    ///         `SystemError` when inotify cannot be created.
    /// @note Spec `hub/discovery`, requirement "Run roots are explicit and
    ///       repeatable".
    /// @note Spec `hub/discovery`, requirement "Initial scan builds the run
    ///       tree".
    [[nodiscard]] core::Status initialize(std::span<const std::string> run_dirs) noexcept;

    /// @brief Rescan every root and reconcile the tree.
    ///
    /// The serialized tree is compared with the previous signature; the
    /// revision is incremented only when it actually changed, so SSE tree
    /// consumers are not woken by a no-op scan.
    ///
    /// @return Empty `core::Status` when every root was scanned, or
    ///         `SystemError` when at least one root is no longer a directory
    ///         (the remaining roots are still reconciled).
    /// @note Spec `hub/discovery`, requirement "Initial scan builds the run
    ///       tree".
    /// @note Spec `hub/discovery`, requirement "Inotify keeps the tree live".
    [[nodiscard]] core::Status rescan() noexcept;

    /// @brief Drain pending inotify events and flag a needed rescan.
    ///
    /// Creates, deletes, and moves set the dirty flag. `IN_Q_OVERFLOW` and
    /// `IN_IGNORED` also force a rebuild of the watch set so a queue overflow
    /// degrades into a full reconciliation rather than a lost update.
    ///
    /// @note Spec `hub/discovery`, requirement "Inotify keeps the tree live"
    ///       (queue overflow triggers a rescan).
    void handle_inotify() noexcept;

    /// @brief True when an event or the reconciliation tick requires a rescan.
    ///
    /// @return The current dirty flag.
    [[nodiscard]] bool dirty() const noexcept { return dirty_; }

    /// @brief Acknowledge the dirty flag after a successful reconciliation.
    void clear_dirty() noexcept { dirty_ = false; }

    /// @brief The non-blocking inotify descriptor owned by the observer.
    ///
    /// @return Descriptor suitable for epoll registration, or `-1` before
    ///         `initialize()` succeeded.
    [[nodiscard]] int inotify_fd() const noexcept { return inotify_fd_; }

    /// @brief The last reconciled run tree.
    ///
    /// @return Const reference to tree storage owned by the observer; valid
    ///         until the next `rescan()` or destruction.
    [[nodiscard]] const HubTree& tree() const noexcept { return tree_; }

    /// @brief The canonical roots in discovery order.
    ///
    /// @return Const reference to the root list owned by the observer.
    [[nodiscard]] const std::vector<RunRoot>& roots() const noexcept { return roots_; }

    /// @brief Monotonic revision of the discovered tree.
    ///
    /// @return Counter incremented once per tree-changing scan.
    /// @note Spec `hub/http-api`, requirement "Live updates use Server-Sent
    ///       Events" (tree snapshots are pushed when discovery changes).
    [[nodiscard]] std::uint64_t revision() const noexcept { return revision_; }

private:
    /// @brief Install one inotify watch, ignoring duplicate or failed adds.
    ///
    /// @param path Directory to watch, already validated as run-root contained.
    void add_watch(std::string_view path) noexcept;

    /// @brief Drop and reinstall the watch set after overflow or directory
    ///        removal.
    void rebuild_watches() noexcept;

    /// @brief Scan one root into a `HubRun`.
    ///
    /// Reconciles suites, standalone instances, and attempts, and reports
    /// whether the run changed.
    ///
    /// @param root Root being scanned.
    /// @param out Run entry filled from the filesystem.
    /// @return `true` when the scan produced a run to include in the tree.
    [[nodiscard]] bool scan_run(const RunRoot& root, HubRun& out) noexcept;

    /// @brief Scan one `.suite/<suite_id>` metadata directory.
    ///
    /// Reads `suite.json`, derives state from execution_manager metadata, and
    /// attaches the contained application instances and execution_manager log.
    ///
    /// @param root Root that owns the suite metadata.
    /// @param suite_dir Absolute suite metadata directory.
    /// @param out Suite entry filled from the metadata.
    void scan_suite(const RunRoot& root, const std::string& suite_dir, HubSuite& out) noexcept;

    /// @brief Scan the attempt directories of one application instance.
    ///
    /// Every attempt becomes a separate instance entry with its own stream
    /// descriptors; paths resolving outside `root` are skipped.
    ///
    /// @param root Root that owns the artifact tree.
    /// @param artifacts_path Contained artifact path read from `suite.json`.
    /// @param suite_id Suite identifier used to build stream identities.
    /// @param app Application name.
    /// @param instance Instance name.
    /// @param run_key Stable run key for stream identities.
    /// @param out Instance entry receiving the attempts and streams.
    void scan_attempts(const RunRoot& root, std::string_view artifacts_path,
                       std::string_view suite_id, std::string_view app, std::string_view instance,
                       std::string_view run_key, HubInstance& out) noexcept;

    /// @brief Scan instances that are not attached to a suite.
    ///
    /// @param root Root being scanned.
    /// @param out Run entry receiving the standalone instances.
    void scan_standalone(const RunRoot& root, HubRun& out) noexcept;

    std::vector<RunRoot> roots_{}; ///< Canonical roots in discovery order.
    HubTree tree_{};               ///< Last reconciled tree snapshot.
    std::string tree_signature_{}; ///< Serialized tree used for change detection.
    std::uint64_t revision_{0};    ///< Tree-changing scan counter.
    int inotify_fd_{-1};           ///< Owned inotify descriptor, `-1` when closed.
    bool dirty_{false};            ///< Set by an event or reconciliation tick.
};

} // namespace nexigen::hub
