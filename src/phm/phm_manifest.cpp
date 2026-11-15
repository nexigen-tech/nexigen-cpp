// nexigen::phm — cold-path health-manifest serialization (change add-phm).
#include <nexigen/phm/phm_manifest.hpp>

#include <limits>
#include <utility>

#include <nexigen/core/detail/json_field.hpp>
#include <nexigen/phm/checkpoint_claim.hpp>

namespace nexigen::phm {

namespace {

// Signed count field: accepts an in-range unsigned or a signed integer so a
// negative value reaches validation instead of being silently ignored.
void parse_count(const nlohmann::json& j, const char* key, std::int64_t& out) noexcept {
    const auto it = j.find(key);
    if (it == j.end()) {
        return;
    }
    if (it->is_number_unsigned()) {
        const auto raw = it->get<std::uint64_t>();
        if (raw <= static_cast<std::uint64_t>(std::numeric_limits<std::int64_t>::max())) {
            out = static_cast<std::int64_t>(raw);
        }
        return;
    }
    if (it->is_number_integer()) {
        out = it->get<std::int64_t>();
    }
}

// Identity and supervision parameters of one supervised process. Every field is
// tolerant: a missing or wrong-typed value keeps the destination's default.
void parse_supervised_identity(const nlohmann::json& j, SupervisedProcess& process) noexcept {
    core::detail::read_string(j, "name", process.name);
    core::detail::read_string(j, "service_name", process.service_name);
    core::detail::read_unsigned(j, "alive_reference_cycle_ms", process.alive_reference_cycle_ms);
    core::detail::read_unsigned(j, "alive_min_margin_ms", process.alive_min_margin_ms);
    core::detail::read_unsigned(j, "alive_max_margin_ms", process.alive_max_margin_ms);
    core::detail::read_unsigned(j, "failed_supervision_cycles_tolerance",
                                process.failed_supervision_cycles_tolerance);
    parse_count(j, "alive_expected_indications", process.alive_expected_indications);
    parse_count(j, "alive_min_margin_cycles", process.alive_min_margin_cycles);
    parse_count(j, "alive_max_margin_cycles", process.alive_max_margin_cycles);
    core::detail::read_bool(j, "safety_critical", process.safety_critical);
    std::uint32_t expected_uid = 0;
    if (core::detail::read_unsigned(j, "expected_uid", expected_uid)) {
        process.expected_uid = expected_uid;
    }
}

// A present checkpoint array with a non-unsigned (or out-of-range) element
// rejects the whole process, leaving the array untouched.
[[nodiscard]] bool parse_checkpoints(const nlohmann::json& j,
                                     std::vector<std::uint32_t>& out) noexcept {
    return core::detail::collect_array_into<std::uint32_t>(
        j, "checkpoints", out, [](const nlohmann::json& item, std::uint32_t& id) noexcept {
            return core::detail::to_unsigned(item, id);
        });
}

// Checkpoint transitions are tolerant per element: from_json on a non-object
// element leaves the element's defaults in place.
void parse_transitions(const nlohmann::json& j, std::vector<CheckpointTransition>& out) noexcept {
    static_cast<void>(core::detail::collect_array_into<CheckpointTransition>(
        j, "transitions", out,
        [](const nlohmann::json& item, CheckpointTransition& transition) noexcept {
            from_json(item, transition);
            return true;
        }));
}

void parse_deadlines(const nlohmann::json& j, std::vector<DeadlinePair>& out) noexcept {
    static_cast<void>(core::detail::collect_array_into<DeadlinePair>(
        j, "deadlines", out, [](const nlohmann::json& item, DeadlinePair& pair) noexcept {
            from_json(item, pair);
            return true;
        }));
}

void parse_recovery_rules(const nlohmann::json& j, std::vector<RecoveryRule>& out) noexcept {
    static_cast<void>(core::detail::collect_array_into<RecoveryRule>(
        j, "recovery_rules", out, [](const nlohmann::json& item, RecoveryRule& rule) noexcept {
            from_json(item, rule);
            return true;
        }));
}

void parse_forks(const nlohmann::json& j, std::vector<LogicalFork>& out) noexcept {
    static_cast<void>(core::detail::collect_array_into<LogicalFork>(
        j, "logical_forks", out, [](const nlohmann::json& item, LogicalFork& fork) noexcept {
            from_json(item, fork);
            return true;
        }));
}

[[nodiscard]] bool contains_checkpoint(const std::vector<std::uint32_t>& checkpoints,
                                       std::uint32_t id) noexcept {
    for (const std::uint32_t candidate : checkpoints) {
        if (candidate == id) {
            return true;
        }
    }
    return false;
}

} // namespace

std::string CheckpointTransition::to_string() const {
    return std::format("CheckpointTransition(from={}, to={})", from_id, to_id);
}

std::string DeadlinePair::to_string() const {
    return std::format("DeadlinePair(source={}, target={}, min_ms={}, max_ms={})", source_id,
                       target_id, min_ms, max_ms);
}

std::string RecoveryRule::to_string() const {
    return std::format("RecoveryRule(action={}, function_group={})",
                       nexigen::phm::to_string(action), function_group);
}

std::string SupervisedProcess::to_string() const {
    return std::format("SupervisedProcess(name={}, service={}, checkpoints={}, transitions={}, "
                       "forks={}, deadlines={}, rules={}, expected={}, safety_critical={})",
                       name, service_name, checkpoints.size(), transitions.size(),
                       logical_forks.size(), deadlines.size(), recovery_rules.size(),
                       alive_expected_indications, safety_critical);
}

std::string PhmManifest::to_string() const {
    std::string out = "PhmManifest(processes=[";
    for (std::size_t i = 0; i < processes.size(); ++i) {
        if (i != 0U) {
            out += ", ";
        }
        out += processes[i].name;
    }
    out += "], bg_thread=" + bg_thread.to_string();
    out += ", fault_storage_thread=" + fault_storage_thread.to_string();
    out += ", watchdog=" + watchdog.to_string();
    out += std::format(", recovery_timeout_ms={})", recovery_timeout_ms);
    return out;
}

void to_json(nlohmann::json& j, const CheckpointTransition& transition) {
    j = nlohmann::json{{"from_id", transition.from_id}, {"to_id", transition.to_id}};
}

void to_json(nlohmann::json& j, const DeadlinePair& pair) {
    j = nlohmann::json{{"source_id", pair.source_id},
                       {"target_id", pair.target_id},
                       {"min_ms", pair.min_ms},
                       {"max_ms", pair.max_ms}};
}

void to_json(nlohmann::json& j, const RecoveryRule& rule) {
    j = nlohmann::json{{"action", nexigen::phm::to_string(rule.action)},
                       {"function_group", rule.function_group}};
}

void to_json(nlohmann::json& j, const SupervisedProcess& process) {
    auto checkpoints = nlohmann::json::array();
    for (const std::uint32_t id : process.checkpoints) {
        checkpoints.push_back(id);
    }
    auto transitions = nlohmann::json::array();
    for (const CheckpointTransition& transition : process.transitions) {
        nlohmann::json t;
        to_json(t, transition);
        transitions.push_back(std::move(t));
    }
    auto deadlines = nlohmann::json::array();
    for (const DeadlinePair& pair : process.deadlines) {
        nlohmann::json p;
        to_json(p, pair);
        deadlines.push_back(std::move(p));
    }
    auto rules = nlohmann::json::array();
    for (const RecoveryRule& rule : process.recovery_rules) {
        nlohmann::json r;
        to_json(r, rule);
        rules.push_back(std::move(r));
    }
    auto forks = nlohmann::json::array();
    for (const LogicalFork& fork : process.logical_forks) {
        nlohmann::json f;
        to_json(f, fork);
        forks.push_back(std::move(f));
    }
    j = nlohmann::json{
        {"name", process.name},
        {"service_name", process.service_name},
        {"alive_reference_cycle_ms", process.alive_reference_cycle_ms},
        {"alive_min_margin_ms", process.alive_min_margin_ms},
        {"alive_max_margin_ms", process.alive_max_margin_ms},
        {"failed_supervision_cycles_tolerance", process.failed_supervision_cycles_tolerance},
        {"alive_expected_indications", process.alive_expected_indications},
        {"alive_min_margin_cycles", process.alive_min_margin_cycles},
        {"alive_max_margin_cycles", process.alive_max_margin_cycles},
        {"safety_critical", process.safety_critical},
        {"checkpoints", std::move(checkpoints)},
        {"transitions", std::move(transitions)},
        {"logical_forks", std::move(forks)},
        {"deadlines", std::move(deadlines)},
        {"recovery_rules", std::move(rules)}};
    if (process.expected_uid.has_value()) {
        j["expected_uid"] = *process.expected_uid;
    }
}

void to_json(nlohmann::json& j, const PhmManifest& manifest) {
    auto processes = nlohmann::json::array();
    for (const SupervisedProcess& process : manifest.processes) {
        nlohmann::json p;
        to_json(p, process);
        processes.push_back(std::move(p));
    }
    j = nlohmann::json{{"processes", std::move(processes)},
                       {"bg_thread", manifest.bg_thread},
                       {"fault_storage_thread", manifest.fault_storage_thread},
                       {"watchdog", manifest.watchdog},
                       {"recovery_timeout_ms", manifest.recovery_timeout_ms}};
}

void from_json(const nlohmann::json& j, CheckpointTransition& transition) noexcept {
    if (!j.is_object()) {
        return;
    }
    core::detail::read_unsigned(j, "from_id", transition.from_id);
    core::detail::read_unsigned(j, "to_id", transition.to_id);
}

void from_json(const nlohmann::json& j, DeadlinePair& pair) noexcept {
    if (!j.is_object()) {
        return;
    }
    core::detail::read_unsigned(j, "source_id", pair.source_id);
    core::detail::read_unsigned(j, "target_id", pair.target_id);
    core::detail::read_unsigned(j, "min_ms", pair.min_ms);
    core::detail::read_unsigned(j, "max_ms", pair.max_ms);
}

void from_json(const nlohmann::json& j, RecoveryRule& rule) noexcept {
    if (!j.is_object()) {
        return;
    }
    // Non-throwing parse; a miss leaves the current action unchanged.
    std::string action;
    if (core::detail::read_string(j, "action", action)) {
        static_cast<void>(nexigen::phm::from_string(action, rule.action));
    }
    core::detail::read_string(j, "function_group", rule.function_group);
}

void from_json(const nlohmann::json& j, SupervisedProcess& process) noexcept {
    if (!j.is_object()) {
        return;
    }
    parse_supervised_identity(j, process);
    if (!parse_checkpoints(j, process.checkpoints)) {
        return;
    }
    parse_transitions(j, process.transitions);
    parse_forks(j, process.logical_forks);
    parse_deadlines(j, process.deadlines);
    parse_recovery_rules(j, process.recovery_rules);
}

void from_json(const nlohmann::json& j, PhmManifest& manifest) noexcept {
    if (!j.is_object()) {
        return;
    }
    // A non-object element is tolerated: from_json leaves its defaults, which are
    // appended (pre-refactor behavior).
    static_cast<void>(core::detail::collect_array_into<SupervisedProcess>(
        j, "processes", manifest.processes,
        [](const nlohmann::json& item, SupervisedProcess& process) noexcept {
            from_json(item, process);
            return true;
        }));
    core::detail::read_object(j, "bg_thread", [&manifest](const nlohmann::json& value) noexcept {
        from_json(value, manifest.bg_thread);
    });
    core::detail::read_object(j, "fault_storage_thread",
                              [&manifest](const nlohmann::json& value) noexcept {
                                  from_json(value, manifest.fault_storage_thread);
                              });
    core::detail::read_object(j, "watchdog", [&manifest](const nlohmann::json& value) noexcept {
        from_json(value, manifest.watchdog);
    });
    static_cast<void>(
        core::detail::read_unsigned(j, "recovery_timeout_ms", manifest.recovery_timeout_ms));
}

namespace {

[[nodiscard]] core::Status validate_watchdog_section(const WatchdogConfig& config,
                                                     std::string& error) noexcept {
    if (!config.enabled) {
        return {};
    }
    if (config.timeout_ms == 0U) {
        error = "watchdog: timeout_ms must be positive";
        return std::unexpected(core::ErrorCode::InvalidArgument);
    }
    if (config.feed_period_ms == 0U || config.feed_period_ms > config.timeout_ms / 2U) {
        error = std::format("watchdog: feed_period_ms={} must be positive and at most half of "
                            "timeout_ms={}",
                            config.feed_period_ms, config.timeout_ms);
        return std::unexpected(core::ErrorCode::InvalidArgument);
    }
    return {};
}

[[nodiscard]] core::Status validate_counts(const SupervisedProcess& process,
                                           std::string& error) noexcept {
    if (process.alive_expected_indications >= 0 && process.alive_min_margin_cycles >= 0 &&
        process.alive_max_margin_cycles >= 0) {
        return {};
    }
    error = std::format("process '{}': alive counts and count margins must be non-negative",
                        process.name);
    return std::unexpected(core::ErrorCode::InvalidArgument);
}

/// @brief Validate the optional identity expectation of one process.
///
/// A claim carries the name in a fixed-size buffer, so an entry that requires a
/// claim must have a non-empty name that fits. Otherwise the published claim
/// would always be truncated and the process could never verify, which turns a
/// configuration mistake into an Expired process and a watchdog reset.
[[nodiscard]] core::Status validate_identity(const SupervisedProcess& process,
                                             std::string& error) noexcept {
    if (!process.expected_uid.has_value()) {
        return {};
    }
    if (*process.expected_uid == 0U) {
        error = std::format("process '{}': expected_uid 0 is rejected", process.name);
        return std::unexpected(core::ErrorCode::InvalidArgument);
    }
    if (process.name.empty() || process.name.size() >= k_claim_name_length) {
        error = std::format("process '{}': expected_uid requires a name of 1..{} bytes "
                            "(claim buffer size minus the terminator)",
                            process.name, k_claim_name_length - 1U);
        return std::unexpected(core::ErrorCode::InvalidArgument);
    }
    return {};
}

[[nodiscard]] core::Status validate_forks(const SupervisedProcess& process,
                                          std::string& error) noexcept {
    if (process.logical_forks.size() > k_max_logical_forks) {
        error = std::format("process '{}': {} logical forks exceed the supported {}", process.name,
                            process.logical_forks.size(), k_max_logical_forks);
        return std::unexpected(core::ErrorCode::InvalidArgument);
    }
    for (std::size_t f = 0; f < process.logical_forks.size(); ++f) {
        const LogicalFork& fork = process.logical_forks[f];
        if (fork.branch_entries.empty()) {
            error = std::format("process '{}': fork {} declares no branch entries", process.name,
                                fork.fork_id);
            return std::unexpected(core::ErrorCode::InvalidArgument);
        }
        if (!contains_checkpoint(process.checkpoints, fork.join_id)) {
            error = std::format("process '{}': fork {} join {} is not a declared checkpoint",
                                process.name, fork.fork_id, fork.join_id);
            return std::unexpected(core::ErrorCode::InvalidArgument);
        }
        for (const std::uint32_t branch : fork.branch_entries) {
            if (!contains_checkpoint(process.checkpoints, branch)) {
                error = std::format(
                    "process '{}': fork {} branch entry {} is not a declared checkpoint",
                    process.name, fork.fork_id, branch);
                return std::unexpected(core::ErrorCode::InvalidArgument);
            }
        }
        for (std::size_t other = f + 1; other < process.logical_forks.size(); ++other) {
            if (process.logical_forks[other].fork_id == fork.fork_id) {
                error =
                    std::format("process '{}': duplicate fork id {}", process.name, fork.fork_id);
                return std::unexpected(core::ErrorCode::InvalidArgument);
            }
        }
    }
    return {};
}

[[nodiscard]] core::Status validate_process(const SupervisedProcess& process,
                                            std::string& error) noexcept {
    if (const core::Status counts = validate_counts(process, error); !counts) {
        return counts;
    }
    if (const core::Status identity = validate_identity(process, error); !identity) {
        return identity;
    }
    return validate_forks(process, error);
}

} // namespace

core::Status validate_phm_manifest(const PhmManifest& manifest, std::string& error) noexcept {
    error.clear();
    if (manifest.recovery_timeout_ms == 0U) {
        error = "recovery_timeout_ms must be positive";
        return std::unexpected(core::ErrorCode::InvalidArgument);
    }
    if (const core::Status watchdog = validate_watchdog_section(manifest.watchdog, error);
        !watchdog) {
        return watchdog;
    }
    for (const SupervisedProcess& process : manifest.processes) {
        if (const core::Status status = validate_process(process, error); !status) {
            return status;
        }
    }
    return {};
}

} // namespace nexigen::phm
