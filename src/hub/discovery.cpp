// nexigen::hub — run-root discovery and inotify reconciliation
// (change nexigen-hub; spec hub/discovery).
#include <algorithm>
#include <cerrno>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <limits.h>
#include <string>
#include <string_view>
#include <unordered_map>
#include <utility>
#include <vector>

#include <dirent.h>
#include <sys/inotify.h>
#include <sys/stat.h>
#include <unistd.h>

#include <nlohmann/json.hpp>

#include <nexigen/core/result.hpp>
#include <nexigen/hub/discovery.hpp>
#include <nexigen/hub/model.hpp>
#include <nexigen/runner/app_suite.hpp>
#include <nexigen/runner/instance_lifecycle.hpp>
#include <nexigen/runner/runner.hpp>

namespace nexigen::hub {

namespace {

constexpr std::uint32_t watch_mask =
    IN_CREATE | IN_DELETE | IN_MOVED_FROM | IN_MOVED_TO | IN_MODIFY |
    IN_CLOSE_WRITE | IN_MOVE_SELF | IN_DELETE_SELF;

[[nodiscard]] std::string join(std::string_view left, std::string_view right) {
    return std::string(left) + "/" + std::string(right);
}

[[nodiscard]] bool is_directory(std::string_view path) noexcept {
    struct stat status {};
    return ::stat(std::string(path).c_str(), &status) == 0 &&
           S_ISDIR(status.st_mode);
}

[[nodiscard]] bool is_regular(std::string_view path) noexcept {
    struct stat status {};
    return ::stat(std::string(path).c_str(), &status) == 0 &&
           S_ISREG(status.st_mode);
}

[[nodiscard]] std::vector<std::string> list_directory(std::string_view path) {
    std::vector<std::string> names;
    DIR* directory = ::opendir(std::string(path).c_str());
    if (directory == nullptr) {
        return names;
    }
    while (const dirent* entry = ::readdir(directory)) {
        const std::string_view name{entry->d_name};
        if (name == "." || name == "..") {
            continue;
        }
        names.emplace_back(name);
    }
    static_cast<void>(::closedir(directory));
    std::sort(names.begin(), names.end());
    return names;
}

[[nodiscard]] std::string canonical_path(std::string_view path) {
    char buffer[PATH_MAX]{};
    if (::realpath(std::string(path).c_str(), buffer) == nullptr) {
        return {};
    }
    return std::string(buffer);
}

[[nodiscard]] bool starts_with(std::string_view text,
                               std::string_view prefix) noexcept {
    return text.size() >= prefix.size() &&
           text.compare(0, prefix.size(), prefix) == 0;
}

[[nodiscard]] std::uint32_t attempt_number(std::string_view name) noexcept {
    if (!starts_with(name, "attempt-") || name.size() <= 8) {
        return 0;
    }
    std::uint32_t value = 0;
    for (std::size_t index = 8; index < name.size(); ++index) {
        const char c = name[index];
        if (c < '0' || c > '9') {
            return 0;
        }
        value = value * 10U + static_cast<std::uint32_t>(c - '0');
    }
    return value;
}

[[nodiscard]] std::string stream_id(std::string_view run_key,
                                    std::string_view suite_id,
                                    std::string_view app,
                                    std::string_view instance,
                                    std::uint32_t attempt,
                                    Stream_kind kind) {
    return std::string(run_key) + ":" + std::string(suite_id) + ":" +
           std::string(app) + ":" + std::string(instance) + ":" +
           std::to_string(attempt) + ":" + std::string(to_string(kind));
}

[[nodiscard]] runner::App_state combine_app_state(
    const std::vector<runner::App_state>& states) noexcept {
    bool running = false;
    bool restarting = false;
    bool failed = false;
    bool stopped = false;
    bool exited = false;
    for (const runner::App_state state : states) {
        if (state == runner::App_state::Failed) {
            failed = true;
        } else if (state == runner::App_state::Restarting) {
            restarting = true;
        } else if (state == runner::App_state::Running) {
            running = true;
        } else if (state == runner::App_state::Stopped) {
            stopped = true;
        } else if (state == runner::App_state::Exited) {
            exited = true;
        }
    }
    if (failed) {
        return runner::App_state::Failed;
    }
    if (restarting) {
        return runner::App_state::Restarting;
    }
    if (running) {
        return runner::App_state::Running;
    }
    if (stopped) {
        return runner::App_state::Stopped;
    }
    if (exited) {
        return runner::App_state::Exited;
    }
    return runner::App_state::Unknown;
}

[[nodiscard]] runner::App_state app_state_from_string(std::string_view state) {
    if (state == "running") {
        return runner::App_state::Running;
    }
    if (state == "restarting") {
        return runner::App_state::Restarting;
    }
    if (state == "exited") {
        return runner::App_state::Exited;
    }
    if (state == "stopped") {
        return runner::App_state::Stopped;
    }
    if (state == "failed") {
        return runner::App_state::Failed;
    }
    return runner::App_state::Unknown;
}

[[nodiscard]] runner::App_state instance_state(const std::string& instance_dir) {
    runner::Runner runner;
    std::string state;
    if (!runner.status(instance_dir, state).has_value()) {
        return runner::App_state::Unknown;
    }
    return app_state_from_string(state);
}

void append_attempt_streams(Hub_attempt& attempt, std::string_view run_key,
                            std::string_view suite_id, std::string_view app,
                            std::string_view instance) {
    struct Entry {
        const char* name;
        Stream_kind kind;
    };
    static constexpr Entry entries[] = {
        {"app.jsonl", Stream_kind::App},
        {"metrics.jsonl", Stream_kind::Metrics},
        {"traces.jsonl", Stream_kind::Traces},
        {"chrono.jsonl", Stream_kind::Chrono},
        {"stdout.log", Stream_kind::Stdout},
        {"stderr.log", Stream_kind::Stderr},
    };
    attempt.streams.reserve(6);
    for (const Entry& entry : entries) {
        Hub_stream stream{};
        stream.id = stream_id(run_key, suite_id, app, instance,
                              attempt.attempt, entry.kind);
        stream.kind = entry.kind;
        stream.path = join(attempt.artifact_dir, entry.name);
        attempt.streams.push_back(std::move(stream));
    }
}

} // namespace

std::string Run_root::to_string() const {
    return "Run_root(key=" + key + ", path=" + path + ")";
}

Discovery::~Discovery() {
    if (inotify_fd_ >= 0) {
        static_cast<void>(::close(inotify_fd_));
    }
}

core::Status Discovery::initialize(
    std::span<const std::string> run_dirs) noexcept {
    if (run_dirs.empty()) {
        return std::unexpected(core::ErrorCode::InvalidArgument);
    }
    roots_.clear();
    for (const std::string& supplied : run_dirs) {
        const std::string canonical = canonical_path(supplied);
        if (canonical.empty() || !is_directory(canonical)) {
            return std::unexpected(core::ErrorCode::InvalidArgument);
        }
        if (std::any_of(roots_.begin(), roots_.end(),
                        [&](const Run_root& root) {
                            return root.path == canonical;
                        })) {
            continue;
        }
        roots_.push_back(Run_root{"run-" + std::to_string(roots_.size() + 1),
                                  canonical});
    }
    std::sort(roots_.begin(), roots_.end(),
              [](const Run_root& left, const Run_root& right) {
                  return left.path < right.path;
              });
    for (std::size_t index = 0; index < roots_.size(); ++index) {
        roots_[index].key = "run-" + std::to_string(index + 1);
    }

    inotify_fd_ = ::inotify_init1(IN_NONBLOCK | IN_CLOEXEC);
    if (inotify_fd_ < 0) {
        return std::unexpected(core::ErrorCode::SystemError);
    }
    tree_ = Hub_tree{};
    tree_signature_.clear();
    revision_ = 0;
    const core::Status scanned = rescan();
    if (!scanned) {
        return scanned;
    }
    return {};
}

core::Status Discovery::rescan() noexcept {
    Hub_tree next{};
    bool failed = false;
    for (const Run_root& root : roots_) {
        if (!is_directory(root.path)) {
            failed = true;
            continue;
        }
        Hub_run run{};
        if (scan_run(root, run)) {
            next.runs.push_back(std::move(run));
        }
    }
    nlohmann::json document;
    to_json(document, next);
    std::string signature = document.dump();
    if (signature != tree_signature_) {
        tree_ = std::move(next);
        tree_signature_ = std::move(signature);
        ++revision_;
        rebuild_watches();
    }
    return failed ? core::Status{std::unexpected(core::ErrorCode::SystemError)}
                  : core::Status{};
}

void Discovery::handle_inotify() noexcept {
    if (inotify_fd_ < 0) {
        return;
    }
    alignas(inotify_event) char buffer[64 * 1024];
    for (;;) {
        const ssize_t count = ::read(inotify_fd_, buffer, sizeof(buffer));
        if (count <= 0) {
            if (count < 0 && errno == EINTR) {
                continue;
            }
            break;
        }
        std::size_t offset = 0;
        while (offset < static_cast<std::size_t>(count)) {
            const auto* event =
                reinterpret_cast<const inotify_event*>(buffer + offset);
            if ((event->mask & IN_Q_OVERFLOW) != 0U ||
                (event->mask & IN_IGNORED) != 0U) {
                dirty_ = true;
            }
            if ((event->mask & (IN_CREATE | IN_DELETE | IN_MOVED_FROM |
                                IN_MOVED_TO | IN_MODIFY | IN_CLOSE_WRITE |
                                IN_MOVE_SELF | IN_DELETE_SELF)) != 0U) {
                dirty_ = true;
            }
            offset += sizeof(inotify_event) + event->len;
        }
    }
}

void Discovery::add_watch(std::string_view path) noexcept {
    if (inotify_fd_ < 0 || path.empty() || !is_directory(path)) {
        return;
    }
    const std::string name(path);
    const int descriptor = ::inotify_add_watch(inotify_fd_, name.c_str(),
                                              watch_mask);
    static_cast<void>(descriptor);
}

void Discovery::rebuild_watches() noexcept {
    if (inotify_fd_ < 0) {
        return;
    }
    for (const Run_root& root : roots_) {
        add_watch(root.path);
        add_watch(join(root.path, ".suite"));
    }
    for (const Hub_run& run : tree_.runs) {
        for (const Hub_suite& suite : run.suites) {
            add_watch(suite.suite_dir);
            if (!suite.artifacts_path.empty()) {
                add_watch(suite.artifacts_path);
            }
            for (const Hub_app& app : suite.apps) {
                for (const Hub_instance& instance : app.instances) {
                    add_watch(instance.instance_dir);
                    for (const Hub_attempt& attempt : instance.attempts) {
                        add_watch(attempt.artifact_dir);
                    }
                }
            }
        }
        for (const Hub_instance& instance : run.standalone_instances) {
            add_watch(instance.instance_dir);
            for (const Hub_attempt& attempt : instance.attempts) {
                add_watch(attempt.artifact_dir);
            }
        }
    }
}

bool Discovery::scan_run(const Run_root& root, Hub_run& out) noexcept {
    out.run_key = root.key;
    out.run_dir = root.path;

    const std::string suite_root = join(root.path, ".suite");
    for (const std::string& name : list_directory(suite_root)) {
        const std::string suite_dir = join(suite_root, name);
        if (!is_directory(suite_dir) ||
            !is_regular(join(suite_dir, "suite.json"))) {
            continue;
        }
        Hub_suite suite{};
        scan_suite(root, suite_dir, suite);
        if (!suite.suite_id.empty()) {
            out.suites.push_back(std::move(suite));
        }
    }
    scan_standalone(root, out);
    return true;
}

void Discovery::scan_suite(const Run_root& root, const std::string& suite_dir,
                           Hub_suite& out) noexcept {
    const core::Result<nlohmann::json> document =
        runner::read_json_file(join(suite_dir, "suite.json"));
    if (!document) {
        return;
    }
    runner::Suite_metadata metadata{};
    runner::from_json(*document, metadata);
    if (metadata.suite_id.empty()) {
        return;
    }

    out.suite_id = metadata.suite_id;
    out.suite_dir = suite_dir;
    out.artifacts_path = metadata.artifacts_path;
    if (!out.artifacts_path.empty()) {
        out.runner_log.id = stream_id(root.key, out.suite_id, "suite", "runner",
                                      0, Stream_kind::Runner);
        out.runner_log.kind = Stream_kind::Runner;
        out.runner_log.path = join(out.artifacts_path, "runner.jsonl");
    }

    runner::Suite_state recorded_state = runner::Suite_state::Unknown;
    runner::Suite_status status{};
    bool have_status = false;
    if (const core::Result<nlohmann::json> state_document =
            runner::read_json_file(join(suite_dir, "suite-state.json"))) {
        runner::from_json(*state_document, status);
        have_status = true;
        recorded_state = status.state;
        out.suite_restarts_used = status.suite_restarts_used;
    }
    if (const core::Result<nlohmann::json> exit_document =
            runner::read_json_file(join(suite_dir, "suite-exit.json"))) {
        runner::Suite_exit exit{};
        runner::from_json(*exit_document, exit);
        recorded_state = exit.state;
        out.reason = exit.reason;
    }
    out.state = recorded_state;

    std::unordered_map<std::string, std::size_t> app_index{};
    for (const runner::Suite_app_metadata& app_meta : metadata.apps) {
        std::size_t index = 0;
        const auto found = app_index.find(app_meta.app);
        if (found == app_index.end()) {
            index = out.apps.size();
            Hub_app app{};
            app.app = app_meta.app;
            out.apps.push_back(std::move(app));
            app_index.emplace(app_meta.app, index);
        } else {
            index = found->second;
        }
        Hub_instance instance{};
        instance.instance = app_meta.instance;
        instance.instance_dir = app_meta.instance_dir;
        instance.state = instance_state(instance.instance_dir);

        if (have_status) {
            for (const runner::Suite_app_status& recorded : status.apps) {
                if (recorded.app == app_meta.app &&
                    recorded.instance == app_meta.instance) {
                    instance.state = recorded.state;
                    break;
                }
            }
        }
        scan_attempts(metadata.artifacts_path, metadata.suite_id, app_meta.app,
                      app_meta.instance, root.key, instance);
        out.apps[index].instances.push_back(std::move(instance));
    }
    for (Hub_app& app : out.apps) {
        std::vector<runner::App_state> states;
        states.reserve(app.instances.size());
        for (const Hub_instance& instance : app.instances) {
            states.push_back(instance.state);
        }
        app.state = combine_app_state(states);
    }

    if (out.state == runner::Suite_state::Unknown) {
        std::vector<runner::App_state> states;
        states.reserve(out.apps.size());
        for (const Hub_app& app : out.apps) {
            states.push_back(app.state);
        }
        const runner::App_state combined = combine_app_state(states);
        if (combined == runner::App_state::Running ||
            combined == runner::App_state::Restarting) {
            out.state = runner::Suite_state::Running;
        } else if (combined == runner::App_state::Failed) {
            out.state = runner::Suite_state::Failed;
        }
    }
}

void Discovery::scan_attempts(std::string_view artifacts_path,
                              std::string_view suite_id, std::string_view app,
                              std::string_view instance,
                              std::string_view run_key,
                              Hub_instance& out) noexcept {
    if (artifacts_path.empty()) {
        return;
    }
    const std::string base =
        join(join(join(artifacts_path, suite_id), app), instance);
    for (const std::string& name : list_directory(base)) {
        const std::uint32_t attempt = attempt_number(name);
        if (attempt == 0U) {
            continue;
        }
        Hub_attempt entry{};
        entry.attempt = attempt;
        entry.artifact_dir = join(base, name);
        append_attempt_streams(entry, run_key, suite_id, app, instance);
        out.attempts.push_back(std::move(entry));
    }
    std::sort(out.attempts.begin(), out.attempts.end(),
              [](const Hub_attempt& left, const Hub_attempt& right) {
                  return left.attempt < right.attempt;
              });
}

void Discovery::scan_standalone(const Run_root& root, Hub_run& out) noexcept {
    const std::vector<std::string> names = list_directory(root.path);
    out.standalone_instances.reserve(names.size());
    for (const std::string& name : names) {
        if (name == ".suite") {
            continue;
        }
        const std::string candidate = join(root.path, name);
        if (!is_directory(candidate) ||
            !is_regular(join(candidate, "resource.json"))) {
            continue;
        }
        Hub_instance instance{};
        instance.instance = "1";
        instance.instance_dir = candidate;
        instance.state = instance_state(candidate);
        Hub_attempt attempt{};
        attempt.attempt = 1;
        attempt.artifact_dir = candidate;
        append_attempt_streams(attempt, root.key, "standalone", name, "1");
        instance.attempts.push_back(std::move(attempt));
        out.standalone_instances.push_back(std::move(instance));
    }
}

} // namespace nexigen::hub
